#include "rt.h"
#include <inttypes.h>
#if defined(__linux__) && defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <sys/mman.h>
typedef unsigned char (__attribute__((ms_abi)) *Oracle)(uint64_t, const void *, const void *);
static Oracle oracle32, oracle64;
static void *oracle_page;
static void prepare_oracle(CPU *c) {
    oracle_page=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(oracle_page==MAP_FAILED) { perror("mmap"); exit(1); }
    memcpy(oracle_page,c->image+0x1560,10);
    memcpy((uint8_t *)oracle_page+32,c->image+0x15d0,9);
    if(mprotect(oracle_page,4096,PROT_READ|PROT_EXEC)) { perror("mprotect"); exit(1); }
    oracle64=(Oracle)oracle_page; oracle32=(Oracle)((uint8_t *)oracle_page+32);
}
#else
static void prepare_oracle(CPU *c) { (void)c; }
#endif
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"check failed at %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static void compare(CPU *c, uint64_t a, uint64_t b, unsigned bits) {
    uint64_t p=c->stack_base+128, q=p+16;
    store(c,p,bits,a); store(c,q,bits,b);
    c->r[RDX]=p; c->r[R8]=q; c->r[RAX]=UINT64_C(0xdeadbeef00000000);
    uint64_t rsp=c->r[RSP];
    CHECK(run(c,bits==64 ? 0x401560 : 0x4015d0));
    CHECK((c->r[RAX]&255)==((a&mask(bits))<(b&mask(bits))));
#if defined(__linux__) && defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    CHECK((c->r[RAX]&255)==(bits==64 ? oracle64(0,&a,&b) : oracle32(0,&a,&b)));
#endif
    CHECK(c->r[RSP]==rsp+8);
    c->r[RSP]=rsp;
}
int main(void) {
    CPU c; CHECK(cpu_init(&c)); prepare_oracle(&c);
    CHECK(executable_overlap(0x401560,1));
    CHECK(executable_overlap(0x40155f,2));
    CHECK(!executable_overlap(0x638800,1));
    CHECK(!executable_overlap(c.stack_base,8));
    uint64_t values[]={0,1,127,128,255,UINT32_MAX,UINT64_C(0x8000000000000000),UINT64_MAX};
    for(unsigned bits=32;bits<=64;bits+=32)
        for(unsigned i=0;i<sizeof(values)/sizeof(*values);i++)
            for(unsigned j=0;j<sizeof(values)/sizeof(*values);j++) compare(&c,values[i],values[j],bits);
    uint64_t seed=42;
    for(unsigned i=0;i<10000;i++) {
        seed=seed*UINT64_C(6364136223846793005)+1; uint64_t a=seed;
        seed=seed*UINT64_C(6364136223846793005)+1;
        compare(&c,a,seed,32); compare(&c,a,seed,64);
    }
    for(unsigned i=0;i<256;i++) {
        uint64_t rsp=c.r[RSP]; c.r[RCX]=i;
        CHECK(run(&c,0x40156a)); c.r[RSP]=rsp;
        c.r[RAX]=UINT64_MAX;
        CHECK(run(&c,0x401571)); c.r[RSP]=rsp;
        CHECK((c.r[RAX]&255)==i);
        CHECK((c.r[RAX]>>8)==(UINT64_MAX>>8));
    }
    uint64_t rsp=c.r[RSP];
    c.r[RCX]=0; c.r[RDX]=UINT64_MAX;
    CHECK(native_import(&c,"KERNEL32.dll!GetTempPathA"));
    CHECK(c.r[RAX]>=2 && c.r[RAX]<=261); c.r[RSP]=rsp;
    uint64_t required=c.r[RAX];
    c.r[RCX]=required; c.r[RDX]=c.stack_base+512;
    CHECK(native_import(&c,"KERNEL32.dll!GetTempPathA")); c.r[RSP]=rsp;
    CHECK(c.r[RAX]==required-1);
    CHECK(load(&c,c.stack_base+512+required-1,8)==0);
    c.r[RCX]=1234;
    CHECK(native_import(&c,"KERNEL32.dll!SetLastError")); c.r[RSP]=rsp;
    CHECK(native_import(&c,"KERNEL32.dll!GetLastError")); c.r[RSP]=rsp;
    CHECK(c.r[RAX]==1234);
    c.r[RCX]=c.stack_base+512;c.r[RDX]=16;c.r[R8]=4;c.r[R9]=c.stack_base+1024;
    CHECK(native_import(&c,"KERNEL32.dll!VirtualProtect"));c.r[RSP]=rsp;
    CHECK(c.r[RAX]==1 && load(&c,c.stack_base+1024,32)==4);
    uint64_t allocation=guest_alloc(&c,32);
    CHECK(allocation && !(allocation&15));
    CHECK(load(&c,allocation-8,64)==0);
    store(&c,allocation,64,UINT64_C(0x123456789abcdef0));
    CHECK(load(&c,allocation,64)==UINT64_C(0x123456789abcdef0));
    guest_free(&c,allocation);
#if defined(__linux__) && defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    munmap(oracle_page,4096);
#endif
#if defined(__SIZEOF_INT128__)
    for(unsigned bits=32;bits<=64;bits+=32) {
        for(unsigned i=0;i<10000;i++) {
            seed=seed*UINT64_C(6364136223846793005)+1; uint64_t a=seed;
            seed=seed*UINT64_C(6364136223846793005)+1; uint64_t b=seed;
            __int128 aa=bits==64 ? (__int128)(int64_t)a : (__int128)(int32_t)a;
            __int128 bb=bits==64 ? (__int128)(int64_t)b : (__int128)(int32_t)b;
            __int128 product=aa*bb, bound=(__int128)1<<(bits-1);
            uint64_t v=multiply(&c,a,b,bits);
            CHECK(v==((uint64_t)product&mask(bits)));
            CHECK(c.cf==(product < -bound || product >= bound));
            CHECK(c.of==c.cf);
            unsigned n=1+i%(bits-1);
            uint64_t expected=(a&mask(bits))>>n;
            CHECK(shift(&c,a,n,bits,1)==expected);
            CHECK(c.cf==(((a&mask(bits))>>(n-1))&1));
            CHECK(shift(&c,a,n,bits,0)==((a<<n)&mask(bits)));
            CHECK(shift(&c,a,n,bits,2)==((uint64_t)(aa>>n)&mask(bits)));
        }
    }
#endif
    for(unsigned i=0;i<10000;i++) {
        seed=seed*UINT64_C(6364136223846793005)+1;
        if(i==0) seed=(UINT64_C(1)<<62)+(UINT64_C(1)<<38)+1;
        scalar_convert(&c,0,seed,64,32,0);
        float expected_float=(float)(int64_t)seed;uint32_t float_bits;memcpy(&float_bits,&expected_float,4);
        CHECK(vector_bits(&c,0,32)==float_bits);
        scalar_convert(&c,0,seed,64,64,0);
        double expected_double=(double)(int64_t)seed;uint64_t double_bits;memcpy(&double_bits,&expected_double,8);
        CHECK(vector_bits(&c,0,64)==double_bits);
    }
    CPU altered;CHECK(cpu_init(&altered));
    store(&altered,0x401560,8,0xcc);
    CHECK(!run(&altered,0x401560));
    CHECK(altered.failed);cpu_free(&altered);
    cpu_free(&c);
    printf("%u checks passed (lifted functions, native oracle and runtime helpers)\n",checks);
    return 0;
}
