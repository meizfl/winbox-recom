#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "rt.h"
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>
#include <glob.h>
#include <iconv.h>
#include <strings.h>
#include <dirent.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

typedef struct Region { uint64_t base; size_t size,requested; uint64_t released; uint8_t *data; struct Region *next,*free_next; } Region;
typedef struct { Region *regions,*free_regions;Region **pages; uint64_t next,allocation_epoch; FILE *files[256]; uint64_t tls[128]; unsigned tls_count; char temp[256]; uint64_t filter;glob_t finds[32];unsigned find_index[32];int find_used[32];char *atoms[256];unsigned atom_count;uint64_t bitmap_sources[256],bitmap_entries[256];unsigned bitmap_count;uint64_t iob;uint8_t *dirty;void *runtime; } Host;
void *thread_runtime(CPU *c) { Host *h=c->host;return h ? h->runtime : NULL; }
void set_thread_runtime(CPU *c,void *value) { Host *h=c->host;h->runtime=value; }
void track_write(CPU *c,uint64_t a,size_t n) {
    Host *h=c->host;if(!h || !n || a<c->base || a-c->base>=c->image_size || n>c->image_size-(a-c->base)) return;
    size_t first=(a-c->base)/4096,last=(a-c->base+n-1)/4096;for(size_t i=first;i<=last;i++) h->dirty[i]=1;
}
int validate_code(CPU *c) {
    Host *h=c->host;uint64_t offset=c->pc-c->base;
    if(offset>=c->image_size || !h) return 1;
    if(!h->dirty[offset/4096] && (offset+15>=c->image_size || !h->dirty[(offset+15)/4096])) return 1;
    extern int instruction_length(uint64_t);
    extern const uint8_t guest_image[];
    int size=instruction_length(c->pc);
    if(size && (offset+size>c->image_size || memcmp(c->image+offset,guest_image+offset,size))) { trap(c,"modified instruction bytes require retranslation");return 0; }
    return 1;
}
uint8_t *memory(CPU *c,uint64_t a,size_t n) {
    if(a>=c->base && a-c->base<=c->image_size && n<=c->image_size-(a-c->base)) return c->image+(a-c->base);
    if(a>=c->stack_base && a-c->stack_base<=c->stack_size && n<=c->stack_size-(a-c->stack_base)) return c->stack+(a-c->stack_base);
    Host *h=c->host;
    extern uint8_t *main_stack_memory(CPU *,uint64_t,size_t);
    uint8_t *shared_stack=main_stack_memory(c,a,n);if(shared_stack) return shared_stack;
    if(h && a>=0x10000000 && a<0xf0000000) { Region *r=h->pages[(a-0x10000000)/4096];if(r && r->data && a>=r->base-16 && a-(r->base-16)<=r->size+16 && n<=r->size+16-(a-(r->base-16))) return r->data+(a-(r->base-16)); }
    char reason[128]; snprintf(reason,sizeof reason,"unmapped guest memory 0x%llx (%zu bytes)",(unsigned long long)a,n); trap(c,reason); return NULL;
}
uint64_t guest_alloc(CPU *c,size_t size) {
    Host *h=c->host;
    if(size>512*1024*1024) { c->last_error=8; return 0; }
    if(!size) size=1;
    /* HeapFree releases ownership, not the committed virtual pages. Keep freed
       storage mapped and recycle it after a short quarantine. This matches the
       heap behaviour relied on by this binary's disconnect callback. */
    h->allocation_epoch++;
    for(Region **link=&h->free_regions;*link;link=&(*link)->free_next) {
        Region *r=*link;
        if(h->allocation_epoch-r->released>=64 && r->size>=size && r->size-size<=4096) {
            *link=r->free_next;r->free_next=NULL;r->released=0;r->requested=size;
            memset(r->data,0,r->size+16);return r->base;
        }
    }
    /* This supplied p10 binary loads a settings-path pointer through ECX at
       0x65b119. Keep guest allocations representable by its 32-bit patch. */
    if(h->next+((size+16+4095)&~UINT64_C(4095))>UINT64_C(0xf0000000)) { c->last_error=8;return 0; }
    Region *r=calloc(1,sizeof *r); if(!r) return 0;
    r->data=calloc(1,size+16); if(!r->data) { free(r); return 0; }
    r->base=h->next+16; r->size=r->requested=size; h->next+=(size+16+4095)&~UINT64_C(4095);
    for(uint64_t page=(r->base-16-0x10000000)/4096;page<(h->next-0x10000000)/4096;page++) h->pages[page]=r;
    r->next=h->regions; h->regions=r; return r->base;
}
size_t guest_size(CPU *c,uint64_t a) { Host *h=c->host;if(a<0x10000000 || a>=0xf0000000) return 0;Region *r=h->pages[(a-0x10000000)/4096];return r && r->base==a && !r->released ? r->requested : 0; }
void guest_free(CPU *c,uint64_t a) {
    Host *h=c->host; if(!h || !a) return;
    if(a>=0x10000000 && a<0xf0000000) { Region *r=h->pages[(a-0x10000000)/4096];if(r && r->base==a && !r->released) { r->released=++h->allocation_epoch;r->free_next=h->free_regions;h->free_regions=r; } }
}
char *guest_string(CPU *c,uint64_t a) {
    if(!a) return NULL;
    uint8_t *p=memory(c,a,1);if(!p) return NULL;size_t available=0;
    if(a>=c->base && a-c->base<c->image_size) available=c->image_size-(a-c->base);
    else if(a>=c->stack_base && a-c->stack_base<c->stack_size) available=c->stack_size-(a-c->stack_base);
    else if(a>=0x10000000 && a<0xf0000000) { Host *h=c->host;Region *r=h->pages[(a-0x10000000)/4096];if(r) available=r->size+16-(a-(r->base-16)); }
    if(available>1024*1024) available=1024*1024;
    if(memchr(p,0,available)) return (char *)p;
    trap(c,"unterminated guest string");return NULL;
}
uint64_t guest_string_alloc(CPU *c,const char *s) {
    size_t n=strlen(s)+1; uint64_t a=guest_alloc(c,n); uint8_t *p=memory(c,a,n);if(p) memcpy(p,s,n);return a;
}
uint64_t argument(CPU *c,unsigned n) { const unsigned r[]={RCX,RDX,R8,R9}; return n<4 ? c->r[r[n]] : load(c,c->r[RSP]+40+8*(n-4),64); }
static const char *executable_path(void);
#ifdef WINBOX_PORTABLE_FONTS
#include <fontconfig/fontconfig.h>
#include "portable_fonts.h"
static void portable_fonts_init(void) {
    static int initialized;if(initialized) return;initialized=1;
    if(getenv("FONTCONFIG_FILE")) return;
    FcConfig *config=FcConfigCreate();
    if(config && FcConfigParseAndLoadFromMemory(config,(const FcChar8 *)portable_font_config,FcTrue) && FcConfigBuildFonts(config)) FcConfigSetCurrent(config);
    else fprintf(stderr,"WinBox could not initialize portable font configuration\n");
    if(config) FcConfigDestroy(config);
}
#endif
int host_init(CPU *c) {
#ifdef __APPLE__
    if(!getenv("FONTCONFIG_FILE")) { char config[PATH_MAX];snprintf(config,sizeof config,"%s",executable_path());char *slash=strrchr(config,'/');if(slash) { *slash=0;size_t length=strlen(config);snprintf(config+length,sizeof config-length,"/../Resources/fonts.conf");if(!access(config,R_OK)) setenv("FONTCONFIG_FILE",config,1); } }
#endif
#ifdef WINBOX_PORTABLE_FONTS
    portable_fonts_init();
#endif
    Host *h=calloc(1,sizeof *h); if(!h) return 0;c->host=h;h->pages=calloc(0xe0000000/4096,sizeof *h->pages);if(!h->pages) return 0;h->dirty=calloc(1,(c->image_size+4095)/4096);if(!h->dirty) return 0;h->next=UINT64_C(0x10000000);
    c->gs_base=guest_alloc(c,4096);
    store(c,c->gs_base+8,64,c->stack_base+c->stack_size); store(c,c->gs_base+16,64,c->stack_base); store(c,c->gs_base+48,64,c->gs_base);
    snprintf(h->temp,sizeof h->temp,"/tmp/winbox-native-XXXXXX"); if(!mkdtemp(h->temp)) return 0;
    extern int threads_init(CPU *);return threads_init(c);
}
void host_free(CPU *c) {
    Host *h=c->host; if(!h) return;
    extern void threads_free(CPU *);threads_free(c);
    extern void network_free(void);network_free();
    extern void process_free(CPU *);process_free(c);
    for(unsigned i=0;i<256;i++) if(h->files[i]) fclose(h->files[i]);
    for(unsigned i=0;i<h->atom_count;i++) free(h->atoms[i]);
    for(unsigned i=0;i<32;i++) if(h->find_used[i]) globfree(&h->finds[i]);
    Region *r=h->regions;while(r) { Region *next=r->next;free(r->data);free(r);r=next; }
    /* Only bootstrap's known PNG files live in this private temporary directory. */
    const char *names[]={"icons16.png","icons24.png","icons32.png"};
    for(unsigned i=0;i<3;i++) { char p[300];snprintf(p,sizeof p,"%s/%s",h->temp,names[i]);unlink(p); }
    DIR *dir=opendir(h->temp);if(dir) { struct dirent *entry;while((entry=readdir(dir))) if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) { char p[600];snprintf(p,sizeof p,"%s/%s",h->temp,entry->d_name);unlink(p); }closedir(dir); }
    rmdir(h->temp);free(h->dirty);free(h->pages);free(h);c->host=NULL;
}
FILE *host_file(CPU *c,uint64_t handle) { Host *h=c->host;if(handle>=0x1000 && handle<0x1100) return h->files[handle-0x1000];if(h->iob && handle>=h->iob && handle<h->iob+144 && (handle-h->iob)%48==0) { FILE *standard[]={stdin,stdout,stderr};return standard[(handle-h->iob)/48]; }return NULL; }
static const char *executable_path(void) {
    static char path[PATH_MAX];if(!*path) {
#ifdef __APPLE__
        uint32_t size=sizeof path;if(_NSGetExecutablePath(path,&size)) strcpy(path,"winbox-native");
#else
        ssize_t n=readlink("/proc/self/exe",path,sizeof path-1);if(n<0) strcpy(path,"winbox-native");else path[n]=0;
#endif
    }return path;
}
static int mkdirs(char *p);
const char *host_executable_path(void) { return executable_path(); }
static const char *profile_path(void) {
    static char path[PATH_MAX];if(!*path) { const char *env=getenv("WINBOX_PROFILE"),*home=getenv("HOME");if(env && *env) snprintf(path,sizeof path,"%s",env);else {
#ifdef __APPLE__
        snprintf(path,sizeof path,"%s/Library/Application Support/WinBox Recom",home && *home ? home : ".");
#else
        const char *config=getenv("XDG_CONFIG_HOME");if(config && *config) snprintf(path,sizeof path,"%s/winbox-recom",config);else snprintf(path,sizeof path,"%s/.config/winbox-recom",home && *home ? home : ".");
#endif
    }mkdirs(path); }return path;
}
const char *host_profile_path(void) { return profile_path(); }
static char *host_path(CPU *c,uint64_t address,char *buffer) {
    char *p=guest_string(c,address);if(!p) return NULL;size_t offset=0;
    if(isalpha((unsigned char)p[0]) && p[1]==':') { offset=(size_t)snprintf(buffer,PATH_MAX,"%s",profile_path());p+=2; }
    if(strlen(p)+offset>=PATH_MAX) { c->last_error=206;return NULL; }
    strcpy(buffer+offset,p);for(char *q=buffer;*q;q++) if(*q=='\\') *q='/';return buffer;
}
static int mkdirs(char *p) { for(char *q=p+1;*q;q++) if(*q=='/') { *q=0;int rc=mkdir(p,0700);*q='/';if(rc && errno!=EEXIST) return -1; }int rc=mkdir(p,0700);return rc && errno!=EEXIST ? -1 : 0; }
static int find_data(CPU *c,const char *path,uint64_t address) {
    struct stat st;if(stat(path,&st)) return 0;uint8_t *d=writable_memory(c,address,320);if(!d) return 0;memset(d,0,320);store(c,address,32,S_ISDIR(st.st_mode) ? 0x10 : 0x80);
    store(c,address+4,64,((uint64_t)st.st_ctime+11644473600ULL)*10000000);store(c,address+12,64,((uint64_t)st.st_atime+11644473600ULL)*10000000);store(c,address+20,64,((uint64_t)st.st_mtime+11644473600ULL)*10000000);store(c,address+28,32,(uint64_t)st.st_size>>32);store(c,address+32,32,st.st_size);const char *name=strrchr(path,'/');snprintf((char *)d+44,260,"%s",name ? name+1 : path);return 1;
}
static uint32_t resource_entry(CPU *c,uint64_t root,uint32_t directory,uint64_t key,int first) {
    uint64_t table=root+directory;unsigned count=(unsigned)load(c,table+12,16)+(unsigned)load(c,table+14,16);
    char *wanted=key>=65536 ? guest_string(c,key) : NULL;
    for(unsigned i=0;i<count && !c->failed;i++) { uint32_t name=load(c,table+16+8*i,32),value=load(c,table+20+8*i,32);if(first) return value;
        if(!(name&0x80000000) && key<65536 && name==(uint32_t)key) return value;
        if((name&0x80000000) && wanted) { uint64_t p=root+(name&0x7fffffff);unsigned n=load(c,p,16);if(strlen(wanted)==n) { unsigned j=0;for(;j<n;j++) if(load(c,p+2+2*j,16)!=(unsigned char)wanted[j]) break;if(j==n) return value; } }
    }return 0;
}
/* Server UI metadata puts the Switch group icon on conditional Settings
 * entries. Keep that group decoration when those entries are filtered out.
 * Adapt only a private read stream; cached bytes and their CRC stay intact. */
