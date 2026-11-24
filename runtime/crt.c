#include "rt.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <strings.h>
#include <time.h>
#define IS(s) (!strcmp(name,s))
#define A(n) argument(c,n)
#define RET(v) do { *result=(uint64_t)(v);return 1; } while(0)
uint64_t crt_data(CPU *c,const char *name) {
    uint64_t a=guest_alloc(c,16);
    if(!strcmp(name,"_acmdln")) store(c,a,64,guest_string_alloc(c,"winbox-native"));
    if(!strcmp(name,"__initenv")) store(c,a,64,guest_alloc(c,8));
    return a;
}
static uint64_t next_arg(CPU *c,unsigned *slot,uint64_t *va) { if(*va) { uint64_t v=load(c,*va,64);*va+=8;return v; }return argument(c,(*slot)++); }
static char *format_text(CPU *c,const char *format,unsigned slot,uint64_t va,size_t *length) {
    size_t used=0,capacity=4096;char *out=malloc(capacity);if(!out) return NULL;
    for(const char *p=format;*p && !c->failed;) {
        char buffer[8192];int n=0;
        if(*p!='%') { buffer[0]=*p++;n=1; }
        else if(*++p=='%') { buffer[0]=*p++;n=1; }
        else {
            char spec[96]="%";size_t k=1;
            while(*p && strchr("-+ #0",*p) && k<16) spec[k++]=*p++;
            if(*p=='*') { int width=(int)next_arg(c,&slot,&va);k+=(size_t)snprintf(spec+k,sizeof spec-k,"%d",width);p++; }
            else while(isdigit((unsigned char)*p) && k<30) spec[k++]=*p++;
            if(*p=='.') { spec[k++]=*p++;if(*p=='*') { int precision=(int)next_arg(c,&slot,&va);if(precision>=0) k+=(size_t)snprintf(spec+k,sizeof spec-k,"%d",precision);else k--;p++; }else while(isdigit((unsigned char)*p) && k<50) spec[k++]=*p++; }
            int wide=0,big=0,small=0;
            if(!strncmp(p,"I64",3)) { big=1;p+=3; }
            else if(*p=='l') { p++;wide=1;if(*p=='l') { p++;big=1; } }
            else if(*p=='z' || *p=='t' || *p=='j') { p++;big=1; }
            else if(*p=='h') { p++;small=16;if(*p=='h') { p++;small=8; } }
            char kind=*p;if(!kind) { trap(c,"unterminated printf format");break; }p++;
            uint64_t value=next_arg(c,&slot,&va);
            if(kind=='s' || kind=='S') {
                char *s=guest_string(c,value);
                if(wide || kind=='S') { trap(c,"wide printf string is not implemented");break; }
                spec[k++]='s';spec[k]=0;n=snprintf(buffer,sizeof buffer,spec,s ? s : "(null)");
            } else if(strchr("diuoxX",kind)) {
                if(!big) value=(kind=='d' || kind=='i') ? (uint64_t)(int64_t)(small==8 ? (int8_t)value : small==16 ? (int16_t)value : (int32_t)value) : small==8 ? (uint8_t)value : small==16 ? (uint16_t)value : (uint32_t)value;
                spec[k++]='l';spec[k++]='l';spec[k++]=kind;spec[k]=0;
                n=kind=='d' || kind=='i' ? snprintf(buffer,sizeof buffer,spec,(long long)value) : snprintf(buffer,sizeof buffer,spec,(unsigned long long)value);
            } else if(strchr("fFeEgGaA",kind)) { double d;memcpy(&d,&value,8);spec[k++]=kind;spec[k]=0;n=snprintf(buffer,sizeof buffer,spec,d); }
            else if(kind=='c') { spec[k++]='c';spec[k]=0;n=snprintf(buffer,sizeof buffer,spec,(int)value); }
            else if(kind=='p') n=snprintf(buffer,sizeof buffer,"%llx",(unsigned long long)value);
            else if(kind=='n') { store(c,value,big ? 64 : small ? small : 32,used);n=0; }
            else { trap(c,"unsupported printf conversion");break; }
        }
        if(n<0 || n>=(int)sizeof buffer || used+(size_t)n>1024*1024) { trap(c,"printf output exceeds runtime limit");break; }
        if(used+(size_t)n+1>capacity) { capacity=(used+n+1)*2;char *next=realloc(out,capacity);if(!next) { free(out);return NULL; }out=next; }
        memcpy(out+used,buffer,n);used+=n;
    }
    out[used]=0;*length=used;return out;
}
/* Parse one conversion at a time; guest pointers and Windows long sizes must
 * never be passed to the host variadic scanner. */
