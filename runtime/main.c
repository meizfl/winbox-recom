#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "rt.h"
extern const uint64_t guest_entry;
int main(int argc,char **argv) {
    extern void process_set_arguments(int,char **);
    char **guest_argv=calloc((size_t)argc+1,sizeof(*guest_argv));if(!guest_argv) return 2;int guest_argc=1;guest_argv[0]=argv[0];
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) { printf("Usage: %s [--profile DIR] [--smoke-ms N] [--screenshot FILE]\nExperimental native WinBox 3 runtime.\n",argv[0]);return 0; }
        const char *key=!strcmp(argv[i],"--profile") ? "WINBOX_PROFILE" : !strcmp(argv[i],"--smoke-ms") ? "WINBOX_SMOKE_MS" : !strcmp(argv[i],"--screenshot") ? "WINBOX_SCREENSHOT" : NULL;
        if(!key) { guest_argv[guest_argc++]=argv[i];continue; }
        if(i+1>=argc) { fprintf(stderr,"Missing value for %s\n",argv[i]);free(guest_argv);return 2; }
        setenv(key,argv[++i],1);
    }
    process_set_arguments(guest_argc,guest_argv);free(guest_argv);
    CPU cpu;if(!cpu_init(&cpu)) { fprintf(stderr,"Could not initialize WinBox runtime\n");return 2; }
    const char *limit=getenv("WINBOX_INSTRUCTION_LIMIT");cpu.limit=limit ? strtoull(limit,NULL,10) : UINT64_MAX;
    int ok=run(&cpu,guest_entry);cpu_free(&cpu);return ok ? 0 : 1;
}
