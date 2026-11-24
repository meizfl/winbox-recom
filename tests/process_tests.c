#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "rt.h"
#include "windows_command_line.h"
#include <unistd.h>
#include <limits.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"process check failed at %d: %s\n",__LINE__,#x);return 1; } } while(0)
extern void process_set_arguments(int,char **);
static uint64_t api(CPU *c,const char *name,uint64_t a,uint64_t b) {
    c->r[RCX]=a;c->r[RDX]=b;c->r[R8]=c->r[R9]=0;
    if(!native_import(c,name) || c->failed) return UINT64_MAX;
    return c->r[RAX];
}
int main(int argc,char **argv) {
    if(getenv("WINBOX_PROCESS_TEST_CHILD")) return argc==1 && strstr(argv[0],"native app") ? 0 : 13;
    if(argc>1 && !strcmp(argv[1],"--child")) return argc==5 && !strcmp(argv[2],"white space") && !strcmp(argv[3],"quote\"tail\\") && !*argv[4] ? 0 : 13;
    char *values[]={argv[0],"white space","quote\"tail\\","","\\\\server\\path\\"};
    char *line=windows_quote_arguments(5,values);int count=0;char **parsed=windows_split_command_line(line,&count);CHECK(parsed && count==5);
    for(int i=0;i<count;i++) CHECK(!strcmp(values[i],parsed[i]));windows_free_arguments(parsed);free(line);
    CPU c;CHECK(cpu_init(&c));c.r[RSP]=c.stack_base+c.stack_size-264;
    process_set_arguments(5,values);
    uint64_t wide=api(&c,"KERNEL32.dll!GetCommandLineW",0,0),number=guest_alloc(&c,4);CHECK(wide);
    uint64_t list=api(&c,"SHELL32.dll!CommandLineToArgvW",wide,number);CHECK(list && load(&c,number,32)==5);
    uint64_t second=load(&c,list+8,64);CHECK(load(&c,second,16)=='w' && load(&c,second+10,16)==' ');
    guest_free(&c,list);
    char *child[]={argv[0],"--child","white space","quote\"tail\\",""};line=windows_quote_arguments(5,child);
    uint64_t command=guest_string_alloc(&c,line),info=guest_alloc(&c,24),startup=guest_alloc(&c,104);free(line);store(&c,startup,32,104);
    for(unsigned i=4;i<10;i++) store(&c,c.r[RSP]+40+8*(i-4),64,i==8 ? startup : i==9 ? info : 0);
    for(int directory=0;directory<2;directory++) {
        store(&c,c.r[RSP]+64,64,directory ? guest_string_alloc(&c,"/tmp") : 0);
    CHECK(api(&c,"KERNEL32.dll!CreateProcessA",0,command)==1);
    uint64_t process=load(&c,info,64),thread=load(&c,info+8,64),code=guest_alloc(&c,4);CHECK(process && thread && load(&c,info+16,32));
    CHECK(api(&c,"KERNEL32.dll!WaitForSingleObject",process,5000)==0);
    CHECK(api(&c,"KERNEL32.dll!GetExitCodeProcess",process,code)==1 && load(&c,code,32)==0);
    CHECK(api(&c,"KERNEL32.dll!CloseHandle",process,0)==1);CHECK(api(&c,"KERNEL32.dll!CloseHandle",thread,0)==1);
    }
    char folder[]="/tmp/winbox-process-XXXXXX",target[PATH_MAX],original[PATH_MAX];CHECK(mkdtemp(folder));
    snprintf(target,sizeof target,"%s/native app",folder);
    if(argv[0][0]=='/') snprintf(original,sizeof original,"%s",argv[0]);
    else { CHECK(getcwd(original,sizeof original));size_t length=strlen(original);snprintf(original+length,sizeof original-length,"/%s",argv[0]); }
    CHECK(!symlink(original,target));CHECK(!setenv("WINBOX_PROCESS_TEST_CHILD","1",1));
    CHECK(api(&c,"KERNEL32.dll!CreateProcessA",guest_string_alloc(&c,target),0)==1);
    CHECK(!unsetenv("WINBOX_PROCESS_TEST_CHILD"));
    uint64_t process=load(&c,info,64),thread=load(&c,info+8,64),code=guest_alloc(&c,4);
    CHECK(api(&c,"KERNEL32.dll!WaitForSingleObject",process,5000)==0);
    CHECK(api(&c,"KERNEL32.dll!GetExitCodeProcess",process,code)==1 && load(&c,code,32)==0);
    CHECK(api(&c,"KERNEL32.dll!CloseHandle",process,0)==1);CHECK(api(&c,"KERNEL32.dll!CloseHandle",thread,0)==1);
    unlink(target);rmdir(folder);
    command=guest_string_alloc(&c,"/definitely/not/a/winbox");CHECK(api(&c,"KERNEL32.dll!CreateProcessA",0,command)==0 && c.last_error==2);
    cpu_free(&c);puts("Native process launch, Win32 quoting and guest command line: ok");return 0;
}