static int scan_text(CPU *c,const char *input,const char *format) {
    const char *start=input;unsigned slot=2;int assigned=0,converted=0;
    while(*format && !c->failed) {
        if(isspace((unsigned char)*format)) { while(isspace((unsigned char)*format)) format++;while(isspace((unsigned char)*input)) input++;continue; }
        if(*format!='%' || format[1]=='%') {
            char literal=*format++;if(literal=='%') format++;
            if(*input!=literal) return !*input && !converted ? -1 : assigned;
            input++;continue;
        }
        format++;int suppress=*format=='*';if(suppress) format++;
        size_t width=0;while(isdigit((unsigned char)*format)) { width=width*10+(*format++-'0');if(width>1048576) { trap(c,"scanf width exceeds runtime limit");return assigned; } }
        int bits=32,long_float=0;
        if(!strncmp(format,"I64",3)) { bits=64;format+=3; }
        else if(*format=='h') { format++;bits=16;if(*format=='h') { format++;bits=8; } }
        else if(*format=='l') { format++;long_float=1;if(*format=='l') { format++;bits=64; } }
        else if(*format=='z' || *format=='t' || *format=='j') { bits=64;format++; }
        char kind=*format;if(!kind) { trap(c,"unterminated scanf format");return assigned; }format++;
        if(kind=='n') { if(!suppress) store(c,argument(c,slot++),bits,input-start);converted=1;continue; }
        if(kind=='s' || kind=='c' || kind=='[') {
            unsigned char set[256]={0};int invert=0;
            if(kind=='[') {
                if(*format=='^') { invert=1;format++; }
                if(*format==']') { set[']']=1;format++; }
                while(*format && *format!=']') {
                    unsigned char lo=*format++;
                    if(*format=='-' && format[1] && format[1]!=']') { format++;unsigned char hi=*format++;for(unsigned k=lo;k<=hi;k++) set[k]=1; }
                    else set[lo]=1;
                }
                if(*format!=']') { trap(c,"unterminated scanf scanset");return assigned; }format++;
            }
            if(kind=='s') while(isspace((unsigned char)*input)) input++;
            size_t n=0,limit=width ? width : kind=='c' ? 1 : strlen(input);
            while(input[n] && n<limit && (kind=='c' || (kind=='s' ? !isspace((unsigned char)input[n]) : !!set[(unsigned char)input[n]]!=invert))) n++;
            if(!n) return !*input && !converted ? -1 : assigned;
            if(!suppress) { uint8_t *out=writable_memory(c,argument(c,slot++),n+(kind!='c'));if(out) { memcpy(out,input,n);if(kind!='c') out[n]=0; }assigned++; }
            input+=n;converted=1;continue;
        }
        if(!strchr("diuoxXpfFeEgGaA",kind)) { trap(c,"unsupported scanf conversion");return assigned; }
        while(isspace((unsigned char)*input)) input++;
        if(!*input) return converted ? assigned : -1;
        size_t length=strlen(input);if(width && length>width) length=width;
        char *part=malloc(length+1);if(!part) { trap(c,"scanf allocation failed");return assigned; }memcpy(part,input,length);part[length]=0;
        char spec[8];int consumed=-1;uint64_t integer=0;double real=0;
        if(strchr("fFeEgGaA",kind)) { strcpy(spec,"%lf%n");sscanf(part,spec,&real,&consumed); }
        else if(kind=='d' || kind=='i') { long long value=0;snprintf(spec,sizeof spec,"%%ll%c%%n",kind);sscanf(part,spec,&value,&consumed);integer=(uint64_t)value; }
        else { unsigned long long value=0;snprintf(spec,sizeof spec,"%%ll%c%%n",kind=='p' ? 'x' : kind);sscanf(part,spec,&value,&consumed);integer=value; }
        free(part);if(consumed<0) return assigned;
        if(!suppress) {
            uint64_t target=argument(c,slot++);
            if(strchr("fFeEgGaA",kind)) { if(long_float) { memcpy(&integer,&real,8);store(c,target,64,integer); }else { float value=real;uint32_t raw;memcpy(&raw,&value,4);store(c,target,32,raw); } }
            else store(c,target,kind=='p' ? 64 : bits,integer);
            assigned++;
        }
        input+=consumed;converted=1;
    }
    return assigned;
}
static size_t allocation_size(CPU *c,uint64_t a) { extern size_t guest_size(CPU *,uint64_t);return guest_size(c,a); }
int crt_api(CPU *c,const char *name,uint64_t *result) {
    if(IS("log10")) { double argument,value;memcpy(&argument,c->xmm[0],sizeof argument);value=log10(argument);memcpy(c->xmm[0],&value,sizeof value);if(argument<=0) { extern uint64_t thread_crt_buffer(CPU *,unsigned,size_t);store(c,thread_crt_buffer(c,0,4),32,argument==0 ? 34 : 33); }RET(0); }
    if(IS("strftime")) {
        if(!A(1) || A(1)>1024*1024) RET(0);char *format=guest_string(c,A(2));if(!format || !A(3)) RET(0);
        struct tm value={0};int *fields[]={&value.tm_sec,&value.tm_min,&value.tm_hour,&value.tm_mday,&value.tm_mon,&value.tm_year,&value.tm_wday,&value.tm_yday,&value.tm_isdst};for(unsigned i=0;i<9;i++) *fields[i]=(int32_t)load(c,A(3)+4*i,32);
        char *buffer=calloc(1,A(1));if(!buffer) RET(0);size_t length=strftime(buffer,A(1),format,&value);if(length) { uint8_t *out=writable_memory(c,A(0),length+1);if(out) memcpy(out,buffer,length+1); }free(buffer);RET(length);
    }
    if(IS("sscanf")) { char *input=guest_string(c,A(0)),*fmt=guest_string(c,A(1));RET(input && fmt ? scan_text(c,input,fmt) : -1); }
    if(IS("fprintf") || IS("vfprintf")) { char *fmt=guest_string(c,A(1));if(!fmt) RET(-1);size_t n=0;char *s=format_text(c,fmt,2,IS("vfprintf") ? A(2) : 0,&n);extern FILE *host_file(CPU *,uint64_t);FILE *f=host_file(c,A(0));if(!s || !f) { free(s);RET(-1); }size_t written=fwrite(s,1,n,f);free(s);RET(written); }
    if(IS("sprintf") || IS("_snprintf") || IS("_vsnprintf")) {
        int bounded=!IS("sprintf");char *fmt=guest_string(c,A(bounded ? 2 : 1));if(!fmt) RET(-1);
        size_t n=0;char *s=format_text(c,fmt,bounded ? 3 : 2,IS("_vsnprintf") ? A(3) : 0,&n);if(!s) RET(-1);
        size_t capacity=bounded ? A(1) : n+1,count=n<capacity ? n : capacity;
        uint8_t *p=writable_memory(c,A(0),count+(n<capacity ? 1 : 0));if(p) { memcpy(p,s,count);if(n<capacity) p[n]=0; }free(s);RET(bounded && n>=capacity ? -1 : (int)n);
    }
    if(IS("malloc")) RET(guest_alloc(c,A(0)));
    if(IS("calloc")) { if(A(1) && A(0)>SIZE_MAX/A(1)) RET(0);RET(guest_alloc(c,A(0)*A(1))); }
    if(IS("free")) { guest_free(c,A(0));RET(0); }
    if(IS("_msize")) RET(allocation_size(c,A(0)));
    if(IS("realloc")) {
        if(!A(1)) { guest_free(c,A(0));RET(0); }
        uint64_t a=guest_alloc(c,A(1));if(!a) RET(0);
        size_t old=A(0) ? allocation_size(c,A(0)) : 0,n=old<A(1) ? old : A(1);
        if(n) { uint8_t *s=memory(c,A(0),n),*d=writable_memory(c,a,n);if(s && d) memcpy(d,s,n); }
        guest_free(c,A(0));RET(a);
    }
    if(IS("memcpy") || IS("memmove")) { size_t n=A(2);if(n) { uint8_t *s=memory(c,A(1),n),*d=writable_memory(c,A(0),n);if(s && d) memmove(d,s,n); }RET(A(0)); }
    if(IS("memset")) { uint8_t *d=writable_memory(c,A(0),A(2));if(d) memset(d,(int)A(1),A(2));RET(A(0)); }
    if(IS("memcmp")) { size_t n=A(2);if(!n) RET(0);uint8_t *a=memory(c,A(0),n),*b=memory(c,A(1),n);RET(a && b ? memcmp(a,b,n) : 0); }
    if(IS("memchr")) { size_t n=A(2);if(!n) RET(0);uint8_t *a=memory(c,A(0),n),*p=a ? memchr(a,(int)A(1),n) : NULL;RET(p ? A(0)+(p-a) : 0); }
    if(IS("strlen")) { char *s=guest_string(c,A(0));RET(s ? strlen(s) : 0); }
    if(IS("strcmp") || IS("_stricmp") || IS("strncmp")) { char *a=guest_string(c,A(0)),*b=guest_string(c,A(1));if(!a || !b) RET(0); RET(IS("strcmp") ? strcmp(a,b) : IS("_stricmp") ? strcasecmp(a,b) : strncmp(a,b,A(2))); }
    if(IS("strcpy") || IS("strcat") || IS("strncpy")) {
        char *s=guest_string(c,A(1));if(!s) RET(A(0));
        size_t offset=0;if(IS("strcat")) { char *d=guest_string(c,A(0));if(!d) RET(0);offset=strlen(d); }
        size_t n=IS("strncpy") ? A(2) : strlen(s)+1;uint8_t *d=writable_memory(c,A(0)+offset,n);
        if(d) { if(IS("strncpy")) strncpy((char *)d,s,n);else memcpy(d,s,n); }RET(A(0));
    }
    if(IS("strchr") || IS("strrchr")) { char *s=guest_string(c,A(0));char *p=s ? (IS("strchr") ? strchr(s,(int)A(1)) : strrchr(s,(int)A(1))) : NULL; RET(p ? A(0)+(p-s) : 0); }
    if(IS("_strdup")) { char *s=guest_string(c,A(0));RET(s ? guest_string_alloc(c,s) : 0); }
    if(IS("atoi") || IS("_atoi64")) { char *s=guest_string(c,A(0));RET(s ? strtoll(s,NULL,10) : 0); }
    if(IS("strtol") || IS("strtoul")) { char *s=guest_string(c,A(0)),*end=NULL;if(!s) RET(0);uint64_t v=IS("strtol") ? (uint32_t)strtol(s,&end,(int)A(2)) : (uint32_t)strtoul(s,&end,(int)A(2));if(A(1)) store(c,A(1),64,A(0)+(end-s));RET(v); }
    if(IS("tolower")) RET(tolower((unsigned char)A(0)));
    if(IS("toupper")) RET(toupper((unsigned char)A(0)));
    if(IS("isalnum")) RET(!!isalnum((unsigned char)A(0)));
    if(IS("isalpha")) RET(!!isalpha((unsigned char)A(0)));
    if(IS("isdigit")) RET(!!isdigit((unsigned char)A(0)));
    if(IS("islower")) RET(!!islower((unsigned char)A(0)));
    if(IS("isprint")) RET(!!isprint((unsigned char)A(0)));
    if(IS("isspace")) RET(!!isspace((unsigned char)A(0)));
    if(IS("isupper")) RET(!!isupper((unsigned char)A(0)));
    if(IS("isxdigit")) RET(!!isxdigit((unsigned char)A(0)));
    if(IS("__set_app_type") || IS("__setusermatherr") || IS("__lconv_init")) RET(0);
    if(IS("__getmainargs")) {
        uint64_t args=guest_alloc(c,16),env=guest_alloc(c,8);store(c,args,64,guest_string_alloc(c,"winbox-native"));
        store(c,A(0),32,1);store(c,A(1),64,args);store(c,A(2),64,env);RET(0);
    }
    if(IS("_initterm")) {
        uint64_t start=A(0),end=A(1);
        while(start<end && !c->failed) { uint64_t fn=load(c,start,64);if(fn && !callback(c,fn,NULL,0)) break;start+=8; }RET(0);
    }
    if(IS("_onexit") || IS("atexit")) RET(IS("_onexit") ? A(0) : 0);
    if(IS("_errno")) { extern uint64_t thread_crt_buffer(CPU *,unsigned,size_t);RET(thread_crt_buffer(c,0,4)); }
    if(IS("signal")) RET(0);
    if(IS("_cexit")) RET(0);
    if(IS("exit") || IS("_exit")) { c->pc=0;RET(0); }
    if(IS("abort") || IS("_amsg_exit")) { trap(c,"guest CRT fatal error");RET(0); }
    if(IS("srand")) { srand((unsigned)A(0));RET(0); }
    if(IS("rand")) RET(rand()&32767);
    if(IS("puts")) { char *s=guest_string(c,A(0));RET(s ? puts(s) : -1); }
    return 0;
}
