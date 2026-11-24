#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "rt.h"
extern const uint64_t guest_entry;
int main(int argc,char **argv) {
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) { printf("Usage: %s [--profile DIR] [--smoke-ms N] [--screenshot FILE]\nExperimental native WinBox 3 runtime.\n",argv[0]);return 0; }
        const char *key=!strcmp(argv[i],"--profile") ? "WINBOX_PROFILE" : !strcmp(argv[i],"--smoke-ms") ? "WINBOX_SMOKE_MS" : !strcmp(argv[i],"--screenshot") ? "WINBOX_SCREENSHOT" : NULL;
        if(!key || i+1>=argc) { fprintf(stderr,"Invalid argument: %s\n",argv[i]);return 2; }
        setenv(key,argv[++i],1);
    }
    CPU cpu;if(!cpu_init(&cpu)) { fprintf(stderr,"Could not initialize WinBox runtime\n");return 2; }
    const char *limit=getenv("WINBOX_INSTRUCTION_LIMIT");cpu.limit=limit ? strtoull(limit,NULL,10) : UINT64_MAX;
    int ok=run(&cpu,guest_entry);cpu_free(&cpu);return ok ? 0 : 1;
}
