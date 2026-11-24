#ifndef WINBOX_RT_H
#define WINBOX_RT_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t r[16], pc, steps, limit;
    unsigned cf, zf, sf, of, pf, df;
    uint8_t xmm[16][16];
    double fp[8];
    unsigned fp_top;
    uint64_t gs_base, fs_base;
    void *host;
    uint8_t *image, *stack;
    uint64_t base, image_size, stack_base, stack_size;
    uint32_t last_error;
    int failed;
} CPU;
enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
static inline uint64_t mask(unsigned bits) { return bits == 64 ? UINT64_MAX : (UINT64_C(1)<<bits)-1; }
static inline void trap(CPU *c, const char *why) {
    fprintf(stderr, "WinBox lift stopped at 0x%llx: %s\n", (unsigned long long)c->pc, why);
    c->failed=1; c->pc=0;
}
uint8_t *memory(CPU *c, uint64_t a, size_t n);
uint64_t guest_alloc(CPU *c, size_t size);
void guest_free(CPU *c, uint64_t address);
char *guest_string(CPU *c, uint64_t address);
uint64_t guest_string_alloc(CPU *c, const char *str);
uint64_t argument(CPU *c, unsigned n);
int host_init(CPU *c);
void host_free(CPU *c);
void bind_imports(CPU *c);
uint64_t crt_data(CPU *c,const char *name);
int host_api(CPU *c, const char *name, uint64_t *result);
int crt_api(CPU *c, const char *name, uint64_t *result);
int ui_api(CPU *c, const char *name, uint64_t *result);
int callback(CPU *c, uint64_t address, const uint64_t *args, unsigned n);
/* Bytewise access is host alignment and endian independent. */
int executable_overlap(uint64_t a, size_t n);
void track_write(CPU *c,uint64_t address,size_t size);
int validate_code(CPU *c);
static inline uint8_t *writable_memory(CPU *c, uint64_t a, size_t n) {
    track_write(c,a,n);
    return memory(c,a,n);
}
static inline uint64_t load(CPU *c, uint64_t a, unsigned bits) {
    uint8_t *p=memory(c,a,bits/8); uint64_t v=0;
    if (p) for(unsigned i=0;i<bits/8;i++) v|=(uint64_t)p[i]<<(8*i);
    return v;
}
static inline void store(CPU *c, uint64_t a, unsigned bits, uint64_t v) {
    uint8_t *p=writable_memory(c,a,bits/8);
    if (p) for(unsigned i=0;i<bits/8;i++) p[i]=(uint8_t)(v>>(8*i));
}
static inline uint64_t reg(CPU *c, unsigned n, unsigned bits, unsigned shift) {
    return (c->r[n]>>shift)&mask(bits);
}
static inline void setreg(CPU *c, unsigned n, unsigned bits, unsigned shift, uint64_t v) {
    if(bits==64 || bits==32) c->r[n]=v&mask(bits);
    else { uint64_t m=mask(bits)<<shift; c->r[n]=(c->r[n]&~m)|((v<<shift)&m); }
}
static inline void result_flags(CPU *c, uint64_t v, unsigned bits) {
    v&=mask(bits); c->zf=v==0; c->sf=(unsigned)(v>>(bits-1));
    unsigned b=(unsigned)(v&255); b^=b>>4; b^=b>>2; b^=b>>1; c->pf=!(b&1);
}
static inline uint64_t alu(CPU *c, uint64_t a, uint64_t b, unsigned bits, int sub) {
    uint64_t m=mask(bits); a&=m; b&=m;
    uint64_t v=(sub ? a-b : a+b)&m;
    c->cf=sub ? a<b : v<a;
    c->of=(unsigned)(((sub ? (a^b) : ~(a^b))&(a^v))>>(bits-1))&1;
    result_flags(c,v,bits); return v;
}
static inline uint64_t logic(CPU *c, uint64_t v, unsigned bits) {
    c->cf=c->of=0; result_flags(c,v,bits); return v&mask(bits);
}
static inline void push(CPU *c, uint64_t v) { c->r[RSP]-=8; store(c,c->r[RSP],64,v); }
static inline uint64_t pop(CPU *c) { uint64_t v=load(c,c->r[RSP],64); c->r[RSP]+=8; return v; }
static inline uint64_t shift(CPU *c, uint64_t a, uint64_t count, unsigned bits, unsigned kind) {
    unsigned n=(unsigned)count&(bits==64 ? 63 : 31); a&=mask(bits);
    if(!n) return a;
    if(n>=bits) { trap(c,"undefined narrow shift count"); return a; }
    uint64_t v;
    if(kind==0) { v=(a<<n)&mask(bits); c->cf=(unsigned)((a>>(bits-n))&1); }
    else {
        v=a>>n; c->cf=(unsigned)((a>>(n-1))&1);
        if(kind==2 && (a>>(bits-1))) v|=mask(bits)^(mask(bits)>>n);
    }
    if(n==1) c->of=kind==0 ? (unsigned)(v>>(bits-1))^c->cf : kind==1 ? (unsigned)(a>>(bits-1)) : 0;
    result_flags(c,v,bits); return v;
}
static inline uint64_t multiply(CPU *c, uint64_t a, uint64_t b, unsigned bits) {
    uint64_t m=mask(bits), sign=UINT64_C(1)<<(bits-1); a&=m; b&=m;
    int negative=!!(a&sign)^!!(b&sign);
    uint64_t aa=(a&sign) ? ((~a+1)&m) : a;
    uint64_t bb=(b&sign) ? ((~b+1)&m) : b;
    uint64_t bound=negative ? sign : sign-1;
    c->cf=c->of=bb && aa>bound/bb;
    return (a*b)&m;
}
uint64_t vector_bits(CPU *c,unsigned regnum,unsigned bits);
void vector_set_bits(CPU *c,unsigned regnum,unsigned bits,uint64_t value);
void scalar_math(CPU *c,unsigned dest,uint64_t source,unsigned bits,unsigned operation);
void scalar_convert(CPU *c,unsigned dest,uint64_t source,unsigned srcbits,unsigned dstbits,unsigned kind);
void wide_integer(CPU *c,uint64_t b,unsigned bits,unsigned operation);
int cpu_init(CPU *c);
void cpu_free(CPU *c);
int translated_step(CPU *c);
const char *import_name(uint64_t pc);
int native_import(CPU *c, const char *name);
int run(CPU *c, uint64_t entry);
#endif
