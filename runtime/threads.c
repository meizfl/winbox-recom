#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "rt.h"
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

typedef struct Worker Worker;
typedef struct Critical { uint64_t address,owner;unsigned recursion;pthread_mutex_t mutex;struct Critical *next; } Critical;
typedef struct { pthread_mutex_t vm;CPU *main;Worker *workers;Critical *critical;uint64_t next_id;int stopping,failed;uint64_t main_tls[128],crt_buffers[3]; } Runtime;
struct Worker { CPU cpu;pthread_t native;uint64_t id,handle,entry,param,stack,teb;uint64_t tls[128],crt_buffers[3];uint32_t exit_code;int done,closed;Worker *next; };
extern void *thread_runtime(CPU *);
extern void set_thread_runtime(CPU *,void *);
static _Thread_local unsigned lock_depth;
static Runtime *runtime(CPU *c) { return thread_runtime(c); }
static int recursive_mutex(pthread_mutex_t *mutex) { pthread_mutexattr_t attr;if(pthread_mutexattr_init(&attr)) return 0;pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);int rc=pthread_mutex_init(mutex,&attr);pthread_mutexattr_destroy(&attr);return !rc; }
int threads_init(CPU *c) { Runtime *r=calloc(1,sizeof *r);if(!r) return 0;if(!recursive_mutex(&r->vm)) { free(r);return 0; }r->main=c;r->next_id=2;set_thread_runtime(c,r);return 1; }
uint8_t *main_stack_memory(CPU *c,uint64_t a,size_t n) { Runtime *r=runtime(c);if(!r || c==r->main) return NULL;CPU *m=r->main;if(a>=m->stack_base && a-m->stack_base<=m->stack_size && n<=m->stack_size-(a-m->stack_base)) return m->stack+(a-m->stack_base);return NULL; }
void vm_enter(CPU *c) { Runtime *r=runtime(c);if(r) { pthread_mutex_lock(&r->vm);lock_depth++; } }
void vm_leave(CPU *c) { Runtime *r=runtime(c);if(r && lock_depth) { lock_depth--;pthread_mutex_unlock(&r->vm); } }
unsigned vm_release(CPU *c) { unsigned depth=lock_depth;while(lock_depth) vm_leave(c);return depth; }
void vm_restore(CPU *c,unsigned depth) { while(depth--) vm_enter(c); }
int vm_stopping(CPU *c) { Runtime *r=runtime(c);return r && (r->stopping || r->failed); }
int vm_failed(CPU *c) { Runtime *r=runtime(c);return r && r->failed; }
static Worker *current(CPU *c) { Runtime *r=runtime(c);if(!r || c==r->main) return NULL;for(Worker *w=r->workers;w;w=w->next) if(&w->cpu==c) return w;return NULL; }
uint64_t thread_crt_buffer(CPU *c,unsigned index,size_t size) {
    if(index>=3) return 0;Worker *w=current(c);uint64_t *slot=w ? &w->crt_buffers[index] : &runtime(c)->crt_buffers[index];
    if(!*slot) *slot=guest_alloc(c,size);return *slot;
}
uint64_t thread_id(CPU *c) { Worker *w=current(c);return w ? w->id : 1; }
static Worker *worker_handle(CPU *c,uint64_t handle) { Runtime *r=runtime(c);for(Worker *w=r->workers;w;w=w->next) if(w->handle==handle && !w->closed) return w;return NULL; }
static void *worker_main(void *arg) { Worker *w=arg;CPU *c=&w->cpu;int ok=run(c,w->entry);vm_enter(c);w->exit_code=ok ? (uint32_t)c->r[RAX] : 1;w->done=1;if(!ok) runtime(c)->failed=1;if(getenv("WINBOX_TRACE")) fprintf(stderr,"Guest thread %llu finished (%u)\n",(unsigned long long)w->id,w->exit_code);vm_leave(c);return NULL; }
void threads_free(CPU *c) {
    Runtime *r=runtime(c);if(!r) return;vm_enter(c);r->stopping=1;vm_leave(c);
    for(Worker *w=r->workers;w;w=w->next) pthread_join(w->native,NULL);
    Worker *w=r->workers;while(w) { Worker *next=w->next;free(w);w=next; }
    Critical *p=r->critical;while(p) { Critical *next=p->next;pthread_mutex_destroy(&p->mutex);free(p);p=next; }
    pthread_mutex_destroy(&r->vm);set_thread_runtime(c,NULL);free(r);
}
static Critical *critical(CPU *c,uint64_t address,int create) { Runtime *r=runtime(c);for(Critical *p=r->critical;p;p=p->next) if(p->address==address) return p;if(!create) return NULL;Critical *p=calloc(1,sizeof *p);if(!p || !recursive_mutex(&p->mutex)) { free(p);return NULL; }p->address=address;p->next=r->critical;r->critical=p;return p; }
#define IS(s) (!strcmp(name,s))
#define A(n) argument(c,n)
#define RET(v) do { *result=(uint64_t)(v);return 1; } while(0)
int threads_api(CPU *c,const char *name,uint64_t *result) {
    Runtime *r=runtime(c);if(!r) return 0;
    if(IS("CreateThread")) {
        if(A(4)) { c->last_error=50;RET(0); }
        size_t size=A(1) ? A(1) : 1024*1024;if(size<65536) size=65536;if(size>64*1024*1024) { c->last_error=8;RET(0); }size=(size+4095)&~(size_t)4095;
        Worker *w=calloc(1,sizeof *w);if(!w) RET(0);w->id=r->next_id++;w->handle=0x30000000+w->id*16;w->entry=A(2);w->param=A(3);
        w->stack=guest_alloc(c,size);w->teb=guest_alloc(c,4096);if(!w->stack || !w->teb) { guest_free(c,w->stack);guest_free(c,w->teb);free(w);RET(0); }
        CPU *child=&w->cpu;child->base=c->base;child->image=c->image;child->image_size=c->image_size;child->host=c->host;child->stack_base=w->stack;child->stack=memory(c,w->stack,size);child->stack_size=size;child->gs_base=w->teb;child->limit=c->limit;child->r[RSP]=w->stack+size-8;child->r[RCX]=w->param;
        store(c,w->teb+8,64,w->stack+size);store(c,w->teb+16,64,w->stack);store(c,w->teb+48,64,w->teb);store(c,w->teb+64,64,getpid());store(c,w->teb+72,64,w->id);
        w->next=r->workers;r->workers=w;
        int rc=pthread_create(&w->native,NULL,worker_main,w);if(rc) { r->workers=w->next;guest_free(c,w->stack);guest_free(c,w->teb);free(w);c->last_error=8;RET(0); }
        if(A(5)) store(c,A(5),32,w->id);
        if(getenv("WINBOX_TRACE")) fprintf(stderr,"Guest thread %llu started at 0x%llx\n",(unsigned long long)w->id,(unsigned long long)w->entry);
        RET(w->handle);
    }
    if(IS("GetCurrentThreadId")) RET(thread_id(c));
    if(IS("GetExitCodeThread")) { Worker *w=worker_handle(c,A(0));if(!w) { c->last_error=6;RET(0); }store(c,A(1),32,w->done ? w->exit_code : 259);RET(1); }
    if(IS("CloseHandle")) { Worker *w=worker_handle(c,A(0));if(!w) return 0;w->closed=1;RET(1); }
    if(IS("TlsGetValue") || IS("TlsSetValue")) { if(A(0)>=128) { c->last_error=87;RET(0); }Worker *w=current(c);uint64_t *slots=w ? w->tls : r->main_tls;if(IS("TlsSetValue")) { slots[A(0)]=A(1);RET(1); }c->last_error=0;RET(slots[A(0)]); }
    if(IS("InitializeCriticalSection") || IS("InitializeCriticalSectionAndSpinCount")) { Critical *p=critical(c,A(0),1);if(!p) { c->last_error=8;RET(0); }uint8_t *bytes=writable_memory(c,A(0),40);if(bytes) memset(bytes,0,40);store(c,A(0)+8,32,UINT32_MAX);RET(1); }
    if(IS("EnterCriticalSection")) { Critical *p=critical(c,A(0),1);if(!p) RET(0);while(pthread_mutex_trylock(&p->mutex)) { unsigned depth=vm_release(c);struct timespec pause={0,1000000};nanosleep(&pause,NULL);vm_restore(c,depth);if(vm_stopping(c)) RET(0); }p->owner=thread_id(c);p->recursion++;store(c,p->address+8,32,p->recursion-1);store(c,p->address+12,32,p->recursion);store(c,p->address+16,64,p->owner);RET(0); }
    if(IS("LeaveCriticalSection")) { Critical *p=critical(c,A(0),0);if(!p || p->owner!=thread_id(c) || !p->recursion) { trap(c,"critical section ownership violation");RET(0); }p->recursion--;if(!p->recursion) p->owner=0;store(c,p->address+8,32,p->recursion-1);store(c,p->address+12,32,p->recursion);store(c,p->address+16,64,p->owner);pthread_mutex_unlock(&p->mutex);RET(0); }
    if(IS("DeleteCriticalSection")) { Critical *p=critical(c,A(0),0);if(p && p->recursion) { trap(c,"deleting an owned critical section"); }RET(0); }
    if(IS("Sleep")) { uint64_t remaining=A(0);unsigned depth=vm_release(c);do { uint64_t chunk=remaining>20 ? 20 : remaining;struct timespec t={chunk/1000,(chunk%1000)*1000000};nanosleep(&t,NULL);remaining-=chunk;vm_restore(c,depth);int stop=vm_stopping(c);vm_release(c);if(stop) break; } while(remaining);vm_restore(c,depth);RET(0); }
    return 0;
}