static FILE *definition_stream(FILE *source,const char *path,const char *mode) {
    size_t length=strlen(path);if(length<11 || strcmp(path+length-10,"roteros.jg") || strcmp(mode,"r")) return source;
    long end;if(fseek(source,0,SEEK_END) || (end=ftell(source))<0 || end>16*1024*1024) { rewind(source);return source; }rewind(source);
    char *data=malloc((size_t)end+1);if(!data) return source;size_t n=fread(data,1,end,source);data[n]=0;rewind(source);
    const char *key="group:'Switch',";size_t key_size=strlen(key);if(!strstr(data,key)) { free(data);return source; }
    FILE *adapted=tmpfile();if(!adapted) { free(data);return source; }
    const char *start=data,*match;while((match=strstr(start,key))) { fwrite(start,1,match-start+key_size,adapted);fputs("groupIcon:24,",adapted);start=match+key_size; }fwrite(start,1,data+n-start,adapted);free(data);
    if(ferror(adapted) || fflush(adapted)) { fclose(adapted);return source; }rewind(adapted);fclose(source);fprintf(stderr,"WinBox inherited Switch group icon in UI definitions\n");return adapted;
}
static uint64_t ticks(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
#define IS(s) (!strcmp(name,s))
#define A(n) argument(c,n)
#define RET(v) do { *result=(uint64_t)(v);return 1; } while(0)
/* The original loader only accepts 4/8-bit indexed DIBs. Expand monochrome PE
 * resources at the resource boundary, preserving their palette and row order. */
static uint64_t bitmap_resource(CPU *c,uint64_t entry) {
    Host *h=c->host;for(unsigned i=0;i<h->bitmap_count;i++) if(h->bitmap_sources[i]==entry) return h->bitmap_entries[i];
    uint64_t data=c->base+load(c,entry,32);size_t size=load(c,entry+4,32);if(size<40) return entry;
    uint32_t header=load(c,data,32);int32_t width=load(c,data+4,32),height=load(c,data+8,32);
    if(header<40 || header>size || load(c,data+12,16)!=1 || load(c,data+14,16)!=1 || load(c,data+16,32)!=0 || width<=0 || width>8192 || !height || height<-8192 || height>8192) return entry;
    unsigned colors=load(c,data+32,32);if(!colors) colors=2;if(colors!=2) return entry;
    size_t rows=height<0 ? -height : height,source_stride=((size_t)width+31)/32*4,dest_stride=((size_t)width+3)&~(size_t)3;
    if(header+8>size || source_stride*rows>size-header-8 || h->bitmap_count>=256) return entry;
    size_t bytes=40+1024+dest_stride*rows;uint64_t converted=guest_alloc(c,bytes),resource=guest_alloc(c,16);
    uint8_t *out=writable_memory(c,converted,bytes),*source=memory(c,data,size);if(!out || !source) return entry;
    memcpy(out,source,40);store(c,converted,32,40);store(c,converted+14,16,8);store(c,converted+20,32,dest_stride*rows);store(c,converted+32,32,256);store(c,converted+36,32,0);
    memcpy(out+40,source+header,8);
    for(size_t y=0;y<rows;y++) for(int x=0;x<width;x++) out[40+1024+y*dest_stride+x]=(source[header+8+y*source_stride+x/8]>>(7-x%8))&1;
    store(c,resource,32,converted-c->base);store(c,resource+4,32,bytes);h->bitmap_sources[h->bitmap_count]=entry;h->bitmap_entries[h->bitmap_count++]=resource;return resource;
}
int host_api(CPU *c,const char *name,uint64_t *result) {
    if(IS("_gmtime64")) {
        extern uint64_t thread_crt_buffer(CPU *,unsigned,size_t);
        if(!A(0)) { store(c,thread_crt_buffer(c,0,4),32,22);RET(0); }int64_t value=(int64_t)load(c,A(0),64);struct tm out;time_t seconds=(time_t)value;
        if(value<0 || value>INT64_C(32535215999) || (int64_t)seconds!=value || !gmtime_r(&seconds,&out)) { store(c,thread_crt_buffer(c,0,4),32,22);RET(0); }
        uint64_t address=thread_crt_buffer(c,1,36);if(!address) RET(0);
        int fields[]={out.tm_sec,out.tm_min,out.tm_hour,out.tm_mday,out.tm_mon,out.tm_year,out.tm_wday,out.tm_yday,out.tm_isdst};for(unsigned i=0;i<9;i++) store(c,address+4*i,32,(uint32_t)fields[i]);RET(address);
    }
    Host *h=c->host;
    if(IS("GetTempPathA")) {
        size_t n=strlen(h->temp)+1; if(A(0)<=n) RET(n+1);
        char *p=(char *)writable_memory(c,A(1),n+1);if(p) { memcpy(p,h->temp,n-1);p[n-1]='/';p[n]=0; } RET(n);
    }
    if(IS("VirtualProtect")) {
        /* Guest image is ordinary host data. Translated instruction bytes remain
           guarded regardless of guest page protection. No executable host pages. */
        if(!A(1) || !memory(c,A(0),A(1))) RET(0);
        if(A(3)) store(c,A(3),32,4); RET(1);
    }
    if(IS("CreateFileA")) {
        char pathbuf[PATH_MAX];char *p=host_path(c,A(0),pathbuf);if(!p) RET(UINT64_MAX);
        const char *mode=A(1)&0x40000000 ? (A(4)==3 ? "r+b" : "w+b") : "rb";
        FILE *f=fopen(p,mode);if(f) f=definition_stream(f,p,mode);if(!f) { c->last_error=errno==ENOENT ? 2 : 5;RET(UINT64_MAX); }
        for(unsigned i=0;i<256;i++) if(!h->files[i]) { h->files[i]=f;RET(i+0x1000); } fclose(f);RET(UINT64_MAX);
    }
    if(IS("WriteFile") || IS("ReadFile")) {
        uint64_t handle=A(0);
        if(IS("WriteFile") && (handle==2 || handle==3)) { uint8_t *p=memory(c,A(1),A(2));size_t n=p ? fwrite(p,1,A(2),handle==2 ? stdout : stderr) : 0;if(A(3)) store(c,A(3),32,n);RET(n==A(2)); }
        if(handle<0x1000 || handle>=0x1100 || !h->files[handle-0x1000]) { c->last_error=6;RET(0); }
        uint8_t *p=IS("ReadFile") ? writable_memory(c,A(1),A(2)) : memory(c,A(1),A(2));if(!p) RET(0);
        FILE *f=h->files[handle-0x1000];size_t n=IS("ReadFile") ? fread(p,1,A(2),f) : fwrite(p,1,A(2),f);
        if(A(3)) store(c,A(3),32,n);RET(!ferror(f));
    }
    if(IS("CloseHandle")) { uint64_t a=A(0);if(a>=0x1000 && a<0x1100 && h->files[a-0x1000]) { fclose(h->files[a-0x1000]);h->files[a-0x1000]=NULL;RET(1); } c->last_error=6;RET(0); }
    if(IS("GetFileSize")) { uint64_t a=A(0);if(a<0x1000 || a>=0x1100 || !h->files[a-0x1000]) RET(UINT32_MAX);FILE *f=h->files[a-0x1000];long pos=ftell(f);fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,pos,SEEK_SET);if(A(1)) store(c,A(1),32,(uint64_t)size>>32);RET((uint32_t)size); }
    if(IS("MultiByteToWideChar") || IS("WideCharToMultiByte")) {
        int to_wide=IS("MultiByteToWideChar"),signed_length=(int32_t)A(3);size_t length;
        if(signed_length<0) { if(to_wide) { char *p=guest_string(c,A(2));if(!p) RET(0);length=strlen(p)+1; }else { length=0;while(load(c,A(2)+length,16) && !c->failed) length+=2;length+=2; } }
        else length=(size_t)signed_length*(to_wide ? 1 : 2);
        uint8_t *input=memory(c,A(2),length);if(!input) RET(0);
        const char *encoding=A(0)==65001 ? "UTF-8" : "WINDOWS-1252";
        iconv_t converter=iconv_open(to_wide ? "UTF-16LE" : encoding,to_wide ? encoding : "UTF-16LE");if(converter==(iconv_t)-1) RET(0);
        size_t cap=length*4+16,left=cap,inleft=length;char *buffer=malloc(cap),*output=buffer,*source=(char *)input;
        if(!buffer) { iconv_close(converter);RET(0); }size_t rc=iconv(converter,&source,&inleft,&output,&left);iconv_close(converter);
        if(rc==(size_t)-1) { free(buffer);c->last_error=1113;RET(0); }
        size_t bytes=cap-left,count=to_wide ? bytes/2 : bytes;if(!A(5)) { free(buffer);RET(count); }
        if(A(5)<count) { free(buffer);c->last_error=122;RET(0); }
        uint8_t *dest=writable_memory(c,A(4),bytes);if(dest) memcpy(dest,buffer,bytes);free(buffer);if(!to_wide && A(7)) store(c,A(7),32,0);RET(count);
    }
    if(IS("GetModuleFileNameW")) { const char *s=executable_path();size_t n=strlen(s);if(A(2)<=n) RET(A(2));for(size_t i=0;i<=n;i++) store(c,A(1)+i*2,16,(unsigned char)s[i]);RET(n); }
    if(IS("FindResourceA")) { uint64_t pe=c->base+load(c,c->base+0x3c,32),root=c->base+load(c,pe+24+128,32);uint32_t type=resource_entry(c,root,0,A(2),0);if(!(type&0x80000000)) { c->last_error=1813;RET(0); }uint32_t entry=resource_entry(c,root,type&0x7fffffff,A(1),0);if(!(entry&0x80000000)) { c->last_error=1814;RET(0); }uint32_t leaf=resource_entry(c,root,entry&0x7fffffff,0,1);if(leaf&0x80000000) RET(0);RET(leaf ? (A(2)==2 ? bitmap_resource(c,root+leaf) : root+leaf) : 0); }
    if(IS("LoadResource")) RET(A(1) ? c->base+load(c,A(1),32) : 0);
    if(IS("SizeofResource")) RET(A(1) ? load(c,A(1)+4,32) : 0);
    if(IS("LockResource")) RET(A(0));
    if(IS("GlobalAddAtomA")) { char *s=guest_string(c,A(0));if(!s) RET(0);for(unsigned i=0;i<h->atom_count;i++) if(!strcmp(h->atoms[i],s)) RET(0xc000+i);if(h->atom_count>=256) RET(0);unsigned i=h->atom_count++;h->atoms[i]=strdup(s);RET(0xc000+i); }
    if(IS("GlobalAlloc")) RET(guest_alloc(c,A(1)));
    if(IS("GlobalLock")) RET(A(0));
    if(IS("GlobalUnlock")) RET(0);
    if(IS("GlobalSize")) RET(guest_size(c,A(0)));
    if(IS("LocalFree")) { guest_free(c,A(0));RET(0); }
    if(IS("__iob_func")) { if(!h->iob) { h->iob=guest_alloc(c,144);for(unsigned i=0;i<3;i++) store(c,h->iob+i*48+28,32,i); }RET(h->iob); }
    if(IS("fopen")) { char buf[PATH_MAX];char *p=host_path(c,A(0),buf),*mode=guest_string(c,A(1));FILE *f=p && mode ? fopen(p,mode) : NULL;if(f) f=definition_stream(f,p,mode);if(!f) RET(0);for(unsigned i=0;i<256;i++) if(!h->files[i]) { h->files[i]=f;RET(0x1000+i); }fclose(f);RET(0); }
    if(IS("fclose")) { uint64_t a=A(0);FILE *f=host_file(c,a);if(!f) RET(-1);int rc=fclose(f);if(a>=0x1000 && a<0x1100) h->files[a-0x1000]=NULL;RET(rc); }
    if(IS("fread") || IS("fwrite")) { FILE *f=host_file(c,A(3));if(!f || (A(1) && A(2)>SIZE_MAX/A(1))) RET(0);size_t n=A(1)*A(2);if(!n) RET(0);uint8_t *p=IS("fread") ? writable_memory(c,A(0),n) : memory(c,A(0),n);if(!p) RET(0);RET(IS("fread") ? fread(p,A(1),A(2),f) : fwrite(p,A(1),A(2),f)); }
    if(IS("fseek")) { FILE *f=host_file(c,A(0));RET(f ? fseek(f,(int32_t)A(1),(int)A(2)) : -1); }
    if(IS("feof") || IS("ferror")) { FILE *f=host_file(c,A(0));RET(f ? (IS("feof") ? feof(f) : ferror(f)) : -1); }
    if(IS("fgets")) { FILE *f=host_file(c,A(2));uint8_t *p=writable_memory(c,A(0),A(1));RET(f && p && fgets((char *)p,(int)A(1),f) ? A(0) : 0); }
    if(IS("fputs")) { FILE *f=host_file(c,A(1));char *p=guest_string(c,A(0));RET(f && p ? fputs(p,f) : -1); }
    if(IS("_write") || IS("_read")) { uint8_t *p=IS("_read") ? writable_memory(c,A(1),A(2)) : memory(c,A(1),A(2));RET(p ? (IS("_read") ? read((int)A(0),p,A(2)) : write((int)A(0),p,A(2))) : -1); }
    if(IS("_lseek")) RET(lseek((int)A(0),(int32_t)A(1),(int)A(2)));
    if(IS("FindFirstFileA")) { char buf[PATH_MAX];char *p=host_path(c,A(0),buf);if(!p) RET(UINT64_MAX);size_t n=strlen(p);if(n>=3 && !strcmp(p+n-3,"*.*")) p[n-2]=0;for(unsigned i=0;i<32;i++) if(!h->find_used[i]) { int rc=glob(p,0,NULL,&h->finds[i]);if(rc || !h->finds[i].gl_pathc) { globfree(&h->finds[i]);c->last_error=2;RET(UINT64_MAX); }h->find_used[i]=1;h->find_index[i]=0;find_data(c,h->finds[i].gl_pathv[0],A(1));RET(0x1200+i); }RET(UINT64_MAX); }
    if(IS("FindNextFileA")) { uint64_t i=A(0)-0x1200;if(i>=32 || !h->find_used[i]) RET(0);if(++h->find_index[i]>=h->finds[i].gl_pathc) { c->last_error=18;RET(0); }RET(find_data(c,h->finds[i].gl_pathv[h->find_index[i]],A(1))); }
    if(IS("FindClose")) { uint64_t i=A(0)-0x1200;if(i<32 && h->find_used[i]) { globfree(&h->finds[i]);h->find_used[i]=0;RET(1); }RET(0); }
    if(IS("CryptAcquireContextA")) { store(c,A(0),64,1);RET(1); }
    if(IS("CryptReleaseContext")) RET(1);
    if(IS("CryptGenRandom")) { uint8_t *p=writable_memory(c,A(2),A(1));if(!p) RET(0);FILE *f=fopen("/dev/urandom","rb");if(!f) RET(0);size_t n=fread(p,1,A(1),f);fclose(f);RET(n==A(1)); }
    if(IS("SHGetFolderPathA")) { const char *p=profile_path();size_t n=strlen(p)+1;if(n>260) RET(0x80004005);uint8_t *d=writable_memory(c,A(4),n);if(d) memcpy(d,p,n);RET(0); }
    if(IS("GetFullPathNameA")) { char buf[PATH_MAX],absolute[PATH_MAX];char *p=host_path(c,A(0),buf);if(!p) RET(0);if(p[0]=='/') snprintf(absolute,sizeof absolute,"%s",p);else { if(!getcwd(absolute,sizeof absolute)) RET(0);size_t k=strlen(absolute);snprintf(absolute+k,sizeof absolute-k,"/%s",p); }size_t n=strlen(absolute);if(A(1)<=n) RET(n+1);uint8_t *d=writable_memory(c,A(2),n+1);if(d) memcpy(d,absolute,n+1);if(A(3)) { char *part=strrchr(absolute,'/');store(c,A(3),64,A(2)+(part ? (size_t)(part+1-absolute) : 0)); }RET(n); }
    if(IS("GetTempFileNameA")) { char path[PATH_MAX],output[PATH_MAX];char *p=host_path(c,A(0),path),*prefix=guest_string(c,A(1));if(!p || !prefix) RET(0);int n=snprintf(output,sizeof output,"%s/%.*sXXXXXX",p,3,prefix);if(n<0 || n>=260) RET(0);int fd=mkstemp(output);if(fd<0) { c->last_error=5;RET(0); }close(fd);size_t length=strlen(output)+1;uint8_t *d=writable_memory(c,A(3),length);if(d) memcpy(d,output,length);static uint32_t serial;RET((++serial%65535)+1); }
    if(IS("MoveFileExA")) { char source[PATH_MAX],dest[PATH_MAX];char *a=host_path(c,A(0),source),*b=host_path(c,A(1),dest);if(!a || !b) RET(0);if(!(A(2)&1) && !access(b,F_OK)) { c->last_error=183;RET(0); }int rc=rename(a,b);
        if(rc && errno==EXDEV && (A(2)&2)) {
            char temporary[PATH_MAX];int n=snprintf(temporary,sizeof temporary,"%s.winbox-XXXXXX",b);int fd=n>0 && n<(int)sizeof temporary ? mkstemp(temporary) : -1;
            FILE *source=fopen(a,"rb"),*destfile=fd>=0 ? fdopen(fd,"wb") : NULL;
            int ok=source && destfile;char block[16384];size_t count;
            if(ok) { while((count=fread(block,1,sizeof block,source))) if(fwrite(block,1,count,destfile)!=count) { ok=0;break; }if(ferror(source) || fflush(destfile)) ok=0; }
            if(source) fclose(source);if(destfile) { if(fclose(destfile)) ok=0; }else if(fd>=0) close(fd);
            if(ok && !rename(temporary,b)) { unlink(a);rc=0; }else if(fd>=0) unlink(temporary);
        }
        if(rc) { if(getenv("WINBOX_TRACE")) fprintf(stderr,"MoveFileEx failed: %s -> %s: %s\n",a,b,strerror(errno));c->last_error=errno==ENOENT ? 2 : 5; }RET(!rc); }
    if(IS("RemoveDirectoryA")) { char buf[PATH_MAX];char *p=host_path(c,A(0),buf);RET(p && !rmdir(p)); }
    if(IS("GetFileTime")) { FILE *f=host_file(c,A(0));struct stat st;if(!f || fstat(fileno(f),&st)) RET(0);if(A(1)) store(c,A(1),64,((uint64_t)st.st_ctime+11644473600ULL)*10000000);if(A(2)) store(c,A(2),64,((uint64_t)st.st_atime+11644473600ULL)*10000000);if(A(3)) store(c,A(3),64,((uint64_t)st.st_mtime+11644473600ULL)*10000000);RET(1); }
    if(IS("FileTimeToDosDateTime")) { uint64_t value=load(c,A(0),64);time_t seconds=(time_t)(value/10000000)-11644473600LL;struct tm tm;if(!localtime_r(&seconds,&tm) || tm.tm_year<80 || tm.tm_year>207) RET(0);store(c,A(1),16,((tm.tm_year-80)<<9)|((tm.tm_mon+1)<<5)|tm.tm_mday);store(c,A(2),16,(tm.tm_hour<<11)|(tm.tm_min<<5)|(tm.tm_sec/2));RET(1); }
    if(IS("GetStdHandle")) RET((int32_t)A(0)==-10 ? 1 : (int32_t)A(0)==-11 ? 2 : 3);
    if(IS("RtlAddFunctionTable")) RET(1);
    if(IS("GetCurrentDirectoryA")) { char buffer[PATH_MAX];if(!getcwd(buffer,sizeof buffer)) RET(0);size_t n=strlen(buffer);if(A(0)<=n) RET(n+1);char *p=(char *)writable_memory(c,A(1),n+1);if(p) memcpy(p,buffer,n+1);RET(n); }
    if(IS("GetFileAttributesA")) { char pathbuf[PATH_MAX];char *p=host_path(c,A(0),pathbuf);struct stat st;if(!p || stat(p,&st)) { c->last_error=2;RET(UINT32_MAX); }RET(S_ISDIR(st.st_mode) ? 0x10 : 0x80); }
    if(IS("CreateDirectoryA") || IS("SHCreateDirectoryExA")) { char pathbuf[PATH_MAX];char *p=host_path(c,A(IS("CreateDirectoryA") ? 0 : 1),pathbuf);int rc=p ? (IS("SHCreateDirectoryExA") ? mkdirs(p) : mkdir(p,0700)) : -1;if(rc && errno==EEXIST) { c->last_error=183;RET(IS("CreateDirectoryA") ? 0 : 183); }RET(IS("CreateDirectoryA") ? rc==0 : rc ? 5 : 0); }
    if(IS("DeleteFileA")) { char pathbuf[PATH_MAX];char *p=host_path(c,A(0),pathbuf);RET(p && !unlink(p)); }
    if(IS("GetCommandLineW")) { const char *s="winbox-native";uint64_t p=guest_alloc(c,28);for(unsigned i=0;i<13;i++) store(c,p+2*i,16,s[i]);RET(p); }
    if(IS("GetCurrentProcessId")) RET(getpid());
    if(IS("GetCurrentThreadId")) RET(1);
    if(IS("GetCurrentProcess")) RET(UINT64_MAX);
    if(IS("GetTickCount")) RET((uint32_t)ticks());
    if(IS("QueryPerformanceCounter")) { store(c,A(0),64,ticks()*1000);RET(1); }
    if(IS("QueryPerformanceFrequency")) { store(c,A(0),64,1000000);RET(1); }
    if(IS("GetSystemTimeAsFileTime")) { struct timespec t;timespec_get(&t,TIME_UTC);store(c,A(0),64,((uint64_t)t.tv_sec+11644473600ULL)*10000000+t.tv_nsec/100);RET(0); }
    if(IS("GetStartupInfoA")) { uint8_t *p=writable_memory(c,A(0),104);if(p) { memset(p,0,104);store(c,A(0),32,104); }RET(0); }
    if(IS("Sleep")) { struct timespec t={A(0)/1000,(A(0)%1000)*1000000};nanosleep(&t,NULL);RET(0); }
    if(IS("SetUnhandledExceptionFilter")) { uint64_t old=h->filter;h->filter=A(0);RET(old); }
    if(IS("InitializeCriticalSection") || IS("DeleteCriticalSection") || IS("EnterCriticalSection") || IS("LeaveCriticalSection")) RET(0);
    if(IS("InitializeCriticalSectionAndSpinCount")) RET(1);
    if(IS("TlsAlloc")) { if(h->tls_count==128) RET(UINT32_MAX); RET(h->tls_count++); }
    if(IS("TlsGetValue")) { c->last_error=0;RET(A(0)<128 ? h->tls[A(0)] : 0); }
    if(IS("TlsSetValue")) { if(A(0)>=128) RET(0);h->tls[A(0)]=A(1);RET(1); }
    if(IS("TlsFree")) RET(A(0)<128);
    if(IS("GetModuleHandleA") || IS("GetModuleHandleW")) RET(A(0) ? 0x70000000 : c->base);
    if(IS("LoadLibraryExA")) { char *p=guest_string(c,A(0));if(p && (!strcasecmp(p,"user32.dll") || !strcasecmp(p,"kernel32.dll") || !strcasecmp(p,"gdi32.dll") || !strcasecmp(p,"msvcrt.dll"))) RET(0x70000000);c->last_error=126;RET(0); }
    if(IS("GetProcAddress")) RET(0);
    if(IS("GetModuleFileNameA")) { const char *s=executable_path();size_t n=strlen(s);if(A(2)<=n) RET(A(2));uint8_t *p=writable_memory(c,A(1),n+1);if(p) memcpy(p,s,n+1);RET(n); }
    if(IS("GetProcessHeap")) RET(1);
    if(IS("HeapAlloc")) RET(guest_alloc(c,A(2)));
    if(IS("HeapFree")) { guest_free(c,A(2));RET(1); }
    if(IS("VirtualAlloc")) RET(A(0) ? 0 : guest_alloc(c,A(1)));
    if(IS("VirtualFree")) { guest_free(c,A(0));RET(1); }
    if(IS("IsDebuggerPresent")) RET(0);
    if(IS("ExitProcess") || IS("TerminateProcess")) { c->pc=0;RET(0); }
    return 0;
}
