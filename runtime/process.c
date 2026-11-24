#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "rt.h"
#include "windows_command_line.h"
#include <SDL3/SDL.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
extern char **environ;
extern const char *host_executable_path(void);
extern unsigned vm_release(CPU *);
extern void vm_restore(CPU *,unsigned);
typedef struct Process { void *owner;pid_t pid;uint64_t handle;int process_open,thread_open,done;uint32_t code;struct Process *next; } Process;
static Process *processes;
static pthread_mutex_t process_mutex=PTHREAD_MUTEX_INITIALIZER;
static uint64_t next_handle=0x40000000;
static char *command_line,*ansi_command_line;
void process_set_arguments(int argc,char **argv) {
    free(command_line);free(ansi_command_line);command_line=windows_quote_arguments(argc,argv);
    const char *line=command_line ? command_line : "winbox-native";
    char *encoded=SDL_iconv_string("CP1252","UTF-8",line,strlen(line)+1);
    ansi_command_line=strdup(encoded ? encoded : line);SDL_free(encoded);
}
const char *process_command_line_ansi(void) { return ansi_command_line ? ansi_command_line : "winbox-native"; }
static uint64_t guest_wide(CPU *c,const char *text) {
    char *wide=SDL_iconv_string("UTF-16LE","UTF-8",text,strlen(text)+1);if(!wide) return 0;
    size_t bytes=0;do { bytes+=2; } while(wide[bytes-2] || wide[bytes-1]);
    uint64_t address=guest_alloc(c,bytes);uint8_t *dest=writable_memory(c,address,bytes);if(dest) memcpy(dest,wide,bytes);SDL_free(wide);return address;
}
static void collect_process(Process *p) {
    if(p->done && !p->process_open && !p->thread_open) { Process **at=&processes;while(*at && *at!=p) at=&(*at)->next;if(*at) *at=p->next;free(p); }
}
static void *reap_process(void *arg) {
    Process *p=arg;int status=0;pid_t result;do { result=waitpid(p->pid,&status,0); } while(result<0 && errno==EINTR);
    pthread_mutex_lock(&process_mutex);p->code=result<0 ? 1 : WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status);p->done=1;collect_process(p);pthread_mutex_unlock(&process_mutex);return NULL;
}
void process_free(CPU *c) {
    pthread_mutex_lock(&process_mutex);Process *p=processes;
    while(p) { Process *next=p->next;if(p->owner==c->host) { p->process_open=p->thread_open=0;p->owner=NULL;collect_process(p); }p=next; }
    pthread_mutex_unlock(&process_mutex);
}
static Process *find_process(CPU *c,uint64_t handle) {
    for(Process *p=processes;p;p=p->next) if(p->owner==c->host && ((p->process_open && handle==p->handle) || (p->thread_open && handle==p->handle+8))) return p;
    return NULL;
}
/* POSIX spawn has no portable chdir action on macOS 10.14. Prepare everything
 * before fork; the child uses only async-signal-safe calls until execve. */
