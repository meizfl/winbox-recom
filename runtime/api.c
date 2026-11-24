#include "rt.h"
extern int threads_api(CPU *,const char *,uint64_t *);
extern int network_api(CPU *,const char *,uint64_t *);
extern int process_api(CPU *,const char *,uint64_t *);
/* Only implemented contracts are dispatched. Every other import is a trap. */
int native_import(CPU *c, const char *name) {
    if(strcmp(name,"KERNEL32.dll!GetLastError")==0) {
        c->r[RAX]=c->last_error; c->pc=pop(c); return 1;
    }
    if(strcmp(name,"KERNEL32.dll!SetLastError")==0) {
        c->last_error=(uint32_t)c->r[RCX]; c->pc=pop(c); return 1;
    }
    const char *symbol=strchr(name,'!');
    if(symbol) {
        uint64_t result=0;
        if(getenv("WINBOX_TRACE")) fprintf(stderr,"API %s ret=%llx [%llx,%llx,%llx,%llx]\n",name,(unsigned long long)load(c,c->r[RSP],64),(unsigned long long)c->r[RCX],(unsigned long long)c->r[RDX],(unsigned long long)c->r[R8],(unsigned long long)c->r[R9]);
        if(threads_api(c,symbol+1,&result) || process_api(c,symbol+1,&result) || network_api(c,symbol+1,&result) || host_api(c,symbol+1,&result) || crt_api(c,symbol+1,&result) || ui_api(c,symbol+1,&result)) {
            c->r[RAX]=result; if(c->pc) c->pc=pop(c);return 1;
        }
    }
    return 0;
}
