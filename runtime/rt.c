#include "rt.h"
#include <math.h>
extern const uint8_t guest_image[];
extern const size_t guest_image_size;
extern const uint64_t guest_base;
int cpu_init(CPU *c) {
    memset(c,0,sizeof *c);
    c->base=guest_base; c->image_size=guest_image_size;
    c->stack_base=UINT64_C(0x100000000); c->stack_size=1024*1024;
    c->limit=UINT64_C(1000000000);
    c->image=malloc(c->image_size); c->stack=calloc(1,c->stack_size);
    if(!c->image || !c->stack) { cpu_free(c); return 0; }
    memcpy(c->image,guest_image,c->image_size);
    c->r[RSP]=c->stack_base+c->stack_size-8;
    if(!host_init(c)) { cpu_free(c); return 0; }
    bind_imports(c);
    return 1;
}
void cpu_free(CPU *c) { host_free(c); free(c->image); free(c->stack); c->image=c->stack=NULL; }
extern void vm_enter(CPU *),vm_leave(CPU *);
extern int vm_stopping(CPU *),vm_failed(CPU *);
int run(CPU *c, uint64_t entry) {
    c->pc=entry;unsigned quantum=256,ticks=0;const char *setting=getenv("WINBOX_VM_QUANTUM");
    if(setting) { unsigned long value=strtoul(setting,NULL,10);if(value>=1 && value<=65536) quantum=value; }
    vm_enter(c);
    while(c->pc && !c->failed) {
        if(vm_stopping(c)) { if(vm_failed(c)) c->failed=1;break; }
        if(++c->steps>c->limit) { trap(c,"instruction limit");break; }
        if(!validate_code(c)) break;
        if(!translated_step(c)) {
            const char *name=import_name(c->pc);
            if(!name || !native_import(c,name)) trap(c,name ? name : "address has no translated body");
        }
        /* The VM remains serialized; amortize the host mutex across a bounded
           instruction quantum. Blocking imports explicitly release the VM. */
        if(++ticks==quantum) { vm_leave(c);vm_enter(c);ticks=0; }
    }
    vm_leave(c);return !c->failed;
}

int callback(CPU *c, uint64_t address, const uint64_t *args, unsigned n) {
    uint64_t saved_pc=c->pc, saved_rsp=c->r[RSP];
    unsigned regs[]={RCX,RDX,R8,R9};
    c->r[RSP]=(saved_rsp-256)&~UINT64_C(15);
    for(unsigned i=0;i<n;i++) {
        if(i<4) c->r[regs[i]]=args[i];
        else store(c,c->r[RSP]+32+(i-4)*8,64,args[i]);
    }
    push(c,0);
    int ok=run(c,address);
    c->r[RSP]=saved_rsp; c->pc=saved_pc;
    return ok;
}

