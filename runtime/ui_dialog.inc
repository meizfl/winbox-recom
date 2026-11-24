/* SDL's asynchronous native/portal picker behind Win32's modal file APIs. */
#include <stdatomic.h>
#include <sys/stat.h>

typedef struct {
    atomic_int references, done;
    char **files;
    size_t count;
    int error, filter_index;
    SDL_PropertiesID properties;
    SDL_DialogFileFilter filters[32];
    int filter_count;
} NativeFileDialog;

static void file_dialog_release(NativeFileDialog *dialog) {
    if(atomic_fetch_sub(&dialog->references,1)!=1) return;
    for(size_t i=0;i<dialog->count;i++) free(dialog->files[i]);
    free(dialog->files);
    for(int i=0;i<dialog->filter_count;i++) {
        free((char *)dialog->filters[i].name);
        free((char *)dialog->filters[i].pattern);
    }
    if(dialog->properties) SDL_DestroyProperties(dialog->properties);
    free(dialog);
}

static void SDLCALL file_dialog_result(void *opaque,const char *const *files,int filter) {
    NativeFileDialog *dialog=opaque;
    dialog->filter_index=filter;
    if(!files) dialog->error=1;
    else {
        size_t count=0;
        while(files[count] && count<4096) count++;
        if(count) {
            dialog->files=calloc(count,sizeof *dialog->files);
            if(!dialog->files) dialog->error=1;
            else for(size_t i=0;i<count;i++) {
                char *path=strdup(files[i]);
                if(!path) { dialog->error=1;break; }
                dialog->files[dialog->count++]=path;
            }
        }
    }
    atomic_store(&dialog->done,1);
    file_dialog_release(dialog);
}

static char *dialog_extensions(const char *pattern) {
    if(!strcmp(pattern,"*.*") || !strcmp(pattern,"*")) return strdup("*");
    char *out=calloc(strlen(pattern)+1,1);
    if(!out) return NULL;
    size_t used=0;
    for(const char *p=pattern;*p;) {
        while(*p==' ' || *p==';') p++;
        if(p[0]=='*' && p[1]=='.') p+=2;
        if(used && *p) out[used++]=';';
        while(*p && *p!=';') {
            if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='_' || *p=='-' || *p=='.')) { free(out);return strdup("*"); }
            out[used++]=*p++;
        }
    }
    if(!used) { free(out);return strdup("*"); }
    return out;
}

/* The callback owns a second reference, including when the app quits while
 * the OS dialog is still closing. It never receives a pointer to stack data. */
static NativeFileDialog *choose_native_files(CPU *c,uint64_t owner,SDL_FileDialogType type,const char *title,const char *location,uint64_t filters,int many) {
    NativeFileDialog *dialog=calloc(1,sizeof *dialog);
    if(!dialog) return NULL;
    atomic_init(&dialog->references,2);atomic_init(&dialog->done,0);
    const char *test=getenv("WINBOX_TEST_DIALOG_FILES");
    if(test) {
        char *copy=strdup(test),*items[65]={0};size_t count=0;
        if(copy && strcmp(copy,"!error") && strcmp(copy,"!cancel")) {
            char *cursor=copy;
            while(cursor && *cursor && count<64) { items[count++]=cursor;char *next=strchr(cursor,'\n');if(next) *next++=0;cursor=next; }
        }
        file_dialog_result(dialog,copy && !strcmp(copy,"!error") ? NULL : (const char *const *)items,0);
        free(copy);return dialog;
    }
    if(!video(c)) { file_dialog_result(dialog,NULL,-1);return dialog; }
    while(filters && dialog->filter_count<32 && load(c,filters,8) && !c->failed) {
        char *name=guest_string(c,filters);if(!name) break;
        char *label=strdup(name);filters+=strlen(name)+1;
        char *pattern=guest_string(c,filters);if(!pattern) { free(label);break; }
        char *extensions=dialog_extensions(pattern);filters+=strlen(pattern)+1;
        if(!label || !extensions) { free(label);free(extensions);break; }
        dialog->filters[dialog->filter_count++]=(SDL_DialogFileFilter){label,extensions};
    }
    Window *parent=window(owner);
    while(parent && !parent->native) parent=window(parent->parent);
    dialog->properties=SDL_CreateProperties();
    if(parent) SDL_SetPointerProperty(dialog->properties,SDL_PROP_FILE_DIALOG_WINDOW_POINTER,parent->native);
    if(title && *title) SDL_SetStringProperty(dialog->properties,SDL_PROP_FILE_DIALOG_TITLE_STRING,title);
    if(location && *location) SDL_SetStringProperty(dialog->properties,SDL_PROP_FILE_DIALOG_LOCATION_STRING,location);
    SDL_SetBooleanProperty(dialog->properties,SDL_PROP_FILE_DIALOG_MANY_BOOLEAN,many);
    if(dialog->filter_count) {
        SDL_SetPointerProperty(dialog->properties,SDL_PROP_FILE_DIALOG_FILTERS_POINTER,dialog->filters);
        SDL_SetNumberProperty(dialog->properties,SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,dialog->filter_count);
    }
    SDL_ShowFileDialogWithProperties(type,file_dialog_result,dialog,dialog->properties);
    while(!atomic_load(&dialog->done) && !c->failed && !ui.quit) {
        extern void network_pump(void);network_pump();pump(c,1);
        while(ui.head!=ui.tail && !c->failed && !ui.quit) {
            Message message=ui.messages[ui.head];ui.head=(ui.head+1)%MAX_MSG;
            message_input_state(&message);Window *target=window(message.hwnd);
            if(target && message.message==0xf) target->paint_pending=0;
            invoke(c,target,message.message,message.wp,message.lp);
            if(target && message.message==0xf) request_present(target);
        }
    }
    return dialog;
}

