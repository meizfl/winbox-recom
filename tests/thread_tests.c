#define _POSIX_C_SOURCE 200809L
#include "rt.h"
#include <time.h>
extern void vm_enter(CPU *),vm_leave(CPU *);
static unsigned checks;
#define CHECK(x) do { checks++;if(!(x)) { fprintf(stderr,"Thread test failed at line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static uint64_t api(CPU *c,const char *name,uint64_t a,uint64_t b,uint64_t d,uint64_t e,uint64_t f,uint64_t g) {
    vm_enter(c);uint64_t saved=c->r[RSP];c->r[RSP]=c->stack_base+c->stack_size-264;
    c->r[RCX]=a;c->r[RDX]=b;c->r[R8]=d;c->r[R9]=e;store(c,c->r[RSP],64,0);store(c,c->r[RSP]+40,64,f);store(c,c->r[RSP]+48,64,g);
    CHECK(native_import(c,name));CHECK(!c->failed);uint64_t result=c->r[RAX];c->r[RSP]=saved;vm_leave(c);return result;
}
static uint64_t imported(const char *name) { for(uint64_t a=0x5f5000;a<0x5f9000;a+=8) { const char *s=import_name(a);if(s && !strcmp(s,name)) return a; }return 0; }
static uint32_t wait_for(CPU *c,uint64_t handle) {
    uint64_t output=c->stack_base+128;
    for(unsigned i=0;i<2000;i++) {
        CHECK(api(c,"KERNEL32.dll!GetExitCodeThread",handle,output,0,0,0,0));
        vm_enter(c);uint32_t value=load(c,output,32);vm_leave(c);
        if(value!=259) return value;
        api(c,"KERNEL32.dll!Sleep",1,0,0,0,0,0);
    }
    CHECK(0);return 0;
}
static double seconds(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9; }
int main(void) {
    CPU c;CHECK(cpu_init(&c));uint64_t id_address=c.stack_base+256;
    uint64_t handle=api(&c,"KERNEL32.dll!CreateThread",0,0,0x40156a,42,0,id_address);
    CHECK(handle);CHECK(wait_for(&c,handle)==0);CHECK(load(&c,0x5ec078,8)==42);CHECK(load(&c,id_address,32)>=2);
    CHECK(api(&c,"KERNEL32.dll!CloseHandle",handle,0,0,0,0,0));
    uint64_t slot=api(&c,"KERNEL32.dll!TlsAlloc",0,0,0,0,0,0);CHECK(slot<128);
    CHECK(api(&c,"KERNEL32.dll!TlsSetValue",slot,0x12345678,0,0,0,0));
    uint64_t tls_entry=imported("KERNEL32.dll!TlsGetValue");CHECK(tls_entry);
    handle=api(&c,"KERNEL32.dll!CreateThread",0,0,tls_entry,slot,0,0);CHECK(handle);CHECK(wait_for(&c,handle)==0);
    CHECK(api(&c,"KERNEL32.dll!TlsGetValue",slot,0,0,0,0,0)==0x12345678);
    uint64_t timestamp=guest_alloc(&c,8),main_tm=api(&c,"msvcrt.dll!_gmtime64",timestamp,0,0,0,0,0);
    CHECK(main_tm);uint64_t gmtime_entry=imported("msvcrt.dll!_gmtime64");CHECK(gmtime_entry);
    handle=api(&c,"KERNEL32.dll!CreateThread",0,0,gmtime_entry,timestamp,0,0);CHECK(handle);
    uint64_t worker_tm=wait_for(&c,handle);CHECK(worker_tm && worker_tm!=main_tm);
    CHECK(load(&c,main_tm+20,32)==70 && load(&c,worker_tm+20,32)==70);
    uint64_t main_ip=api(&c,"WS2_32.dll!inet_ntoa",0x0100007f,0,0,0,0,0),inet_entry=imported("WS2_32.dll!inet_ntoa");CHECK(main_ip && inet_entry);handle=api(&c,"KERNEL32.dll!CreateThread",0,0,inet_entry,0x010200c0,0,0);CHECK(handle);uint64_t worker_ip=wait_for(&c,handle);CHECK(worker_ip && worker_ip!=main_ip);CHECK(!strcmp(guest_string(&c,main_ip),"127.0.0.1") && !strcmp(guest_string(&c,worker_ip),"192.0.2.1"));
    CHECK(api(&c,"KERNEL32.dll!GetCurrentThreadId",0,0,0,0,0,0)==1);
    uint64_t id_entry=imported("KERNEL32.dll!GetCurrentThreadId");CHECK(id_entry);
    handle=api(&c,"KERNEL32.dll!CreateThread",0,0,id_entry,0,0,0);CHECK(handle);CHECK(wait_for(&c,handle)>=2);
    uint64_t lock=guest_alloc(&c,40);
    api(&c,"KERNEL32.dll!InitializeCriticalSection",lock,0,0,0,0,0);
    api(&c,"KERNEL32.dll!EnterCriticalSection",lock,0,0,0,0,0);api(&c,"KERNEL32.dll!EnterCriticalSection",lock,0,0,0,0,0);
    CHECK(load(&c,lock+12,32)==2);CHECK(load(&c,lock+16,64)==1);
    api(&c,"KERNEL32.dll!LeaveCriticalSection",lock,0,0,0,0,0);api(&c,"KERNEL32.dll!LeaveCriticalSection",lock,0,0,0,0,0);
    CHECK(load(&c,lock+12,32)==0);CHECK(load(&c,lock+8,32)==UINT32_MAX);
    api(&c,"KERNEL32.dll!DeleteCriticalSection",lock,0,0,0,0,0);
    uint64_t sleep_entry=imported("KERNEL32.dll!Sleep");CHECK(sleep_entry);
    handle=api(&c,"KERNEL32.dll!CreateThread",0,0,sleep_entry,5000,0,0);CHECK(handle);
    api(&c,"KERNEL32.dll!Sleep",25,0,0,0,0,0);
    CHECK(api(&c,"KERNEL32.dll!CloseHandle",handle,0,0,0,0,0));
    double start=seconds();cpu_free(&c);CHECK(seconds()-start<2);
    printf("%u checks passed: translated worker, per-thread TLS/IDs, recursive locks, close and shutdown\n",checks);return 0;
}