static int spawn_in_directory(pid_t *pid,const char *executable,char **argv,const char *cwd,int *fds,size_t count) {
    char resolved[PATH_MAX];
    if(strchr(executable,'/')) {
        if(executable[0]=='/') snprintf(resolved,sizeof resolved,"%s",executable);
        else snprintf(resolved,sizeof resolved,"%s/%s",cwd,executable);
    }else {
        const char *path=getenv("PATH");char *paths=strdup(path ? path : "/usr/bin:/bin"),*save=NULL;int found=0;
        if(!paths) return ENOMEM;
        for(char *part=strtok_r(paths,":",&save);part;part=strtok_r(NULL,":",&save)) {
            if(*part=='/') snprintf(resolved,sizeof resolved,"%s/%s",part,executable);
            else snprintf(resolved,sizeof resolved,"%s/%s/%s",cwd,part,executable);
            if(!access(resolved,X_OK)) { found=1;break; }
        }
        free(paths);if(!found) return ENOENT;
    }
    int channel[2];if(pipe(channel)) return errno;
    fcntl(channel[0],F_SETFD,FD_CLOEXEC);fcntl(channel[1],F_SETFD,FD_CLOEXEC);
    *pid=fork();if(*pid<0) { int error=errno;close(channel[0]);close(channel[1]);return error; }
    if(!*pid) {
        close(channel[0]);for(size_t i=0;i<count;i++) if(fds[i]!=channel[1]) close(fds[i]);
        if(!chdir(cwd)) execve(resolved,argv,environ);
        int error=errno;ssize_t ignored=write(channel[1],&error,sizeof error);(void)ignored;_exit(127);
    }
    close(channel[1]);int error=0;ssize_t bytes;do { bytes=read(channel[0],&error,sizeof error); } while(bytes<0 && errno==EINTR);close(channel[0]);
    if(bytes>0) { while(waitpid(*pid,NULL,0)<0 && errno==EINTR) {} return error; }
    return bytes<0 ? errno : 0;
}
#define IS(s) (!strcmp(name,s))
#define A(n) argument(c,n)
#define RET(v) do { *result=(uint64_t)(v);return 1; } while(0)
int process_api(CPU *c,const char *name,uint64_t *result) {
    if(IS("GetCommandLineW")) RET(guest_wide(c,command_line ? command_line : "winbox-native"));
    if(IS("CommandLineToArgvW")) {
        size_t bytes=0;while(bytes<131072 && load(c,A(0)+bytes,16)) bytes+=2;
        uint8_t *input=memory(c,A(0),bytes+2);if(!input || c->failed) RET(0);
        char *line=SDL_iconv_string("UTF-8","UTF-16LE",(char *)input,bytes+2);if(!line) RET(0);
        int argc=0;char **argv=windows_split_command_line(line,&argc);SDL_free(line);if(!argv) RET(0);
        if(!argc) { windows_free_arguments(argv);argv=calloc(2,sizeof(*argv));if(!argv) RET(0);argv[0]=strdup(host_executable_path());argc=1; }
        size_t size=(argc+1)*8;for(int i=0;i<argc;i++) size+=(strlen(argv[i])+1)*2;
        uint64_t address=guest_alloc(c,size),offset=(argc+1)*8;
        for(int i=0;i<argc;i++) {
            char *wide=SDL_iconv_string("UTF-16LE","UTF-8",argv[i],strlen(argv[i])+1);
            if(!wide) { windows_free_arguments(argv);guest_free(c,address);RET(0); }
            size_t length=0;do { length+=2; } while(wide[length-2] || wide[length-1]);
            store(c,address+8*i,64,address+offset);uint8_t *dest=writable_memory(c,address+offset,length);if(dest) memcpy(dest,wide,length);offset+=length;SDL_free(wide);
        }
        store(c,address+8*argc,64,0);if(A(1)) store(c,A(1),32,argc);windows_free_arguments(argv);RET(address);
    }
    if(IS("CreateProcessA")) {
        uint64_t info=A(9);if(!info || !writable_memory(c,info,24)) { c->last_error=87;RET(0); }
        memset(writable_memory(c,info,24),0,24);
        /* A suspended native child cannot run translated ResumeThread yet. */
        if(A(5)&4 || A(6)) { c->last_error=50;RET(0); }
        char *command=A(1) ? guest_string(c,A(1)) : NULL,*application=A(0) ? guest_string(c,A(0)) : NULL;
        if(!command && !application) { c->last_error=87;RET(0); }
        char *utf8=SDL_iconv_string("UTF-8","CP1252",command ? command : application,strlen(command ? command : application)+1);
        int argc=0;char **argv;
        if(!command && application) { argv=calloc(2,sizeof(*argv));if(argv) { argv[0]=strdup(utf8 ? utf8 : application);if(argv[0]) argc=1; } }
        else argv=windows_split_command_line(utf8 ? utf8 : command,&argc);
        SDL_free(utf8);
        if(!argv || !argc) { windows_free_arguments(argv);c->last_error=87;RET(0); }
        const char *executable=application ? application : argv[0],*base=strrchr(executable,'\\');if(!base) base=strrchr(executable,'/');base=base ? base+1 : executable;
        if(!strcmp(base,"winbox.exe") || !strcmp(base,"winbox64.exe") || !strcmp(base,"winbox64.v3.43p10.exe")) executable=host_executable_path();
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
        /* Do not leak live router sockets into a new WinBox session. */
#ifdef __APPLE__
        DIR *fds=opendir("/dev/fd");
#else
        DIR *fds=opendir("/proc/self/fd");
#endif
        int *close_fds=NULL;size_t close_count=0;
        if(fds) { struct dirent *entry;while((entry=readdir(fds))) { char *end;long fd=strtol(entry->d_name,&end,10);if(!*end && fd>2 && fd!=dirfd(fds)) { int *next=realloc(close_fds,(close_count+1)*sizeof(int));if(next) { close_fds=next;close_fds[close_count++]=(int)fd; }posix_spawn_file_actions_addclose(&actions,(int)fd); } }closedir(fds); }
        char *cwd=A(7) ? guest_string(c,A(7)) : NULL;
        Process *p=calloc(1,sizeof(*p));if(!p) { free(close_fds);posix_spawn_file_actions_destroy(&actions);windows_free_arguments(argv);c->last_error=8;RET(0); }
        int error=cwd && *cwd ? spawn_in_directory(&p->pid,executable,argv,cwd,close_fds,close_count) : posix_spawnp(&p->pid,executable,&actions,NULL,argv,environ);free(close_fds);posix_spawn_file_actions_destroy(&actions);windows_free_arguments(argv);
        if(error) { free(p);c->last_error=error==ENOENT ? 2 : error==EACCES ? 5 : error==ENOEXEC ? 193 : 87;RET(0); }
        p->owner=c->host;p->process_open=p->thread_open=1;
        pthread_mutex_lock(&process_mutex);p->handle=(next_handle+=16);p->next=processes;processes=p;
        pthread_t reaper;error=pthread_create(&reaper,NULL,reap_process,p);if(!error) pthread_detach(reaper);
        if(error) { processes=p->next;kill(p->pid,SIGTERM);waitpid(p->pid,NULL,0);free(p);pthread_mutex_unlock(&process_mutex);c->last_error=8;RET(0); }
        store(c,info,64,p->handle);store(c,info+8,64,p->handle+8);store(c,info+16,32,p->pid);store(c,info+20,32,p->pid);pthread_mutex_unlock(&process_mutex);
        fprintf(stderr,"WinBox native child process started\n");RET(1);
    }
    if(IS("CloseHandle") || IS("GetExitCodeProcess") || IS("WaitForSingleObject") || IS("TerminateProcess")) {
        pthread_mutex_lock(&process_mutex);Process *p=find_process(c,A(0));
        if(!p) { pthread_mutex_unlock(&process_mutex);if(IS("TerminateProcess") && A(0)!=UINT64_MAX) { c->last_error=6;RET(0); }return 0; }
        if(IS("CloseHandle")) { if(A(0)==p->handle) p->process_open=0;else p->thread_open=0;collect_process(p);pthread_mutex_unlock(&process_mutex);RET(1); }
        if(IS("GetExitCodeProcess")) { store(c,A(1),32,p->done ? p->code : 259);pthread_mutex_unlock(&process_mutex);RET(1); }
        if(IS("TerminateProcess")) { int ok=p->done || !kill(p->pid,SIGTERM);pthread_mutex_unlock(&process_mutex);RET(ok); }
        uint32_t timeout=A(1),elapsed=0;int done=p->done;pthread_mutex_unlock(&process_mutex);
        while(!done && (timeout==UINT32_MAX || elapsed<timeout)) {
            unsigned depth=vm_release(c);struct timespec delay={0,1000000};nanosleep(&delay,NULL);vm_restore(c,depth);elapsed++;
            pthread_mutex_lock(&process_mutex);p=find_process(c,A(0));done=p && p->done;pthread_mutex_unlock(&process_mutex);if(!p) { c->last_error=6;RET(UINT32_MAX); }
        }
        RET(done ? 0 : 258);
    }
    return 0;
}