void wide_integer(CPU *c,uint64_t b,unsigned bits,unsigned operation) {
#if defined(__SIZEOF_INT128__)
    if(bits!=16 && bits!=32 && bits!=64) { trap(c,"unsupported wide integer width");return; }
    __uint128_t m=((__uint128_t)1<<bits)-1;
    __uint128_t a=c->r[RAX]&m,hi=c->r[RDX]&m;
    if(operation>=2) {
        __uint128_t product;
        if(operation==2) product=a*(b&m);
        else {
            __int128 aa=(a&((__uint128_t)1<<(bits-1))) ? (__int128)a-((__int128)1<<bits) : (__int128)a;
            __int128 bb=(b&((uint64_t)1<<(bits-1))) ? (__int128)(b&(uint64_t)m)-((__int128)1<<bits) : (__int128)b;
            product=(__uint128_t)(aa*bb);
        }
        setreg(c,RAX,bits,0,(uint64_t)product);setreg(c,RDX,bits,0,(uint64_t)(product>>bits));
        c->cf=c->of=operation==2 ? (product>>bits)!=0 : ((product>>bits)&m)!=((product&((__uint128_t)1<<(bits-1))) ? m : 0);
        return;
    }
    if(!b) { trap(c,"integer division by zero");return; }
    __uint128_t dividend=(hi<<bits)|a,q,r;
    if(operation==0) { q=dividend/(b&m);r=dividend%(b&m);if(q>m) { trap(c,"integer division overflow");return; } }
    else {
        int negative=(hi>>(bits-1))&1,divisor_negative=(b>>(bits-1))&1;
        __uint128_t magnitude=negative ? (~dividend+1) : dividend;
        if(bits<64) magnitude&=(((__uint128_t)1<<(bits*2))-1);
        uint64_t divisor=divisor_negative ? ((~b+1)&(uint64_t)m) : b&(uint64_t)m;
        q=magnitude/divisor;r=magnitude%divisor;
        __uint128_t bound=(__uint128_t)1<<(bits-1);
        if(q>(negative!=divisor_negative ? bound : bound-1)) { trap(c,"signed integer division overflow");return; }
        if(negative!=divisor_negative) q=-q;if(negative) r=-r;
    }
    setreg(c,RAX,bits,0,(uint64_t)q);setreg(c,RDX,bits,0,(uint64_t)r);
#else
    (void)b;(void)bits;(void)operation;trap(c,"compiler needs 128-bit integer support");
#endif
}
uint64_t vector_bits(CPU *c,unsigned regnum,unsigned bits) {
    uint64_t v=0;for(unsigned i=0;i<bits/8;i++) v|=(uint64_t)c->xmm[regnum][i]<<(i*8);return v;
}
void vector_set_bits(CPU *c,unsigned regnum,unsigned bits,uint64_t value) { for(unsigned i=0;i<bits/8;i++) c->xmm[regnum][i]=(uint8_t)(value>>(i*8)); }
static double scalar_value(uint64_t v,unsigned bits) { if(bits==32) { uint32_t n=(uint32_t)v;float f;memcpy(&f,&n,4);return f; }double d;memcpy(&d,&v,8);return d; }
static uint64_t scalar_bits(double value,unsigned bits) { uint64_t v=0;if(bits==32) { float f=(float)value;uint32_t n;memcpy(&n,&f,4);v=n; }else memcpy(&v,&value,8);return v; }
void scalar_math(CPU *c,unsigned dest,uint64_t source,unsigned bits,unsigned operation) {
    double a=scalar_value(vector_bits(c,dest,bits),bits),b=scalar_value(source,bits),v=0;
    if(operation==0) v=a+b;else if(operation==1) v=a-b;else if(operation==2) v=a*b;else if(operation==3) v=a/b;
    else if(operation==4) { c->cf=isnan(a)||isnan(b)||a<b;c->zf=isnan(a)||isnan(b)||a==b;c->pf=isnan(a)||isnan(b);c->of=c->sf=0;return; }
    else if(operation==5) { vector_set_bits(c,dest,64,!(a<=b) ? UINT64_MAX : 0);return; }
    vector_set_bits(c,dest,bits,scalar_bits(v,bits));
}
void scalar_convert(CPU *c,unsigned dest,uint64_t source,unsigned srcbits,unsigned dstbits,unsigned kind) {
    if(kind==0) { double d=dstbits==32 ? (double)(srcbits==64 ? (float)(int64_t)source : (float)(int32_t)source) : srcbits==64 ? (double)(int64_t)source : (double)(int32_t)source;vector_set_bits(c,dest,dstbits,scalar_bits(d,dstbits)); }
    else if(kind==1) { double d=scalar_value(source,srcbits);vector_set_bits(c,dest,dstbits,scalar_bits(d,dstbits)); }
    else { double d=scalar_value(source,srcbits);d=kind==2 ? trunc(d) : nearbyint(d);double bound=ldexp(1.,dstbits-1);uint64_t n=!isfinite(d)||d < -bound||d>=bound ? UINT64_C(1)<<(dstbits-1) : (uint64_t)(int64_t)d;setreg(c,dest,dstbits,0,n); }
}