static uint64_t native_file_dialog(CPU *c,uint64_t info,int save) {
    if(load(c,info,32)<120) { c->last_error=87;return 0; }
    uint64_t destination=load(c,info+48,64);uint32_t capacity=load(c,info+56,32),flags=load(c,info+96,32);
    if(!destination || !capacity || capacity>1024*1024) { c->last_error=87;return 0; }
    char *title=text(c,load(c,info+88,64),0),*directory=text(c,load(c,info+80,64),0),*initial=text(c,destination,0);
    if(!title || !directory || !initial) { free(title);free(directory);free(initial);c->last_error=8;return 0; }
    char *location=calloc(strlen(directory)+strlen(initial)+2,1);
    if(location) {
        if(*initial=='/' || (initial[0] && initial[1]==':')) strcpy(location,initial);
        else if(*directory) snprintf(location,strlen(directory)+strlen(initial)+2,"%s/%s",directory,initial);
        else strcpy(location,initial);
        for(char *p=location;*p;p++) if(*p=='\\') *p='/';
    }
    NativeFileDialog *dialog=choose_native_files(c,load(c,info+8,64),save ? SDL_FILEDIALOG_SAVEFILE : SDL_FILEDIALOG_OPENFILE,title,location,load(c,info+24,64),!save && (flags&0x200));
    free(title);free(directory);free(initial);free(location);
    if(!dialog) { c->last_error=8;return 0; }
    if(!atomic_load(&dialog->done) || dialog->error || !dialog->count) { if(dialog->error) c->last_error=120;file_dialog_release(dialog);return 0; }
    if(save && load(c,info+104,64)) {
        char *extension=guest_string(c,load(c,info+104,64));char *base=strrchr(dialog->files[0],'/');base=base ? base+1 : dialog->files[0];
        if(extension && *extension && !strchr(base,'.')) {
            size_t bytes=strlen(dialog->files[0])+strlen(extension)+2;char *path=malloc(bytes);
            if(path) { snprintf(path,bytes,"%s.%s",dialog->files[0],extension);free(dialog->files[0]);dialog->files[0]=path; }
        }
    }
    char *first=dialog->files[0],*last_slash=strrchr(first,'/');size_t folder=last_slash ? (size_t)(last_slash-first) : 0;
    int multiple=!save && (flags&0x200) && dialog->count>1;size_t needed=multiple ? folder+2 : strlen(first)+1;
    if(multiple) for(size_t i=0;i<dialog->count;i++) {
        char *slash=strrchr(dialog->files[i],'/');
        if(!slash || (size_t)(slash-dialog->files[i])!=folder || strncmp(first,dialog->files[i],folder)) { c->last_error=87;file_dialog_release(dialog);return 0; }
        needed+=strlen(slash+1)+1;
    }
    if(needed>capacity) { if(capacity>=2) store(c,destination,16,needed>65535 ? 65535 : needed);c->last_error=122;file_dialog_release(dialog);return 0; }
    char *out=(char *)writable_memory(c,destination,capacity);
    if(!out) { file_dialog_release(dialog);return 0; }
    memset(out,0,capacity);
    if(multiple) { memcpy(out,first,folder);size_t offset=folder+1;for(size_t i=0;i<dialog->count;i++) { const char *base=strrchr(dialog->files[i],'/')+1;size_t bytes=strlen(base)+1;memcpy(out+offset,base,bytes);offset+=bytes; } }
    else memcpy(out,first,strlen(first)+1);
    store(c,info+100,16,multiple ? folder+1 : last_slash ? folder+1 : 0);
    char *dot=strrchr(first,'.');store(c,info+102,16,!multiple && dot && (!last_slash || dot>last_slash) ? dot-first+1 : 0);
    if(dialog->filter_index>=0) store(c,info+44,32,dialog->filter_index+1);
    uint64_t file_title=load(c,info+64,64);unsigned title_size=load(c,info+72,32);
    if(file_title && title_size) { const char *base=last_slash ? last_slash+1 : first;size_t length=strlen(base);if(length>=title_size) length=title_size-1;uint8_t *p=writable_memory(c,file_title,length+1);if(p) { memcpy(p,base,length);p[length]=0; } }
    file_dialog_release(dialog);return 1;
}

static uint64_t native_folder_dialog(CPU *c,uint64_t info) {
    char *title=text(c,load(c,info+24,64),0);
    NativeFileDialog *dialog=choose_native_files(c,load(c,info,64),SDL_FILEDIALOG_OPENFOLDER,title,NULL,0,0);free(title);
    if(!dialog) return 0;uint64_t result=0;
    if(atomic_load(&dialog->done) && !dialog->error && dialog->count) {
        result=guest_string_alloc(c,dialog->files[0]);uint64_t display=load(c,info+16,64);
        if(display) { const char *base=strrchr(dialog->files[0],'/');base=base ? base+1 : dialog->files[0];size_t length=strlen(base);if(length>259) length=259;uint8_t *p=writable_memory(c,display,length+1);if(p) { memcpy(p,base,length);p[length]=0; } }
    }
    file_dialog_release(dialog);return result;
}
