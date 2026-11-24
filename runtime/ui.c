#include "rt.h"
#include <SDL3/SDL.h>
#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <ft2build.h>
#include FT_TRUETYPE_TABLES_H
#include <math.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#define EDIT_PROC UINT64_C(0xe0000000)
#define MAX_WINDOWS 2048
#define MAX_GDI 8192
#define MAX_MSG 16384
#define WH(n) (UINT64_C(0x50000000)+(n)*16)
#define GH(n) (UINT64_C(0x60000000)+(n)*16)
typedef struct { uint64_t hwnd;uint32_t message;uint64_t wp,lp;uint16_t modifiers;uint32_t buttons;int cursor_x,cursor_y; } Message;
typedef struct { char *name;uint64_t proc,background,cursor;unsigned extra,style; } Class;
typedef struct { char *text;uint64_t submenu,data;uint32_t id,state,type; } MenuItem;
typedef struct { MenuItem *items;size_t count; } Menu;
typedef struct { int height,ascent,descent,internal,external,average,maximum; } FontMetrics;
typedef struct { int used,type,metric_valid,cursor_shared;SDL_Cursor *cursor;FontMetrics metrics;uint32_t color;unsigned modern_icon;double width;int size,weight,font_width,pitch,charset,italic,underline,strike;char face[80];cairo_surface_t *surface;Menu menu; } Gdi;
typedef struct { int used;uint64_t zorder,hwnd,proc,parent,native_parent,class_cursor,style,exstyle,menu,instance,userdata,extra[32];
    int x,y,w,h,visible,paint_pending,painting,resize_pending,selection_start,selection_end,destroying,frame_dirty,native_popup;unsigned class_style;uint32_t opening_buttons;uint64_t last_present;char *title,*class_name;SDL_Window *native;cairo_surface_t *surface,*frame;
} Window;
typedef struct DCState { uint64_t font,brush,pen,bitmap;uint32_t text,bg;int bgmode,x,y;cairo_t *cr;cairo_surface_t *surface;struct DCState *next; } DCState;
typedef struct { int used;uint64_t window,font,brush,pen,bitmap;uint32_t text,bg;int bgmode,x,y,save_depth;
    cairo_t *cr;cairo_surface_t *surface;DCState *saved; } DC;
typedef struct { uint64_t hwnd,after;int x,y,width,height;unsigned flags,changes; } DeferredPosition;
typedef struct { uint64_t handle;int used;size_t count;DeferredPosition *positions; } DeferredBatch;
static DeferredBatch deferred[128];
static DeferredBatch *position_batch(uint64_t handle) { if(handle<0x78000000 || (handle-0x78000000)%16) return NULL;DeferredBatch *batch=&deferred[((handle-0x78000000)/16)%128];return batch->used && batch->handle==handle ? batch : NULL; }
static struct { uint64_t hwnd,id,next;uint32_t interval; } timers[256];
static struct { uint64_t hwnd,key,value;char *name; } properties[4096];
static struct { int ready,quit,capture_screenshot,test_input_observed,cursor_initialized,cursor_show_count;uint64_t cursor;uint32_t caret_blink_ms;struct { uint64_t owner,next;int x,y,w,h,visibility,phase,gray;cairo_surface_t *bitmap; } caret;Class classes[256];unsigned class_count;Window windows[MAX_WINDOWS];Gdi gdi[MAX_GDI];DC dc[MAX_GDI];
    Message messages[MAX_MSG];unsigned head,tail;uint64_t focus,capture,active;uint64_t started,idle_started;uint64_t test_popup,test_popup_at;int test_popup_x,test_popup_y;uint32_t clipboard_format;int clipboard_open,input_state;uint16_t event_modifiers,message_modifiers;uint32_t input_buttons,message_buttons;uint8_t keys[256];uint64_t clipboard_handles[17];int cursor_x,cursor_y,message_x,message_y,has_message_position;uint64_t pointer_window; } ui;
static unsigned virtual_key(SDL_Keycode key) {
    if(key>=SDLK_A && key<=SDLK_Z) return key-'a'+'A';if(key>=SDLK_F1 && key<=SDLK_F12) return key-SDLK_F1+112;if(key>=SDLK_F13 && key<=SDLK_F24) return key-SDLK_F13+124;
    if(key>=SDLK_KP_1 && key<=SDLK_KP_9) return key-SDLK_KP_1+97;
    switch(key) { case SDLK_RETURN:case SDLK_KP_ENTER:return 13;case SDLK_TAB:return 9;case SDLK_BACKSPACE:return 8;case SDLK_ESCAPE:return 27;
    case SDLK_LEFT:return 37;case SDLK_RIGHT:return 39;case SDLK_UP:return 38;case SDLK_DOWN:return 40;case SDLK_HOME:return 36;case SDLK_END:return 35;case SDLK_DELETE:return 46;case SDLK_INSERT:return 45;case SDLK_PAGEUP:return 33;case SDLK_PAGEDOWN:return 34;
    case SDLK_LSHIFT:case SDLK_RSHIFT:return 16;case SDLK_LCTRL:case SDLK_RCTRL:return 17;case SDLK_LALT:case SDLK_RALT:return 18;case SDLK_LGUI:return 91;case SDLK_RGUI:return 92;case SDLK_APPLICATION:return 93;
    case SDLK_CAPSLOCK:return 20;case SDLK_NUMLOCKCLEAR:return 144;case SDLK_SCROLLLOCK:return 145;case SDLK_PAUSE:return 19;case SDLK_PRINTSCREEN:return 44;
    case SDLK_KP_0:return 96;case SDLK_KP_MULTIPLY:return 106;case SDLK_KP_PLUS:return 107;case SDLK_KP_MINUS:return 109;case SDLK_KP_PERIOD:return 110;case SDLK_KP_DIVIDE:return 111;
    case SDLK_SEMICOLON:return 0xba;case SDLK_EQUALS:return 0xbb;case SDLK_COMMA:return 0xbc;case SDLK_MINUS:return 0xbd;case SDLK_PERIOD:return 0xbe;case SDLK_SLASH:return 0xbf;case SDLK_GRAVE:return 0xc0;case SDLK_LEFTBRACKET:return 0xdb;case SDLK_BACKSLASH:return 0xdc;case SDLK_RIGHTBRACKET:return 0xdd;case SDLK_APOSTROPHE:return 0xde;
    default:return key>=0 && key<128 ? key : 0; }
}
static void input_event_state(const SDL_Event *event) {
    if(event->type==SDL_EVENT_KEY_DOWN || event->type==SDL_EVENT_KEY_UP) ui.event_modifiers=event->key.mod;
    else if(event->type==SDL_EVENT_MOUSE_MOTION) ui.input_buttons=event->motion.state;
    else if(event->type==SDL_EVENT_MOUSE_BUTTON_DOWN) ui.input_buttons|=SDL_BUTTON_MASK(event->button.button);
    else if(event->type==SDL_EVENT_MOUSE_BUTTON_UP) ui.input_buttons&=~SDL_BUTTON_MASK(event->button.button);
}
static void message_input_state(const Message *message) {
    ui.message_x=message->cursor_x;ui.message_y=message->cursor_y;ui.has_message_position=1;uint32_t type=message->message;if(!((type>=0x100 && type<=0x109) || (type>=0x200 && type<=0x20e))) return;
    ui.message_modifiers=message->modifiers;ui.message_buttons=message->buttons;ui.input_state=1;
    if(message->wp<256 && (type==0x100 || type==0x104)) { unsigned key=message->wp;if(!(ui.keys[key]&0x80) && (key==20 || key==144 || key==145)) ui.keys[key]^=1;ui.keys[key]|=0x80; }
    if(message->wp<256 && (type==0x101 || type==0x105)) ui.keys[message->wp]&=1;
}
static unsigned mouse_key_flags(void) { unsigned flags=0;if(ui.input_buttons&SDL_BUTTON_LMASK) flags|=1;if(ui.input_buttons&SDL_BUTTON_RMASK) flags|=2;if(ui.input_buttons&SDL_BUTTON_MMASK) flags|=16;if(ui.input_buttons&SDL_BUTTON_X1MASK) flags|=32;if(ui.input_buttons&SDL_BUTTON_X2MASK) flags|=64;if(ui.event_modifiers&SDL_KMOD_SHIFT) flags|=4;if(ui.event_modifiers&SDL_KMOD_CTRL) flags|=8;return flags; }
static void origin(Window *,int *,int *);
static void request_present(Window *);
static Window *native_root(Window *);
static int menu_height(Window *);
static void color(cairo_t *,uint32_t);
static Window *window(uint64_t);
static int local_coordinates(void) { const char *driver=SDL_GetCurrentVideoDriver();return driver && !strcmp(driver,"wayland"); }
#include "ui_dpi.inc"
static SDL_Window *create_toplevel(const char *title,int x,int y,int width,int height,SDL_WindowFlags flags,SDL_Window *parent) {
    x=native_points(parent,x);y=native_points(parent,y);width=native_points(parent,width);height=native_points(parent,height);flags|=SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_PropertiesID props=SDL_CreateProperties();SDL_SetStringProperty(props,SDL_PROP_WINDOW_CREATE_TITLE_STRING,title);SDL_SetNumberProperty(props,SDL_PROP_WINDOW_CREATE_X_NUMBER,x);SDL_SetNumberProperty(props,SDL_PROP_WINDOW_CREATE_Y_NUMBER,y);SDL_SetNumberProperty(props,SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,width);SDL_SetNumberProperty(props,SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,height);SDL_SetNumberProperty(props,SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,flags);if(parent) SDL_SetPointerProperty(props,SDL_PROP_WINDOW_CREATE_PARENT_POINTER,parent);SDL_Window *native=SDL_CreateWindowWithProperties(props);SDL_DestroyProperties(props);return native;
}
static int popup_backend(void) { const char *driver=SDL_GetCurrentVideoDriver();return driver && strcmp(driver,"dummy") && strcmp(driver,"offscreen"); }
static SDL_Window *create_native_window(Window *w) {
    SDL_WindowFlags flags=SDL_WINDOW_HIDDEN;if((w->style&0x00c00000)!=0x00c00000) flags|=SDL_WINDOW_BORDERLESS;if(w->style&0x00040000) flags|=SDL_WINDOW_RESIZABLE;if(w->exstyle&8) flags|=SDL_WINDOW_ALWAYS_ON_TOP;if(w->exstyle&0x80) flags|=SDL_WINDOW_UTILITY;
    Window *owner=window(w->parent);while(owner && !owner->native) owner=window(owner->parent);
    if(owner) w->parent=owner->hwnd;
    if(owner && (w->style&0x80000000) && (w->style&0x00c00000)!=0x00c00000 && popup_backend()) { int x,y;origin(owner,&x,&y);w->native_popup=1;w->native_parent=owner->hwnd;fprintf(stderr,"WinBox native popup: %s relative to %llx at %d,%d\n",w->class_name,(unsigned long long)owner->hwnd,w->x-x,w->y-y);return create_pixel_popup(owner->native,w->x-x,w->y-y+menu_height(owner),w->w,w->h,SDL_WINDOW_HIDDEN|SDL_WINDOW_POPUP_MENU); }
    while(owner && owner->native_popup) owner=window(owner->native_parent);
    return create_toplevel(w->title,w->x,w->y,w->w,w->h,flags,owner ? owner->native : NULL);
}
static void position_native(Window *w) {
    if(!w->native) return;int x=w->x,y=w->y;if(w->native_popup) { int px,py;origin(window(w->native_parent),&px,&py);x-=px;y-=py-menu_height(window(w->native_parent)); }
    if(!local_coordinates() || w->native_popup) SDL_SetWindowPosition(w->native,native_points(w->native,x),native_points(w->native,y));
}
static char *wide(CPU *c,uint64_t p) {
    if(!p) return strdup("");char *s=malloc(16385);if(!s) return NULL;size_t k=0;
    for(unsigned i=0;i<4096;i++) { uint32_t ch=(uint32_t)load(c,p+2*i,16);if(!ch || c->failed) break;
        if(ch<128) s[k++]=(char)ch;else if(ch<2048) { s[k++]=(char)(0xc0|(ch>>6));s[k++]=(char)(0x80|(ch&63)); }
        else { s[k++]=(char)(0xe0|(ch>>12));s[k++]=(char)(0x80|((ch>>6)&63));s[k++]=(char)(0x80|(ch&63)); }
    }s[k]=0;return s;
}
static char *text(CPU *c,uint64_t p,int unicode) { if(!p) return strdup("");if(p<65536) { char name[40];snprintf(name,sizeof name,"#%llu",(unsigned long long)p);return strdup(name); }if(unicode) return wide(c,p);char *s=guest_string(c,p);return strdup(s ? s : ""); }
/* Cairo errors are sticky: one malformed field must not poison the entire DC. */
static char *display_utf8(const char *text) {
    size_t length=strlen(text);char *out=malloc(length*3+1);if(!out) return NULL;size_t used=0;
    for(size_t i=0;i<length;) {
        const unsigned char *p=(const unsigned char *)text+i;unsigned n=p[0]<0x80 ? 1 : p[0]>=0xc2 && p[0]<=0xdf ? 2 : p[0]>=0xe0 && p[0]<=0xef ? 3 : p[0]>=0xf0 && p[0]<=0xf4 ? 4 : 0;
        int valid=n && i+n<=length;
        for(unsigned k=1;valid && k<n;k++) if((p[k]&0xc0)!=0x80) valid=0;
        if(valid && n>=3 && ((p[0]==0xe0 && p[1]<0xa0) || (p[0]==0xed && p[1]>=0xa0))) valid=0;
        if(valid && n==4 && ((p[0]==0xf0 && p[1]<0x90) || (p[0]==0xf4 && p[1]>=0x90))) valid=0;
        if(valid) { memcpy(out+used,p,n);used+=n;i+=n; }
        else { out[used++]=(char)0xef;out[used++]=(char)0xbf;out[used++]=(char)0xbd;i++; }
    }out[used]=0;return out;
}
static Window *window(uint64_t h) { if(h<WH(0) || (h-WH(0))%16) return NULL;uint64_t i=((h-WH(0))/16)%MAX_WINDOWS;return ui.windows[i].used && ui.windows[i].hwnd==h ? &ui.windows[i] : NULL; }
static uint64_t coordinate_space(Window *w) { unsigned depth=0;while(w && !w->native && depth++<MAX_WINDOWS) w=window(w->parent);while(w && depth++<MAX_WINDOWS && (w->native_popup || (w->parent && (w->style&0x80000000) && !(w->style&0x00c00000)))) w=window(w->native_parent ? w->native_parent : w->parent);return w ? w->hwnd : 0; }
static SDL_Rect work_area(void) { SDL_Rect bounds={0,0,1280,800};if(ui.ready) SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(),&bounds);bounds.x=guest_pixels(NULL,bounds.x);bounds.y=guest_pixels(NULL,bounds.y);bounds.w=guest_pixels(NULL,bounds.w);bounds.h=guest_pixels(NULL,bounds.h);if(local_coordinates()) bounds.x=bounds.y=0;return bounds; }
static Gdi *gdi(uint64_t h) { if(h<GH(0) || (h-GH(0))%16) return NULL;uint64_t i=(h-GH(0))/16;return i<MAX_GDI && ui.gdi[i].used ? &ui.gdi[i] : NULL; }
/* The SDL surface includes the Win32 nonclient menu; guest client coordinates do not. */
static int menu_height(Window *w) { Gdi *g=w ? gdi(w->menu) : NULL;return w && w->native && !(w->style&0x40000000) && g && g->type==5 && g->menu.count ? dpi_units(19) : 0; }
static char *menu_label(const char *text) { char *s=strdup(text ? text : "");if(!s) return NULL;char *out=s;for(const char *p=s;*p && *p!='\t';p++) { if(*p=='&') { if(p[1]!='&') continue;p++; }*out++=*p; }*out=0;return s; }
static int menubar_item(Window *w,cairo_t *cr,int pointer) {
    Gdi *g=gdi(w->menu);if(!menu_height(w) || !g) return -1;
    cairo_save(cr);cairo_scale(cr,dpi_scale,dpi_scale);if(pointer>=0) pointer=(int)lround(pointer/dpi_scale);cairo_select_font_face(cr,"Tahoma",0,0);cairo_set_font_size(cr,11);int x=2;
    for(size_t i=0;i<g->menu.count;i++) { char *label=menu_label(g->menu.items[i].text);if(!label) continue;cairo_text_extents_t ext;cairo_text_extents(cr,label,&ext);int width=(int)ceil(ext.x_advance)+14;
        if(pointer>=x && pointer<x+width) { free(label);cairo_restore(cr);return i; }
        if(pointer<0) { color(cr,0);cairo_move_to(cr,x+7,13);cairo_show_text(cr,label); }free(label);x+=width;
    }cairo_restore(cr);return -1;
}
static uint64_t new_gdi(int type) { for(unsigned i=1;i<MAX_GDI;i++) if(!ui.gdi[i].used) { memset(&ui.gdi[i],0,sizeof(Gdi));ui.gdi[i].used=1;ui.gdi[i].type=type;return GH(i); }return 0; }
static DC *dc(uint64_t h) { if(h<0x70000000 || (h-0x70000000)%16) return NULL;uint64_t i=(h-0x70000000)/16;return i<MAX_GDI && ui.dc[i].used ? &ui.dc[i] : NULL; }
static void color(cairo_t *cr,uint32_t value) { cairo_set_source_rgb(cr,(value&255)/255.,((value>>8)&255)/255.,((value>>16)&255)/255.); }
static uint32_t syscolor(unsigned i) { switch(i) { case 0:return 0xcbcbcb;case 1:return 0;case 2:return 0xb47800;case 3:return 0xbfbfbf;case 4:return 0xdcdcdc;case 5:return 0xffffff;case 6:case 7:case 8:case 18:return 0;case 9:return 0xffffff;case 12:return 0xb4b4b4;case 13:return 0xd77800;case 14:return 0xffffff;case 15:return 0xe2e2e2;case 16:return 0xb4b4b4;case 17:return 0x6d6d6d;case 20:return 0xffffff;case 21:return 0;case 22:return 0xdcdcdc;default:return 0xe2e2e2; } }
static void enqueue(uint64_t h,uint32_t msg,uint64_t wp,uint64_t lp) {
    Window *w=window(h);if(msg==0xf && (!w || w->paint_pending)) return;
    if(msg==5 || msg==3 || msg==0x20 || msg==0x200 || msg==0x113) for(unsigned i=ui.head;i!=ui.tail;i=(i+1)%MAX_MSG) if(ui.messages[i].hwnd==h && ui.messages[i].message==msg && (msg!=0x113 || ui.messages[i].wp==wp)) { ui.messages[i].wp=wp;ui.messages[i].lp=lp;if(msg==0x200) { ui.messages[i].modifiers=ui.event_modifiers;ui.messages[i].buttons=ui.input_buttons;ui.messages[i].cursor_x=ui.cursor_x;ui.messages[i].cursor_y=ui.cursor_y; }return; }
    unsigned n=(ui.tail+1)%MAX_MSG;if(n==ui.head) return;if(msg==0xf) w->paint_pending=1;
    ui.messages[ui.tail]=(Message){h,msg,wp,lp,ui.event_modifiers,ui.input_buttons,ui.cursor_x,ui.cursor_y};ui.tail=n;
}
static uint64_t edit_proc(CPU *c,Window *w,uint32_t msg,uint64_t wp,uint64_t lp) {
    if(!w) return 0;size_t length=strlen(w->title);
    if(msg==0x81) return 1;
    if(msg==0x87) return 0x89;
    if(msg==0xc) { char *s=text(c,lp,0);free(w->title);w->title=s;w->selection_start=w->selection_end=strlen(s);enqueue(w->hwnd,0xf,0,0);return 1; }
    if(msg==0xe) return length;
    if(msg==0xd) { if(!wp) return 0;size_t n=length<wp ? length : wp-1;uint8_t *p=writable_memory(c,lp,n+1);if(p) { memcpy(p,w->title,n);p[n]=0; }return n; }
    if(msg==0xb1) { int a=(int)wp,b=(int)lp;if(a<0) a=length;if(b<0) b=length;if(a>(int)length) a=length;if(b>(int)length) b=length;w->selection_start=a;w->selection_end=b;return 0; }
    if(msg==0xb0) { if(wp) store(c,wp,32,w->selection_start);if(lp) store(c,lp,32,w->selection_end);return (uint16_t)w->selection_start|((uint32_t)(uint16_t)w->selection_end<<16); }
    if(msg==0xba) return 1;
    int changed=0;
    if(msg==0x100) { int cursor=w->selection_end;if(wp==37 && cursor>0) cursor--;if(wp==39 && cursor<(int)length) cursor++;if(wp==36) cursor=0;if(wp==35) cursor=length;if(wp==37 || wp==39 || wp==36 || wp==35) w->selection_start=w->selection_end=cursor;if(wp==46 && cursor<(int)length) { memmove(w->title+cursor,w->title+cursor+1,length-cursor);changed=1; } }
    if(msg==0x102) {
        int a=w->selection_start,b=w->selection_end;if(a>b) { int t=a;a=b;b=t; }if(a<0) a=0;if(b>(int)length) b=length;
        if(wp==1) { w->selection_start=0;w->selection_end=length; }
        else if(wp==8) { if(a==b && a>0) a--;if(b>a) { memmove(w->title+a,w->title+b,length-b+1);w->selection_start=w->selection_end=a;changed=1; } }
        else if(wp>=32 && wp<256 && length<16384) { char *s=malloc(length+2);if(s) { memcpy(s,w->title,a);s[a]=(char)wp;memcpy(s+a+1,w->title+b,length-b+1);free(w->title);w->title=s;w->selection_start=w->selection_end=a+1;changed=1; } }
    }
    if(changed) { const char *test=getenv("WINBOX_TEST_INPUT");if(test && !strcmp(w->title,test)) ui.test_input_observed=1;enqueue(w->parent,0x111,((uint32_t)0x300<<16)|(w->menu&0xffff),w->hwnd);enqueue(w->hwnd,0xf,0,0); }
    if(msg==0xf && w->surface) { cairo_t *cr=cairo_create(w->surface);cairo_set_source_rgb(cr,1,1,1);cairo_paint(cr);cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);cairo_set_font_size(cr,12);cairo_set_source_rgb(cr,0,0,0);cairo_move_to(cr,2,14);char *display=w->title,*masked=NULL;if(w->style&0x20) { masked=malloc(length+1);if(masked) { memset(masked,'*',length);masked[length]=0;display=masked; } }cairo_show_text(cr,display);free(masked);cairo_destroy(cr); }
    return 0;
}
static uint64_t invoke(CPU *c,Window *w,uint32_t msg,uint64_t wp,uint64_t lp) { if(!w || !w->proc) return msg==0x81 ? 1 : 0;uint64_t hwnd=w->hwnd,ret=0;if(msg==0xf) w->painting++;if(w->proc==EDIT_PROC) ret=edit_proc(c,w,msg,wp,lp);else { uint64_t args[]={hwnd,msg,wp,lp};if(callback(c,w->proc,args,4)) ret=c->r[RAX]; }if(msg==0xf && window(hwnd)==w && w->painting) w->painting--;return ret; }
static char *iconv_latin(const char *to,const char *from,const char *input,size_t length) {
    char *out=SDL_iconv_string(to,from,input,length);
    if(!out) {
        if(!strcmp(to,"WINDOWS-1252")) out=SDL_iconv_string("ISO-8859-1",from,input,length);
        if(!out && !strcmp(from,"WINDOWS-1252")) out=SDL_iconv_string(to,"ISO-8859-1",input,length);
    }
    return out;
}
#include "ui_theme.inc"
#include "ui_wallpaper.inc"
#include "ui_theme_cache.inc"
#include "ui_icons.inc"
static int video(CPU *c) { theme_init();if(ui.ready) return 1;
    const char *legacy_driver=getenv("SDL_VIDEODRIVER");if(legacy_driver && !getenv("SDL_VIDEO_DRIVER")) SDL_SetHint(SDL_HINT_VIDEO_DRIVER,legacy_driver);
    if(!legacy_driver && !getenv("SDL_VIDEO_DRIVER") && getenv("WAYLAND_DISPLAY")) SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"wayland,x11");
    if(!SDL_InitSubSystem(SDL_INIT_VIDEO)) { trap(c,SDL_GetError());return 0; }ui.ready=1;dpi_initialize();ui.started=SDL_GetTicks();fprintf(stderr,"WinBox video backend: %s\n",SDL_GetCurrentVideoDriver());return 1; }
static cairo_surface_t *surface(int w,int h) { if(w<1) w=1;if(h<1) h=1;if(w>8192 || h>8192) return NULL;cairo_surface_t *s=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);cairo_t *cr=cairo_create(s);cairo_set_source_rgb(cr,.94,.94,.94);cairo_paint(cr);cairo_destroy(cr);return s; }
static void font(DC *d) { Gdi *f=gdi(d->font);const char *family=f && *f->face ? f->face : "Tahoma";if(!strcmp(family,"MS Shell Dlg")) family="Tahoma";if(f && !*f->face && ((f->pitch&3)==1 || f->charset==255)) family="Courier New";cairo_select_font_face(d->cr,family,f && f->italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL,f && f->weight>=600 ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);cairo_set_font_size(d->cr,f && f->size ? abs(f->size) : 12); }
static unsigned font_be16(const unsigned char *p) { return ((unsigned)p[0]<<8)|p[1]; }
/* GDI uses VDMX vertical metrics and OS/2 average width, rather than Cairo's
 * line spacing or the mean advance of the Latin alphabet. Cache per HFONT. */
static FontMetrics font_metrics(DC *d) {
    Gdi *g=gdi(d->font);if(g && g->metric_valid) return g->metrics;
    cairo_font_extents_t e;cairo_font_extents(d->cr,&e);cairo_text_extents_t a;cairo_text_extents(d->cr,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",&a);
    int size=g && g->size ? abs(g->size) : 12;FontMetrics m={(int)ceil(e.height),(int)ceil(e.ascent),0,0,0,(int)ceil(a.x_advance/52),(int)ceil(e.max_x_advance)};m.descent=m.height-m.ascent;
    cairo_scaled_font_t *scaled=cairo_get_scaled_font(d->cr);FT_Face face=cairo_scaled_font_get_type(scaled)==CAIRO_FONT_TYPE_FT ? cairo_ft_scaled_font_lock_face(scaled) : NULL;
    if(face) {
        if(face->units_per_EM) { double scale=(double)size/face->units_per_EM;TT_OS2 *os2=FT_Get_Sfnt_Table(face,ft_sfnt_os2);TT_HoriHeader *hhea=FT_Get_Sfnt_Table(face,ft_sfnt_hhea);
            if(os2) { m.ascent=lround(os2->usWinAscent*scale);m.descent=lround(os2->usWinDescent*scale);if(os2->xAvgCharWidth>0) m.average=lround(os2->xAvgCharWidth*scale); }
            m.maximum=lround((face->bbox.xMax-face->bbox.xMin)*scale);if(hhea) m.external=lround(hhea->Line_Gap*scale);
        }
        FT_ULong bytes=0;FT_ULong tag=FT_MAKE_TAG('V','D','M','X');
        if(!FT_Load_Sfnt_Table(face,tag,0,NULL,&bytes) && bytes>=6 && bytes<=1024*1024) { unsigned char *table=malloc(bytes);
            if(table && !FT_Load_Sfnt_Table(face,tag,0,table,&bytes)) { unsigned ratios=font_be16(table+4);
                if(ratios<=1024 && 6u+6u*ratios<=bytes) for(unsigned i=0;i<ratios;i++) { unsigned char *ratio=table+6+4*i;if(ratio[1]!=1 || ratio[2]>1 || ratio[3]<1) continue;unsigned offset=font_be16(table+6+4*ratios+2*i);if(offset+4>bytes) continue;unsigned entries=font_be16(table+offset);if(offset+4u+6u*entries>bytes) continue;
                    for(unsigned j=0;j<entries;j++) { unsigned char *entry=table+offset+4+6*j;if(font_be16(entry)==(unsigned)size) { m.ascent=(int16_t)font_be16(entry+2);m.descent=-(int16_t)font_be16(entry+4);break; } }break;
                }
            }free(table);
        }
        cairo_ft_scaled_font_unlock_face(scaled);
    }
    cairo_text_extents_t thin,wide;cairo_text_extents(d->cr,"i",&thin);cairo_text_extents(d->cr,"W",&wide);if(fabs(thin.x_advance-wide.x_advance)<0.01) m.average=(int)ceil(wide.x_advance);m.height=m.ascent+m.descent;m.internal=m.height>size ? m.height-size : 0;if(m.average<1) m.average=1;if(m.external<0) m.external=0;if(g) { g->metrics=m;g->metric_valid=1; }return m;
}

static void discard_dc_states(DC *d) { while(d->saved) { DCState *state=d->saved;d->saved=state->next;cairo_destroy(state->cr);free(state); }d->save_depth=0; }
static void resize_canvas(Window *w,int width,int height) {
    cairo_surface_t *next=surface(width,height);if(!next) return;cairo_surface_t *old=w->surface;
    if(old) { cairo_t *copy=cairo_create(next);int oldw=cairo_image_surface_get_width(old),oldh=cairo_image_surface_get_height(old);cairo_rectangle(copy,0,0,width<oldw ? width : oldw,height<oldh ? height : oldh);cairo_clip(copy);cairo_set_operator(copy,CAIRO_OPERATOR_SOURCE);cairo_set_source_surface(copy,old,0,0);cairo_paint(copy);cairo_destroy(copy); }
    w->w=width;w->h=height;w->surface=next;Window *root=w;for(unsigned depth=0;root && !root->native && depth<MAX_WINDOWS;depth++) root=window(root->parent);if(root) root->resize_pending=1;
    for(unsigned i=1;i<MAX_GDI;i++) if(ui.dc[i].used && ui.dc[i].surface==old) { DC *d=&ui.dc[i];cairo_matrix_t matrix;cairo_get_matrix(d->cr,&matrix);discard_dc_states(d);cairo_destroy(d->cr);d->surface=next;d->cr=cairo_create(next);cairo_set_matrix(d->cr,&matrix);font(d); }
    if(old) cairo_surface_destroy(old);
}
static uint64_t new_dc(uint64_t h,cairo_surface_t *s) { for(unsigned i=1;i<MAX_GDI;i++) if(!ui.dc[i].used) { DC *d=&ui.dc[i];memset(d,0,sizeof *d);d->used=1;d->window=h;d->surface=s;d->cr=cairo_create(s);d->bg=0xffffff;d->bgmode=1;
        static uint64_t default_font,default_pen,default_brush,default_bitmap;
        if(!default_font) { default_font=new_gdi(3);Gdi *f=gdi(default_font);f->size=-dpi_units(11);f->weight=400;strcpy(f->face,"MS Shell Dlg");default_pen=new_gdi(2);gdi(default_pen)->width=1;default_brush=new_gdi(1);gdi(default_brush)->color=0xffffff;default_bitmap=new_gdi(4);gdi(default_bitmap)->surface=surface(1,1); }
        d->font=default_font;d->pen=default_pen;d->brush=default_brush;d->bitmap=default_bitmap;font(d);return 0x70000000+i*16; }return 0; }
#include "ui_feedback.inc"
static Window *hit(Window *,int *,int *);
static uint64_t zserial=MAX_WINDOWS;
static uint64_t window_z(Window *w) { return w->zorder ? w->zorder : (uint64_t)(w-ui.windows); }
static int compare_window_z(const void *a,const void *b) { uint64_t x=window_z(*(Window *const *)a),y=window_z(*(Window *const *)b);return (x>y)-(x<y); }
static unsigned child_order(Window *parent,Window **ordered) {
    unsigned n=0;for(unsigned i=1;i<MAX_WINDOWS;i++) { Window *w=&ui.windows[i];if(w->used && !w->native && w->parent==parent->hwnd) ordered[n++]=w; }qsort(ordered,n,sizeof *ordered,compare_window_z);return n;
}
static int position_zorder(Window *w,uint64_t after,unsigned flags) {
    if(!w || (flags&4)) return 0;
    if(w->native) { if(after!=1 && !(flags&16)) SDL_RaiseWindow(w->native);return 0; }
    Window *parent=window(w->parent);if(!parent) return 0;Window *ordered[MAX_WINDOWS];unsigned n=child_order(parent,ordered),old=n;
    for(unsigned i=0;i<n;i++) if(ordered[i]==w) { old=i;memmove(ordered+i,ordered+i+1,(n-i-1)*sizeof *ordered);n--;break; }
    if(old>n) return 0;unsigned at=n;
    if(after==1) at=0;
    else if(after && after!=UINT64_MAX && after!=UINT64_MAX-1) { if(after==w->hwnd) return 0;int found=0;for(unsigned i=0;i<n;i++) if(ordered[i]->hwnd==after) { at=i;found=1;break; }if(!found) return 0; }
    memmove(ordered+at+1,ordered+at,(n-at)*sizeof *ordered);ordered[at]=w;
    for(unsigned i=0;i<=n;i++) ordered[i]->zorder=++zserial;
    if(at!=old) request_present(w);return at!=old;
}
static void raise_mdi(Window *w) {
    for(Window *p=w;p && !p->native;p=window(p->parent)) if(p->title && *p->title && !strcmp(p->class_name,"routeros_dbl_canvas")) position_zorder(p,0,0);
}
static void composite(cairo_t *cr,Window *w,int x,int y) { cairo_save(cr);cairo_rectangle(cr,x,y,w->w,w->h);cairo_clip(cr);theme_init();cairo_surface_t *layer=w->surface;int temporary=0;if(theme.dark) { if(theme_cache_enabled) layer=theme_cached_layer(w);else layer=NULL;if(!layer) { layer=theme_copy_region(w->surface,0,0,w->w,w->h);theme_apply(layer,w);temporary=1; } }cairo_set_source_surface(cr,layer,x,y);cairo_paint(cr);if(temporary) cairo_surface_destroy(layer);wallpaper_paint(cr,w,x,y);Window *ordered[MAX_WINDOWS];unsigned count=child_order(w,ordered);for(unsigned i=0;i<count;i++) { Window *child=ordered[i];if(child->visible) composite(cr,child,x+child->x,y+child->y); }cairo_restore(cr); }
#ifdef WINBOX_UI_TESTS
/* Offline overlapping MDI fixture: style each backing surface before z-composition. */
int ui_theme_composition_test(void) {
    theme.initialized=1;theme.dark=1;
    Window *root=&ui.windows[1],*back=&ui.windows[2],*front=&ui.windows[3];
    *root=(Window){.used=1,.hwnd=WH(1),.w=320,.h=240,.class_name="fixture",.title="",.surface=surface(320,240)};
    *back=(Window){.used=1,.visible=1,.hwnd=WH(2),.parent=WH(1),.x=80,.y=60,.w=200,.h=150,.class_name="routeros_dbl_canvas",.title="Interfaces",.surface=surface(200,150)};
    *front=(Window){.used=1,.visible=1,.hwnd=WH(3),.parent=WH(1),.x=20,.y=20,.w=220,.h=180,.class_name="routeros_dbl_canvas",.title="Terminal <1>",.surface=surface(220,180)};
    for(unsigned i=1;i<=3;i++) { Window *w=&ui.windows[i];cairo_t *c=cairo_create(w->surface);cairo_set_source_rgb(c,1,1,1);cairo_paint(c);if(i>1) { cairo_set_source_rgb(c,.3,.3,.8);cairo_rectangle(c,2,2,w->w-4,18);cairo_fill(c); }cairo_destroy(c); }
    cairo_surface_t *image=surface(320,240);cairo_t *cr=cairo_create(image);composite(cr,root,0,0);cairo_destroy(cr);cairo_surface_flush(image);
    unsigned char *data=cairo_image_surface_get_data(image);int stride=cairo_image_surface_get_stride(image),ok=1;
    for(int y=45;y<195;y++) for(int x=25;x<235;x++) { uint32_t pixel;memcpy(&pixel,data+y*stride+x*4,4);if(pixel!=theme_pixel(0xffffffff)) ok=0; }
    /* Moving the foreground must reveal the background's original blue caption. */
    front->x=0;front->y=120;cr=cairo_create(image);composite(cr,root,0,0);cairo_destroy(cr);cairo_surface_flush(image);uint32_t pixel;memcpy(&pixel,data+65*stride+100*4,4);if(!theme_blue(pixel)) ok=0;
    cairo_surface_flush(front->surface);memcpy(&pixel,cairo_image_surface_get_data(front->surface)+40*cairo_image_surface_get_stride(front->surface)+40*4,4);if(pixel!=0xffffffff) ok=0;
    uint64_t builds=theme_cache_builds;cr=cairo_create(image);composite(cr,root,0,0);cairo_destroy(cr);if(theme_cache_builds!=builds) ok=0;
    cairo_t *persistent=cairo_create(back->surface);cairo_set_source_rgb(persistent,0,0,0);cairo_rectangle(persistent,40,80,10,10);cairo_fill(persistent);
    cr=cairo_create(image);composite(cr,root,0,0);cairo_destroy(cr);if(theme_cache_builds!=builds+1) ok=0;
    cairo_set_source_rgb(persistent,1,1,1);cairo_rectangle(persistent,40,80,10,10);cairo_fill(persistent);cairo_destroy(persistent);
    cr=cairo_create(image);composite(cr,root,0,0);cairo_destroy(cr);if(theme_cache_builds!=builds+2) ok=0;
    int hx=100,hy=130;if(hit(root,&hx,&hy)!=front) ok=0;
    position_zorder(back,0,4);hx=100;hy=130;if(hit(root,&hx,&hy)!=front) ok=0;
    raise_mdi(back);hx=100;hy=130;if(hit(root,&hx,&hy)!=back) ok=0;
    cr=cairo_create(image);composite(cr,root,0,0);cairo_destroy(cr);cairo_surface_flush(image);memcpy(&pixel,data+125*stride+100*4,4);if(pixel!=theme_pixel(0xffffffff)) ok=0;
    position_zorder(back,1,0);hx=100;hy=130;if(hit(root,&hx,&hy)!=front) ok=0;
    position_zorder(front,back->hwnd,0);hx=100;hy=130;if(hit(root,&hx,&hy)!=back) ok=0;
    cairo_surface_destroy(image);for(unsigned i=1;i<=3;i++) { cairo_surface_destroy(ui.windows[i].surface);memset(&ui.windows[i],0,sizeof(Window)); }return ok;
}
int ui_wallpaper_composition_test(const char *path) {
    if(!wallpaper_set(path)) return 0;theme.initialized=1;theme.dark=1;
    Window *root=&ui.windows[1],*workspace=&ui.windows[2],*front=&ui.windows[3];
    *root=(Window){.used=1,.hwnd=WH(1),.w=240,.h=160,.class_name="routeros_main",.title="",.surface=surface(240,160)};
    *workspace=(Window){.used=1,.visible=1,.hwnd=WH(2),.parent=WH(1),.x=40,.y=20,.w=200,.h=140,.class_name="routeros_dbl_canvas",.title="",.surface=surface(200,140)};
    *front=(Window){.used=1,.visible=1,.hwnd=WH(3),.parent=WH(2),.x=60,.y=40,.w=80,.h=60,.class_name="routeros_dbl_canvas",.title="Interfaces",.surface=surface(80,60)};
    cairo_t *fill=cairo_create(front->surface);cairo_set_source_rgb(fill,1,1,1);cairo_paint(fill);cairo_destroy(fill);
    cairo_surface_t *image=surface(240,160);cairo_t *cr=cairo_create(image);composite(cr,root,0,0);cairo_destroy(cr);cairo_surface_flush(image);unsigned char *data=cairo_image_surface_get_data(image);int stride=cairo_image_surface_get_stride(image);uint32_t background,client,sidebar;memcpy(&background,data+40*stride+60*4,4);memcpy(&client,data+100*stride+130*4,4);memcpy(&sidebar,data+40*stride+20*4,4);
    int ok=background==0xff2c58a6 && client==theme_pixel(0xffffffff) && sidebar!=background;
    theme.dark=0;cr=cairo_create(image);composite(cr,root,0,0);cairo_destroy(cr);cairo_surface_flush(image);memcpy(&client,data+40*stride+60*4,4);if(client!=background) ok=0;
    /* Force a fresh load, as on restart; invalid selections preserve this image. */
    cairo_surface_destroy(wallpaper.image);wallpaper.image=NULL;wallpaper.initialized=0;wallpaper_init();if(!wallpaper.image || wallpaper_set("/nonexistent/wallpaper.jpg")) ok=0;
    if(!wallpaper_set(NULL) || wallpaper.image) ok=0;
    cairo_surface_destroy(image);for(unsigned i=1;i<=3;i++) { cairo_surface_destroy(ui.windows[i].surface);memset(&ui.windows[i],0,sizeof(Window)); }return ok;
}
int ui_dpi_mapping_test(void) {
    double old_density=dpi_density,old_scale=dpi_scale;int ok=1;double scales[]={1,1.25,1.5,2,3};
    for(unsigned i=0;i<sizeof scales/sizeof *scales;i++) { dpi_density=dpi_scale=scales[i];if(dpi_units(96)!=(int)lround(96*scales[i])) ok=0;for(int value=-3000;value<=3000;value+=17) { int points=native_points(NULL,value),pixels=guest_pixels(NULL,points);if(abs(pixels-value)>(int)ceil(scales[i]/2)) ok=0; } }
    dpi_density=old_density;dpi_scale=old_scale;return ok;
}
int ui_drag_render_benchmark(void) {
    theme.initialized=1;theme.dark=1;Window *root=&ui.windows[1],*panel=&ui.windows[2];
    *root=(Window){.used=1,.hwnd=WH(1),.w=1280,.h=900,.class_name="fixture",.title="",.surface=surface(1280,900)};
    *panel=(Window){.used=1,.visible=1,.hwnd=WH(2),.parent=WH(1),.x=120,.y=80,.w=900,.h=700,.class_name="routeros_dbl_canvas",.title="Fields",.surface=surface(900,700)};
    for(unsigned i=3;i<203;i++) { Window *w=&ui.windows[i];*w=(Window){.used=1,.visible=1,.hwnd=WH(i),.parent=WH(2),.x=8+(int)((i-3)%4)*220,.y=24+(int)((i-3)/4)*13,.w=210,.h=12,.class_name="EDIT",.title="",.surface=surface(210,12)}; }
    cairo_surface_t *frame=surface(1280,900);double elapsed[2];uint64_t hashes[2],builds=0;
    for(int mode=0;mode<2;mode++) { theme_cache_enabled=mode;cairo_t *cr=cairo_create(frame);composite(cr,root,0,0);cairo_destroy(cr);if(mode) builds=theme_cache_builds;
        uint64_t start=SDL_GetPerformanceCounter();for(unsigned i=0;i<40;i++) { panel->x=120+i%30;panel->y=80+i%20;cr=cairo_create(frame);composite(cr,root,0,0);cairo_destroy(cr); }elapsed[mode]=(SDL_GetPerformanceCounter()-start)*1000./SDL_GetPerformanceFrequency()/40;hashes[mode]=theme_surface_hash(frame);
    }
    int ok=hashes[0]==hashes[1] && builds==theme_cache_builds;
    fprintf(stderr,"200-field drag renderer: %.2f ms/frame before, %.2f ms/frame cached (%.1fx); unchanged layers rebuilt: %llu\n",elapsed[0],elapsed[1],elapsed[0]/elapsed[1],(unsigned long long)(theme_cache_builds-builds));
    theme_cache_enabled=1;cairo_surface_destroy(frame);for(unsigned i=1;i<203;i++) { cairo_surface_destroy(ui.windows[i].surface);memset(&ui.windows[i],0,sizeof(Window)); }return ok;
}
#endif
static void present(Window *w) { if(!w) return;while(!w->native && w->parent && window(w->parent)) w=window(w->parent);if(!w->native || !w->visible) return;uint64_t frame_started=SDL_GetTicks();
    if(frame_unready(w)) { w->last_present=SDL_GetTicks();if(w->frame) upload_frame(w,w->frame);return; }
    if(!w->frame || cairo_image_surface_get_width(w->frame)!=w->w || cairo_image_surface_get_height(w->frame)!=w->h) { if(w->frame) cairo_surface_destroy(w->frame);w->frame=surface(w->w,w->h); }
    cairo_surface_t *s=w->frame;if(!s) return;cairo_t *cr=cairo_create(s);int bar=menu_height(w);if(bar) { color(cr,0xdcdcdc);cairo_paint(cr); }composite(cr,w,0,bar);if(bar) { cairo_surface_t *menu=surface(w->w,bar);cairo_t *mc=cairo_create(menu);color(mc,0xdcdcdc);cairo_paint(mc);menubar_item(w,mc,-1);cairo_destroy(mc);theme_apply(menu,NULL);cairo_set_source_surface(cr,menu,0,0);cairo_paint(cr);cairo_surface_destroy(menu); }theme_control_paint(cr,w);paint_caret(cr,w);cairo_destroy(cr);cairo_surface_flush(s);w->resize_pending=0;w->frame_dirty=0;w->last_present=frame_started;
    if(!upload_frame(w,s)) w->frame_dirty=1;
    if(getenv("WINBOX_TRACE_FRAMES")) fprintf(stderr,"WinBox frame: %llu ms\n",(unsigned long long)(SDL_GetTicks()-frame_started));
    const char *frames=getenv("WINBOX_TEST_FRAME_DIR");if(frames && !strcmp(w->class_name,"routeros_connect")) { static unsigned serial;char path[4096];snprintf(path,sizeof path,"%s/frame-%04u.png",frames,serial++);cairo_surface_write_to_png(s,path); }
    if(ui.capture_screenshot && getenv("WINBOX_TEST_WINDOW_TREE")) for(unsigned i=1;i<MAX_WINDOWS;i++) { Window *p=&ui.windows[i];if(p->used && p->visible) fprintf(stderr,"TREE %llx parent=%llx %s %d,%d %dx%d title=%s\n",(unsigned long long)p->hwnd,(unsigned long long)p->parent,p->class_name,p->x,p->y,p->w,p->h,p->title); }
    const char *shot=getenv("WINBOX_SCREENSHOT");if(shot && ui.capture_screenshot && w->visible) cairo_surface_write_to_png(s,shot);
}
static void request_present(Window *w) { if(!w) return;while(!w->native && w->parent && window(w->parent)) w=window(w->parent);if(w->native) w->frame_dirty=1; }
static void flush_frames(void) { uint64_t now=SDL_GetTicks();for(unsigned i=1;i<MAX_WINDOWS;i++) { Window *w=&ui.windows[i];if(w->used && w->native && w->visible && w->frame_dirty && now-w->last_present>=16) present(w); } }
static void rectangle(CPU *c,uint64_t p,int x,int y,int r,int b) { store(c,p,32,x);store(c,p+4,32,y);store(c,p+8,32,r);store(c,p+12,32,b); }
static void notify_position(CPU *c,Window *w,unsigned flags) {
    uint64_t position=guest_alloc(c,40);if(!position) return;
    store(c,position,64,w->hwnd);store(c,position+16,32,w->x);store(c,position+20,32,w->y);store(c,position+24,32,w->w);store(c,position+28,32,w->h);store(c,position+32,32,flags);
    invoke(c,w,0x47,0,position);guest_free(c,position);
}
static unsigned position_geometry(Window *w,int x,int y,int width,int height,unsigned flags) {
    unsigned changes=0;
    if(!(flags&2) && (w->x!=x || w->y!=y)) { w->x=x;w->y=y;position_native(w);changes|=1; }
    if(!(flags&1) && width>0 && height>0 && width<=8192 && height<=8192 && (width!=w->w || height!=w->h)) { resize_canvas(w,width,height);if(w->native) set_pixel_size(w->native,width,height);changes|=2; }
    if(flags&0x40) { if(!w->visible) changes|=4;w->visible=1;if(w->native) SDL_ShowWindow(w->native); }
    if(flags&0x80) { if(w->visible) changes|=8;w->visible=0;if(w->native) SDL_HideWindow(w->native); }
    return changes;
}
static void position_notify(CPU *c,Window *w,unsigned flags,unsigned changes) {
    if(!changes) return;notify_position(c,w,flags);if(changes&2) enqueue(w->hwnd,5,0,(uint16_t)w->w|((uint32_t)(uint16_t)(w->h-menu_height(w))<<16));
    if(changes&(2|4)) enqueue(w->hwnd,0xf,0,0);request_present(w);
}
static void destroy_window(CPU *c,Window *w) {
    if(!w || w->destroying) return;if(ui.caret.owner==w->hwnd) caret_destroy();w->destroying=1;uint64_t hwnd=w->hwnd;
    invoke(c,w,2,0,0);
    for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].parent==hwnd) destroy_window(c,&ui.windows[i]);
    invoke(c,w,0x82,0,0);
    for(unsigned i=0;i<256;i++) if(timers[i].hwnd==hwnd) memset(&timers[i],0,sizeof timers[i]);
    for(unsigned i=0;i<4096;i++) if(properties[i].hwnd==hwnd) { free(properties[i].name);memset(&properties[i],0,sizeof properties[i]); }
    for(unsigned i=1;i<MAX_GDI;i++) if(ui.dc[i].used && ui.dc[i].window==hwnd) { discard_dc_states(&ui.dc[i]);cairo_destroy(ui.dc[i].cr);memset(&ui.dc[i],0,sizeof ui.dc[i]); }
    if(w->native) SDL_DestroyWindow(w->native);if(w->surface) cairo_surface_destroy(w->surface);if(w->frame) cairo_surface_destroy(w->frame);
    if(ui.focus==hwnd) ui.focus=0;if(ui.capture==hwnd) ui.capture=0;if(ui.active==hwnd) ui.active=0;
    free(w->title);free(w->class_name);memset(w,0,sizeof *w);w->hwnd=hwnd;
}
static Menu *menu(uint64_t handle) { Gdi *g=gdi(handle);return g && g->type==5 ? &g->menu : NULL; }
static MenuItem *menu_item(Menu *m,uint64_t key,int position) {
    if(!m) return NULL;if(position) return key<m->count ? &m->items[key] : NULL;
    for(size_t i=0;i<m->count;i++) { if(m->items[i].id==key) return &m->items[i];MenuItem *child=menu_item(menu(m->items[i].submenu),key,0);if(child) return child; }return NULL;
}
static void menu_info(CPU *c,MenuItem *item,uint64_t info) {
    uint32_t mask=load(c,info+4,32);
    if(mask&0x100) item->type=load(c,info+8,32);
    if(mask&1) item->state=load(c,info+12,32);
    if(mask&2) item->id=load(c,info+16,32);
    if(mask&4) item->submenu=load(c,info+24,64);
    if(mask&0x20) item->data=load(c,info+48,64);
    if(mask&(0x40|0x10)) { if(mask&0x10) item->type=load(c,info+8,32);if(!(item->type&0x800)) { char *value=text(c,load(c,info+56,64),0);free(item->text);item->text=value; } }
}
static uint32_t popup_menu(CPU *c,uint64_t handle,uint64_t owner,int x,int y,unsigned depth,SDL_Window *parent_native,int parent_x,int parent_y) {
    Menu *m=menu(handle);if(getenv("WINBOX_TEST_MENU_SELECT")) fprintf(stderr,"WinBox smoke popup requested: %zu items\n",m ? m->count : 0);if(!m || !m->count || depth>16 || !video(c)) return 0;
    invoke(c,window(owner),0x117,handle,0);wallpaper_settings(handle); /* WM_INITMENUPOPUP may update labels/state. */
    int width=140,height=0;size_t count=m->count;
    cairo_surface_t *measure=surface(1,1);cairo_t *cr=cairo_create(measure);cairo_select_font_face(cr,"Tahoma",0,0);cairo_set_font_size(cr,11);
    for(size_t i=0;i<count;i++) { char *label=menu_label(m->items[i].text);const char *tab=m->items[i].text ? strchr(m->items[i].text,'\t') : NULL;cairo_text_extents_t ext,shortcut;cairo_text_extents(cr,label ? label : "",&ext);cairo_text_extents(cr,tab ? tab+1 : "",&shortcut);free(label);double needed=ext.x_advance+shortcut.x_advance+(tab ? 32 : 0)+48;if(needed>width) width=needed;height+=(m->items[i].type&0x800) ? 8 : 20; }
    cairo_destroy(cr);cairo_surface_destroy(measure);if(width>1024) width=1024;if(height>2048) height=2048;
    SDL_Rect bounds=work_area();int pixel_width=dpi_units(width),pixel_height=dpi_units(height);if(x+pixel_width>bounds.x+bounds.w) x=bounds.x+bounds.w-pixel_width;if(y+pixel_height>bounds.y+bounds.h) y=bounds.y+bounds.h-pixel_height;if(y<bounds.y) y=bounds.y;
    Window *popup_owner=window(owner);while(popup_owner && !popup_owner->native) popup_owner=window(popup_owner->parent);SDL_Window *native=NULL;if(parent_native && popup_backend()) native=create_pixel_popup(parent_native,x-parent_x,y-parent_y,pixel_width,pixel_height,SDL_WINDOW_POPUP_MENU);else if(popup_owner && popup_backend()) { int px,py;origin(popup_owner,&px,&py);native=create_pixel_popup(popup_owner->native,x-px,y-py+menu_height(popup_owner),pixel_width,pixel_height,SDL_WINDOW_POPUP_MENU); }else native=create_toplevel("WinBox menu",x,y,pixel_width,pixel_height,SDL_WINDOW_BORDERLESS|SDL_WINDOW_ALWAYS_ON_TOP,NULL);if(!native) return 0;
    cairo_surface_t *canvas=surface(pixel_width,pixel_height);cr=cairo_create(canvas);cairo_scale(cr,dpi_scale,dpi_scale);int selected=-1,done=0;uint32_t command=0;uint32_t opening_buttons=ui.input_buttons;int pressed=-1;
    const char *choice=getenv("WINBOX_TEST_MENU_SELECT");char choice_part[256];if(choice) { const char *part=choice;for(unsigned level=0;level<depth;level++) { const char *next=strchr(part,'/');if(!next) break;part=next+1; }size_t length=strcspn(part,"/");if(length>=sizeof choice_part) length=sizeof choice_part-1;memcpy(choice_part,part,length);choice_part[length]=0;choice=choice_part; }
    if(choice) {
        int steps=0,found=0;for(size_t i=0;i<count;i++) { MenuItem *item=&m->items[i];if(item->type&0x800 || item->state&3) continue;steps++;char label[256];size_t n=0;for(const char *p=item->text ? item->text : "";*p && *p!='\t' && n<255;p++) if(*p!='&') label[n++]=*p;label[n]=0;if(!strcmp(label,choice)) { found=1;break; } }
        const char *method=getenv("WINBOX_TEST_MENU_METHOD");if(found && method && !strcmp(method,"mouse")) { int item_y=0,left=steps;for(size_t i=0;i<count;i++) { MenuItem *item=&m->items[i];int row=item->type&0x800 ? 8 : 20;if(!(item->type&0x800) && !(item->state&3) && !--left) { item_y+=row/2;break; }item_y+=row; }
            SDL_Event click={0};click.type=SDL_EVENT_MOUSE_BUTTON_UP;click.button.button=SDL_BUTTON_LEFT;click.button.windowID=popup_owner ? SDL_GetWindowID(popup_owner->native) : 0;SDL_PushEvent(&click);click.type=SDL_EVENT_MOUSE_BUTTON_DOWN;click.button.windowID=SDL_GetWindowID(native);click.button.x=native_points(native,dpi_units(30));click.button.y=native_points(native,dpi_units(item_y));SDL_PushEvent(&click);click.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&click);
        }else {
        SDL_Event key={0};key.type=SDL_EVENT_KEY_DOWN;key.key.windowID=SDL_GetWindowID(native);if(found) { key.key.key=SDLK_DOWN;while(steps--) SDL_PushEvent(&key);key.key.key=SDLK_RETURN; }else { key.key.key=SDLK_ESCAPE;fprintf(stderr,"WinBox smoke menu item missing: %s\n",choice); }SDL_PushEvent(&key);
        }
    }
    while(!done && !c->failed && !ui.quit) {
        extern void network_pump(void);network_pump();uint64_t now=SDL_GetTicks();
        for(unsigned i=0;i<256;i++) if(timers[i].hwnd && timers[i].next<=now) { enqueue(timers[i].hwnd,0x113,timers[i].id,0);timers[i].next=now+timers[i].interval; }
        while(ui.head!=ui.tail && !c->failed && !ui.quit) { Message message=ui.messages[ui.head];ui.head=(ui.head+1)%MAX_MSG;message_input_state(&message);Window *target=window(message.hwnd);if(target && message.message==0xf) target->paint_pending=0;invoke(c,target,message.message,message.wp,message.lp);if(target && message.message==0xf) request_present(target); }
        if(!menu(handle)) break;
        cairo_set_source_rgb(cr,.94,.94,.94);cairo_paint(cr);cairo_select_font_face(cr,"Tahoma",0,0);cairo_select_font_face(cr,"Tahoma",0,0);cairo_set_font_size(cr,11);int top=0;
        for(size_t i=0;i<count;i++) { MenuItem *item=&m->items[i];int row=item->type&0x800 ? 8 : 20;
            if(item->type&0x800) { cairo_set_source_rgb(cr,.65,.65,.65);cairo_rectangle(cr,4,top+3,width-8,1);cairo_fill(cr); }
            else { if((int)i==selected) { cairo_set_source_rgb(cr,.1,.4,.75);cairo_rectangle(cr,1,top,width-2,row);cairo_fill(cr); }
                cairo_set_source_rgb(cr,item->state&3 ? .55 : (int)i==selected ? 1 : 0,item->state&3 ? .55 : (int)i==selected ? 1 : 0,item->state&3 ? .55 : (int)i==selected ? 1 : 0);
                char *label=menu_label(item->text);if(label) { cairo_move_to(cr,24,top+14);cairo_show_text(cr,label);free(label); }const char *tab=item->text ? strchr(item->text,'\t') : NULL;if(tab) { char *shortcut=display_utf8(tab+1);if(shortcut) { cairo_text_extents_t ext;cairo_text_extents(cr,shortcut,&ext);cairo_move_to(cr,width-24-ext.x_advance,top+14);cairo_show_text(cr,shortcut);free(shortcut); } }
                if(item->state&8) { cairo_set_line_width(cr,1.5);cairo_move_to(cr,7,top+10);cairo_line_to(cr,10,top+13);cairo_line_to(cr,15,top+7);cairo_stroke(cr); }if(item->submenu) { cairo_move_to(cr,width-16,top+14);cairo_show_text(cr,">"); }
            }top+=row;
        }
        theme_apply(canvas,NULL);
        const char *menu_shot=getenv("WINBOX_TEST_MENU_SCREENSHOT");if(menu_shot) cairo_surface_write_to_png(canvas,menu_shot);cairo_surface_flush(canvas);SDL_Surface *source=SDL_CreateSurfaceFrom(pixel_width,pixel_height,SDL_PIXELFORMAT_ARGB8888,cairo_image_surface_get_data(canvas),cairo_image_surface_get_stride(canvas));
        if(source) { SDL_BlitSurface(source,NULL,SDL_GetWindowSurface(native),NULL);SDL_DestroySurface(source);SDL_UpdateWindowSurface(native); }
        SDL_Event event;extern unsigned vm_release(CPU *);extern void vm_restore(CPU *,unsigned);unsigned locks=vm_release(c);int received=SDL_WaitEventTimeout(&event,100);vm_restore(c,locks);if(!received) continue;input_event_state(&event);ui.message_modifiers=ui.event_modifiers;ui.message_buttons=ui.input_buttons;ui.input_state=1;
        if(event.type==SDL_EVENT_QUIT) { ui.quit=1;break; }if(event.window.windowID==SDL_GetWindowID(native) && (event.type==SDL_EVENT_WINDOW_HIDDEN || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED || (event.type==SDL_EVENT_WINDOW_FOCUS_LOST && SDL_GetKeyboardFocus()!=native))) break;
        int activate=0;
        if(event.type==SDL_EVENT_MOUSE_MOTION || event.type==SDL_EVENT_MOUSE_BUTTON_UP || event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
            int opening_release=event.type==SDL_EVENT_MOUSE_BUTTON_UP && (opening_buttons&SDL_BUTTON_MASK(event.button.button));if(event.type==SDL_EVENT_MOUSE_BUTTON_UP) opening_buttons&=~SDL_BUTTON_MASK(event.button.button);if(event.window.windowID!=SDL_GetWindowID(native)) { if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) done=1;continue; }if(opening_release) continue;
            int my=(int)lround(guest_pixels(native,event.type==SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y)/dpi_scale);top=0;selected=-1;
            for(size_t i=0;i<count;i++) { int row=m->items[i].type&0x800 ? 8 : 20;if(my>=top && my<top+row && !(m->items[i].type&0x800)) selected=i;top+=row; }
            if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT) pressed=selected;activate=event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT && selected==pressed;
        }else if(event.type==SDL_EVENT_KEY_DOWN) {
            if(event.key.key==SDLK_ESCAPE || event.key.key==SDLK_LEFT) done=1;
            else if(event.key.key==SDLK_DOWN || event.key.key==SDLK_UP) { int direction=event.key.key==SDLK_DOWN ? 1 : -1;for(size_t n=0;n<count;n++) { selected=(selected+direction+(int)count)%(int)count;if(!(m->items[selected].type&0x800) && !(m->items[selected].state&3)) break; } }
            else activate=event.key.key==SDLK_RETURN || event.key.key==SDLK_RIGHT;
        }
        if(activate && selected>=0 && !(m->items[selected].state&3)) {
            MenuItem item=m->items[selected];if(item.submenu) { int row_y=y;for(int i=0;i<selected;i++) row_y+=dpi_units((m->items[i].type&0x800) ? 8 : 20);command=popup_menu(c,item.submenu,owner,x+pixel_width,row_y,depth+1,native,x,y);if(command) done=1; }
            else { command=item.id;done=1; }
        }
    }
    cairo_destroy(cr);cairo_surface_destroy(canvas);SDL_DestroyWindow(native);return command;
}
static uint32_t lparam(int x,int y) { return (uint16_t)x|((uint32_t)(uint16_t)y<<16); }
static void origin(Window *w,int *x,int *y) { *x=*y=0;while(w) { if(w->native) { int a=0,b=0;if(w->native_popup) { get_pixel_position(w->native,&a,&b);int px,py;origin(window(w->native_parent),&px,&py);a+=px;b+=py-menu_height(window(w->native_parent)); }else if(!local_coordinates()) get_pixel_position(w->native,&a,&b);*x+=a;*y+=b+menu_height(w);break; }*x+=w->x;*y+=w->y;w=window(w->parent); } }
static Window *hit(Window *root,int *x,int *y) { Window *ordered[MAX_WINDOWS];unsigned count=child_order(root,ordered);while(count) { Window *w=ordered[--count];if(w->visible && *x>=w->x && *y>=w->y && *x<w->x+w->w && *y<w->y+w->h) { *x-=w->x;*y-=w->y;return hit(w,x,y); } }return root; }
void ui_post(uint64_t h,uint32_t msg,uint64_t wp,uint64_t lp) { enqueue(h,msg,wp,lp); }
static int pump(CPU *c,int wait) {
    extern void network_pump(void);network_pump();
    uint64_t now=SDL_GetTicks();if(!ui.idle_started) { ui.idle_started=now;fprintf(stderr,"WinBox event loop ready (%llu ms startup)\n",(unsigned long long)(now-ui.started)); }for(unsigned i=0;i<256;i++) if(timers[i].hwnd && timers[i].next<=now) { enqueue(timers[i].hwnd,0x113,timers[i].id,0);timers[i].next=now+timers[i].interval; }
    if(!video(c)) return 0;
    caret_tick(SDL_GetTicks());const char *smoke=getenv("WINBOX_SMOKE_MS");if(smoke && SDL_GetTicks()-ui.idle_started>(uint64_t)strtoul(smoke,NULL,10)) { ui.capture_screenshot=1;for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].native) present(&ui.windows[i]);if(ui.test_input_observed) fprintf(stderr,"WinBox smoke input verified\n");ui.quit=1;return 0; }
    static int input_stage;const char *input=getenv("WINBOX_TEST_INPUT");
    if(input && now-ui.idle_started>1000 && input_stage==0) { for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].visible && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_connect")) { SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(ui.windows[i].native);e.button.button=SDL_BUTTON_LEFT;e.button.x=150;e.button.y=23+native_points(ui.windows[i].native,menu_height(&ui.windows[i]));SDL_Event motion;memset(&motion,0,sizeof motion);motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.windowID=e.button.windowID;motion.motion.x=150;motion.motion.y=23+native_points(ui.windows[i].native,menu_height(&ui.windows[i]));SDL_PushEvent(&motion);SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e);input_stage=1;break; } }
    if(input && now-ui.idle_started>1400 && input_stage==1 && ui.focus) { enqueue(ui.focus,0x102,1,1);enqueue(ui.focus,0x102,8,1);if(getenv("WINBOX_TEST_PASTE_INPUT")) { SDL_SetClipboardText(input);enqueue(ui.focus,0x102,22,1); }else for(const unsigned char *p=(const unsigned char *)input;*p;p++) enqueue(ui.focus,0x102,*p,1);if(getenv("WINBOX_TEST_BACKSPACE")) enqueue(ui.focus,0x102,'X',1);input_stage=2; }
    static int backspace_stage;
    if(input && getenv("WINBOX_TEST_BACKSPACE") && now-ui.idle_started>1800 && input_stage==2 && !backspace_stage) { Window *target=window(ui.focus);while(target && !target->native) target=window(target->parent);if(target && target->native) { SDL_Event key;memset(&key,0,sizeof key);key.type=SDL_EVENT_KEY_DOWN;key.key.windowID=SDL_GetWindowID(target->native);key.key.key=SDLK_BACKSPACE;key.key.scancode=SDL_SCANCODE_BACKSPACE;SDL_PushEvent(&key);key.type=SDL_EVENT_KEY_UP;SDL_PushEvent(&key);backspace_stage=1; } }
    if(input && now-ui.idle_started>2000 && input_stage==2 && ui.focus) { enqueue(ui.focus,0x100,9,1);enqueue(ui.focus,0x101,9,1);input_stage=3; }
    static int theme_menu_test_stage;
    if((getenv("WINBOX_TEST_THEME_MENU") || getenv("WINBOX_TEST_SETTINGS_MENU")) && now-ui.idle_started>1600 && !theme_menu_test_stage) {
        for(unsigned i=1;i<MAX_WINDOWS;i++) { Window *root=&ui.windows[i];Gdi *bar=root->used ? gdi(root->menu) : NULL;
            if(root->visible && root->native && bar && bar->type==5 && bar->menu.count && bar->menu.items[0].submenu && (!getenv("WINBOX_TEST_SETTINGS_MENU") || !strcmp(root->class_name,"routeros_main"))) {
                theme_menu_test_stage=1;int x,y;origin(root,&x,&y);unsigned selected=getenv("WINBOX_TEST_SETTINGS_MENU") && bar->menu.count>1 ? 1 : 0;uint32_t command=popup_menu(c,bar->menu.items[selected].submenu,root->hwnd,x,y,0,NULL,0,0);wallpaper_command(c,root,command);break;
            }
        }
    }
    static int theme_test_stage;
    if(getenv("WINBOX_TEST_THEME_TOGGLE") && now-ui.idle_started>1400 && !theme_test_stage) {
        for(unsigned i=1;i<MAX_WINDOWS;i++) { Window *root=&ui.windows[i];if(root->used && root->visible && theme_control(root,root->w-40,8)) {
            SDL_Event event={0};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.windowID=SDL_GetWindowID(root->native);event.button.button=SDL_BUTTON_LEFT;event.button.x=native_points(root->native,root->w-dpi_units(40));event.button.y=native_points(root->native,dpi_units(8));SDL_PushEvent(&event);event.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&event);theme_test_stage=1;break;
        } }
    }
    static int password_stage;
    const char *password_file=getenv("WINBOX_TEST_PASSWORD_FILE");
    if(password_file && now-ui.idle_started>2200 && !password_stage) { for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].visible && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_connect")) { SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(ui.windows[i].native);e.button.button=SDL_BUTTON_LEFT;e.button.x=150;e.button.y=79+native_points(ui.windows[i].native,menu_height(&ui.windows[i]));SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e);password_stage=1;break; } }
    if(password_file && now-ui.idle_started>2400 && password_stage==1 && ui.focus) { FILE *f=fopen(password_file,"rb");char secret[1024]={0};if(f && fgets(secret,sizeof secret,f)) { secret[strcspn(secret,"\r\n")]=0;enqueue(ui.focus,0x102,1,1);enqueue(ui.focus,0x102,8,1);for(const unsigned char *p=(const unsigned char *)secret;*p;p++) enqueue(ui.focus,0x102,*p,1); }if(f) fclose(f);memset(secret,0,sizeof secret);password_stage=2; }
    if(password_file && now-ui.idle_started>2700 && password_stage==2 && ui.focus) { enqueue(ui.focus,0x100,9,1);enqueue(ui.focus,0x101,9,1);password_stage=3; }
    static int connect_stage;
    if(getenv("WINBOX_TEST_CONNECT") && now-ui.idle_started>(password_file ? 3200 : 2500) && !connect_stage) { for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].visible && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_connect")) { SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(ui.windows[i].native);e.button.button=SDL_BUTTON_LEFT;e.button.x=470;e.button.y=125+native_points(ui.windows[i].native,menu_height(&ui.windows[i]));SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e);connect_stage=1;break; } }
    static uint64_t main_seen;static int main_test_stage;
    if(getenv("WINBOX_TEST_MAIN_READONLY") && !main_test_stage) {
        for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].visible && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_main")) {
            if(!main_seen) { main_seen=now;const char *size=getenv("WINBOX_TEST_MAIN_RESIZE");if(size) { int width=960,height=720;if(strcmp(size,"1")) sscanf(size,"%dx%d",&width,&height);if(width>0 && height>0 && width<=8192 && height<=8192) SDL_SetWindowSize(ui.windows[i].native,width,height); } }
            if(now-main_seen>1000) { SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(ui.windows[i].native);e.button.button=SDL_BUTTON_LEFT;const char *section=getenv("WINBOX_TEST_MAIN_SECTION");const char *sections[]={"Quick Set","WiFi","User Manager","Wireless","Interfaces","WireGuard","Bridge","PPP","Switch","Mesh","IP","IPv6","OpenFlow","MPLS","Routing","System","Queues","Files","Log","RADIUS","Tools","Terminal","IoT","TR069","Dot1X","Dude","Make Supout.rif","Container","App","New WinBox","Exit"};int row_index=4;if(section) { row_index=-1;for(unsigned j=0;j<sizeof sections/sizeof *sections;j++) if(!strcmp(section,sections[j])) { row_index=j;break; }if(row_index<0) break; }int row=menu_height(&ui.windows[i])+44+20*row_index;e.button.x=65;e.button.y=row;SDL_WarpMouseInWindow(ui.windows[i].native,65,row);SDL_Event motion={0};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.windowID=e.button.windowID;motion.motion.x=65;motion.motion.y=row;SDL_PushEvent(&motion);SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e);main_test_stage=1;fprintf(stderr,"WinBox smoke opening %s at %d (read only)\n",section ? section : "Interfaces",row); }break;
        }
    }
    static int second_section_opened;
    if(getenv("WINBOX_TEST_MAIN_READONLY") && getenv("WINBOX_TEST_MAIN_TERMINAL") && main_seen && main_test_stage && !second_section_opened && now-main_seen>4500) {
        for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_main")) {
            SDL_Event e={0};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(ui.windows[i].native);e.button.button=SDL_BUTTON_LEFT;e.button.x=65;e.button.y=menu_height(&ui.windows[i])+44+20*21;SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e);second_section_opened=1;fprintf(stderr,"WinBox smoke opening Terminal above Interfaces (read only)\n");break;
        }
    }
    static int reactivated;
    if(getenv("WINBOX_TEST_MAIN_REACTIVATE") && second_section_opened && !reactivated && now-main_seen>8000) {
        for(unsigned i=1;i<MAX_WINDOWS;i++) { Window *child=&ui.windows[i];if(!child->used || !child->visible || !child->title || strcmp(child->title,"Interfaces")) continue;Window *root=native_root(child);if(!root || !root->native) continue;int x,y,rx,ry;origin(child,&x,&y);origin(root,&rx,&ry);SDL_Event e={0};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(root->native);e.button.button=SDL_BUTTON_LEFT;e.button.x=x-rx+20;e.button.y=y-ry+menu_height(root)+10;SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e);reactivated=1;break; }
    }
    if(getenv("WINBOX_TEST_MAIN_DRAG") && main_seen && main_test_stage>=1 && main_test_stage<4) {
        for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_main")) {
            SDL_Event e={0};uint32_t id=SDL_GetWindowID(ui.windows[i].native);
            if(main_test_stage==1 && now-main_seen>4000) { SDL_WarpMouseInWindow(ui.windows[i].native,260,122);e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=id;e.motion.x=260;e.motion.y=122;SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=id;e.button.button=SDL_BUTTON_LEFT;e.button.x=260;e.button.y=122;SDL_PushEvent(&e);main_test_stage=2; }
            else if(main_test_stage==2 && now-main_seen>4200) { SDL_WarpMouseInWindow(ui.windows[i].native,440,192);for(int n=1;n<=90;n++) { e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=id;e.motion.state=SDL_BUTTON_LMASK;e.motion.x=260+n*2;e.motion.y=122+n*70/90;SDL_PushEvent(&e); }main_test_stage=3; }
            else if(main_test_stage==3 && now-main_seen>4500) { e.type=SDL_EVENT_MOUSE_BUTTON_UP;e.button.windowID=id;e.button.button=SDL_BUTTON_LEFT;e.button.x=440;e.button.y=192;SDL_PushEvent(&e);main_test_stage=4;fprintf(stderr,"WinBox smoke internal window drag completed\n"); }break;
        }
    }
    static int terminal_stage;
    if(getenv("WINBOX_TEST_TERMINAL") && main_seen && now-main_seen>5000) {
        Window *root=NULL,*terminal=NULL;for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].visible) { if(ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_main")) root=&ui.windows[i];if(!strncmp(ui.windows[i].title,"Terminal",8)) terminal=&ui.windows[i]; }
        if(root && terminal) {
            uint32_t id=SDL_GetWindowID(root->native);SDL_Event e={0};
            if(!terminal_stage) { int rx,ry,tx,ty;origin(root,&rx,&ry);origin(terminal,&tx,&ty);int x=tx-rx+40,y=ty-ry+menu_height(root)+40;SDL_WarpMouseInWindow(root->native,x,y);e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=id;e.motion.x=x;e.motion.y=y;SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=id;e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e);terminal_stage=1; }
            else if(terminal_stage==1 && now-main_seen>5400) { const char *command=":put (\"WINBOX_NATIVE_\" . \"TERMINAL_OK\")";for(const unsigned char *ch=(const unsigned char *)command;*ch;ch++) enqueue(ui.focus ? ui.focus : root->hwnd,0x102,*ch,1);terminal_stage=2; }
            else if(terminal_stage==2 && now-main_seen>5800) { e.type=SDL_EVENT_KEY_DOWN;e.key.windowID=id;e.key.key=SDLK_RETURN;SDL_PushEvent(&e);e.type=SDL_EVENT_KEY_UP;SDL_PushEvent(&e);terminal_stage=3;fprintf(stderr,"WinBox smoke submitted read-only terminal command\n"); }
        }
    }
    static int popup_test_stage;
    if(getenv("WINBOX_TEST_MENU_SELECT") && ui.test_popup && now>=ui.test_popup_at && !popup_test_stage) { Window *popup=window(ui.test_popup);if(popup && popup->native && popup->visible) { SDL_Event e={0};uint32_t id=SDL_GetWindowID(popup->native);SDL_WarpMouseInWindow(popup->native,ui.test_popup_x,ui.test_popup_y);e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=id;e.motion.x=ui.test_popup_x;e.motion.y=ui.test_popup_y;SDL_PushEvent(&e);
        const char *method=getenv("WINBOX_TEST_MENU_METHOD");if(method && !strcmp(method,"keyboard")) { e.type=SDL_EVENT_KEY_DOWN;e.key.windowID=id;e.key.key=SDLK_DOWN;SDL_PushEvent(&e);e.key.key=SDLK_UP;SDL_PushEvent(&e);e.key.key=SDLK_RETURN;SDL_PushEvent(&e); }
        else { e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=id;e.button.button=SDL_BUTTON_LEFT;e.button.x=ui.test_popup_x;e.button.y=ui.test_popup_y;SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e); }popup_test_stage=1;fprintf(stderr,"WinBox smoke selecting custom popup: %s\n",getenv("WINBOX_TEST_MENU_SELECT")); } }
    static int resize_test_stage;
    if(getenv("WINBOX_TEST_RESIZE") && now-ui.idle_started>2800 && !resize_test_stage) { for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].visible && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_connect")) { for(int n=0;n<16;n++) SDL_SetWindowSize(ui.windows[i].native,900+n*4,660+n*4);SDL_SetWindowSize(ui.windows[i].native,960,720);resize_test_stage=1;break; } }
    static int neighbor_stage;if(getenv("WINBOX_TEST_NEIGHBORS") && now-ui.idle_started>3000 && !neighbor_stage) { for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].visible && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_connect")) { SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(ui.windows[i].native);e.button.button=SDL_BUTTON_LEFT;e.button.x=105;e.button.y=207+menu_height(&ui.windows[i]);SDL_PushEvent(&e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;SDL_PushEvent(&e);neighbor_stage=1;break; } }
    static int close_stage;if(getenv("WINBOX_TEST_CLOSE") && now-ui.idle_started>3500 && !close_stage) { for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].visible && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_connect")) { ui.capture_screenshot=1;present(&ui.windows[i]);if(ui.test_input_observed) fprintf(stderr,"WinBox smoke input verified\n");fprintf(stderr,"WinBox smoke close requested\n");enqueue(ui.windows[i].hwnd,0x10,0,0);close_stage=1;break; } }
    flush_frames();SDL_Event e;extern unsigned vm_release(CPU *);extern void vm_restore(CPU *,unsigned);unsigned depth=vm_release(c);int received=wait ? SDL_WaitEventTimeout(&e,8) : SDL_PollEvent(&e);vm_restore(c,depth);if(!received) return 1;
    if(e.type==SDL_EVENT_MOUSE_MOTION) for(unsigned i=0;i<128;i++) { SDL_Event next;if(SDL_PeepEvents(&next,1,SDL_PEEKEVENT,SDL_EVENT_FIRST,SDL_EVENT_LAST)!=1 || next.type!=SDL_EVENT_MOUSE_MOTION || next.motion.windowID!=e.motion.windowID || next.motion.state!=e.motion.state) break;if(SDL_PeepEvents(&next,1,SDL_GETEVENT,SDL_EVENT_FIRST,SDL_EVENT_LAST)!=1) break;e=next; }
    if(e.type==SDL_EVENT_MOUSE_MOTION) { SDL_Event next;for(unsigned i=0;i<1024;i++) { if(SDL_PeepEvents(&next,1,SDL_PEEKEVENT,SDL_EVENT_FIRST,SDL_EVENT_LAST)!=1 || next.type!=SDL_EVENT_MOUSE_MOTION || next.motion.windowID!=e.motion.windowID) break;SDL_PeepEvents(&e,1,SDL_GETEVENT,SDL_EVENT_FIRST,SDL_EVENT_LAST); } }
    input_event_state(&e);Window *w=NULL;for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].native && SDL_GetWindowID(ui.windows[i].native)==e.window.windowID) { w=&ui.windows[i];break; }
    if(e.type==SDL_EVENT_QUIT) { ui.quit=1;return 0; }if(!w) return 1;
    if(e.type>=SDL_EVENT_WINDOW_FIRST && e.type<=SDL_EVENT_WINDOW_LAST) {
        if(e.type==SDL_EVENT_WINDOW_MOVED) { int x,y;origin(w,&x,&y);if((!local_coordinates() || w->native_popup) && (x!=w->x || y!=w->y)) { w->x=x;w->y=y;notify_position(c,w,5);enqueue(w->hwnd,3,0,lparam(x,y)); } }
        if(e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type==SDL_EVENT_WINDOW_RESIZED) { int width,height;SDL_GetWindowSizeInPixels(w->native,&width,&height);if(width>0 && height>0 && width<=8192 && height<=8192 && (w->w!=width || w->h!=height)) { resize_canvas(w,width,height);notify_position(c,w,6);enqueue(w->hwnd,5,0,lparam(width,height-menu_height(w)));enqueue(w->hwnd,0xf,0,0);request_present(w); } }
        if(e.type==SDL_EVENT_WINDOW_EXPOSED) enqueue(w->hwnd,0xf,0,0);
        if(e.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) enqueue(w->hwnd,0x10,0,0);
        if(e.type==SDL_EVENT_WINDOW_HIDDEN && w->native_popup && w->visible && (SDL_GetWindowFlags(w->native)&SDL_WINDOW_HIDDEN)) { w->visible=0;enqueue(w->hwnd,8,w->native_parent,0);enqueue(w->hwnd,0x10,0,0); }
        if(e.type==SDL_EVENT_WINDOW_MOUSE_LEAVE && theme.hover==w->hwnd) { theme.hover=0;request_present(w); }
        if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST && theme.pressed==w->hwnd) { theme.pressed=0;request_present(w); }
        if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { Window *focused=window(ui.focus),*root=focused;while(root && !root->native && window(root->parent)) root=window(root->parent);Window *next=NULL;SDL_Window *keyboard=SDL_GetKeyboardFocus();for(unsigned i=1;i<MAX_WINDOWS;i++) if(keyboard && ui.windows[i].used && ui.windows[i].native==keyboard) { next=&ui.windows[i];break; }if(focused && root==w && (!next || coordinate_space(next)!=coordinate_space(root))) enqueue(focused->hwnd,8,next ? next->hwnd : 0,0); }
        if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED) { ui.active=coordinate_space(w);Window *focused=window(ui.focus),*root=focused;while(root && !root->native && root->parent && window(root->parent)) root=window(root->parent);if(!focused || (root!=w && coordinate_space(root)!=coordinate_space(w))) { ui.focus=w->hwnd;enqueue(w->hwnd,7,0,0); } }
    } else if(e.type==SDL_EVENT_MOUSE_MOTION || e.type==SDL_EVENT_MOUSE_BUTTON_DOWN || e.type==SDL_EVENT_MOUSE_BUTTON_UP) {
        int x=guest_pixels(w->native,e.type==SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x),y=guest_pixels(w->native,e.type==SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y);int gx,gy;origin(w,&gx,&gy);gy-=menu_height(w);
        int over_theme=!ui.capture && theme_control(w,x,y);
        uint64_t hover=over_theme ? w->hwnd : 0;
        if(theme.hover!=hover) { request_present(window(theme.hover));theme.hover=hover;request_present(w); }
        if(over_theme) {
            if(e.type==SDL_EVENT_MOUSE_MOTION) change_cursor(c,system_cursor(c,32649));
            if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT) { theme.pressed=w->hwnd;request_present(w); }
            if(e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT) { int activate=theme.pressed==w->hwnd;theme.pressed=0;if(activate) theme_toggle();request_present(w); }
            return 1;
        }
        if(e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT && theme.pressed) { request_present(window(theme.pressed));theme.pressed=0; }
        if(!ui.capture && y<menu_height(w)) { if(e.type==SDL_EVENT_MOUSE_MOTION) change_cursor(c,system_cursor(c,32512)); if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT) { cairo_t *cr=cairo_create(w->surface);int index=menubar_item(w,cr,x);cairo_destroy(cr);Gdi *g=gdi(w->menu);if(index>=0 && g) { MenuItem item=g->menu.items[index];if(!(item.state&3)) { uint32_t command=item.submenu ? popup_menu(c,item.submenu,w->hwnd,gx+x,gy+menu_height(w),0,NULL,0,0) : item.id;if(command && !wallpaper_command(c,w,command)) enqueue(w->hwnd,0x111,command,0); } } }return 1; }
        ui.cursor_x=x+gx;ui.cursor_y=y+gy;ui.pointer_window=w->hwnd;
        Window *target=NULL;
        if(ui.capture) { target=window(ui.capture);if(target) { int tx,ty;origin(target,&tx,&ty);x+=gx-tx;y+=gy-ty; } }
        else { y-=menu_height(w);target=hit(w,&x,&y); }
        if(!target) target=w;
        if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && !ui.capture) raise_mdi(target);
        if(e.type==SDL_EVENT_MOUSE_BUTTON_UP) { uint32_t bit=SDL_BUTTON_MASK(e.button.button);int opening_release=!!(target->opening_buttons&bit);for(unsigned i=1;i<MAX_WINDOWS;i++) ui.windows[i].opening_buttons&=~bit;if(opening_release) return 1; }
        uint32_t msg=0x200;if(e.type!=SDL_EVENT_MOUSE_MOTION) { unsigned base=e.button.button==SDL_BUTTON_RIGHT ? 0x204 : e.button.button==SDL_BUTTON_MIDDLE ? 0x207 : 0x201;msg=e.type==SDL_EVENT_MOUSE_BUTTON_UP ? base+1 : base;if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.clicks && !(e.button.clicks&1) && (target->class_style&8)) msg=base+2; }
        if(!ui.capture) enqueue(target->hwnd,0x20,target->hwnd,1u|((uint32_t)msg<<16));enqueue(target->hwnd,msg,mouse_key_flags(),lparam(x,y));
    } else if(e.type==SDL_EVENT_TEXT_INPUT) { for(unsigned char *p=(unsigned char *)e.text.text;*p;p++) enqueue(ui.focus ? ui.focus : w->hwnd,0x102,*p,1); }
    else if(e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP) { if(e.type==SDL_EVENT_KEY_DOWN && e.key.key==SDLK_BACKSPACE && getenv("WINBOX_TEST_BACKSPACE")) ui.test_input_observed=0;unsigned key=virtual_key(e.key.key);uint32_t flags=e.type==SDL_EVENT_KEY_UP ? 0xc0000001u : 1u|(e.key.repeat ? 0x40000000u : 0);if(key) enqueue(ui.focus ? ui.focus : w->hwnd,e.type==SDL_EVENT_KEY_DOWN ? 0x100 : 0x101,key,flags); }
    else if(e.type==SDL_EVENT_MOUSE_WHEEL) { int direction=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;uint64_t target=window(ui.focus) ? ui.focus : w->hwnd;if(e.wheel.y) enqueue(target,0x20a,mouse_key_flags()|((uint32_t)(uint16_t)(e.wheel.y*120*direction)<<16),lparam(ui.cursor_x,ui.cursor_y));if(e.wheel.x) enqueue(target,0x20e,mouse_key_flags()|((uint32_t)(uint16_t)(e.wheel.x*120*direction)<<16),lparam(ui.cursor_x,ui.cursor_y)); }
    return 1;
}
#define IS(s) (!strcmp(name,s))
#define A(n) v[n]
#define RET(x) do { *result=(uint64_t)(x);return 1; } while(0)
#include "ui_dialog.inc"
static int wallpaper_command(CPU *c,Window *owner,uint32_t command) {
    if(command!=WALLPAPER_CHOOSE && command!=WALLPAPER_REMOVE) return 0;
    if(command==WALLPAPER_REMOVE) { if(!wallpaper_set(NULL)) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Wallpaper","Could not remove the saved wallpaper.",owner ? owner->native : NULL);return 1; }
    const char filters[]="Images (PNG, JPEG, BMP)\0*.png;*.jpg;*.jpeg;*.bmp\0\0";uint64_t filter=guest_alloc(c,sizeof filters);if(!filter) return 1;memcpy(writable_memory(c,filter,sizeof filters),filters,sizeof filters);
    NativeFileDialog *dialog=choose_native_files(c,owner ? owner->hwnd : 0,SDL_FILEDIALOG_OPENFILE,"Choose wallpaper",NULL,filter,0);guest_free(c,filter);
    if(dialog) { if(atomic_load(&dialog->done) && (dialog->error || (dialog->count && !wallpaper_set(dialog->files[0])))) { fprintf(stderr,"WinBox wallpaper: unable to load image\n");if(!getenv("WINBOX_TEST_DIALOG_FILES")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Wallpaper","Could not load this image. Choose a PNG, JPEG or BMP up to 8192 pixels per side (32 megapixels).",owner ? owner->native : NULL); }file_dialog_release(dialog); }return 1;
}
int ui_api(CPU *c,const char *name,uint64_t *result) {
    uint64_t v[16]={0};for(unsigned i=0;i<4;i++) v[i]=argument(c,i);for(unsigned i=4;i<16;i++) { uint64_t address=c->r[RSP]+40+8*(i-4);if(address>=c->stack_base && address+8<=c->stack_base+c->stack_size) v[i]=load(c,address,64); }
    if(IS("GetOpenFileNameA") || IS("GetSaveFileNameA")) RET(native_file_dialog(c,A(0),IS("GetSaveFileNameA")));
    if(IS("SHBrowseForFolderA")) RET(native_folder_dialog(c,A(0)));
    if(IS("SHGetPathFromIDListA")) { char *path=A(0) ? guest_string(c,A(0)) : NULL;if(!path || strlen(path)>=260 || !A(1)) RET(0);uint8_t *out=writable_memory(c,A(1),strlen(path)+1);if(out) memcpy(out,path,strlen(path)+1);RET(out!=NULL); }
    if(IS("SetPropA") || IS("GetPropA") || IS("RemovePropA")) { char *key=A(1)>=65536 ? text(c,A(1),0) : NULL;unsigned empty=4096;for(unsigned i=0;i<4096;i++) { if(!properties[i].hwnd && empty==4096) empty=i;if(properties[i].hwnd==A(0) && (key ? (properties[i].name && !strcmp(key,properties[i].name)) : (!properties[i].name && properties[i].key==A(1)))) { uint64_t old=properties[i].value;if(IS("SetPropA")) properties[i].value=A(2);if(IS("RemovePropA")) { free(properties[i].name);memset(&properties[i],0,sizeof properties[i]); }free(key);RET(IS("SetPropA") ? 1 : old); } }if(IS("SetPropA") && empty<4096) { properties[empty].hwnd=A(0);properties[empty].key=A(1);properties[empty].name=key;properties[empty].value=A(2);RET(1); }free(key);RET(0); }
    if(IS("WindowFromPoint")) { int px=(int32_t)A(0),py=(int32_t)(A(0)>>32);for(int i=MAX_WINDOWS-1;i>0;i--) { Window *w=&ui.windows[i];if(!w->used || !w->native || !w->visible) continue;if(local_coordinates() && ui.pointer_window && coordinate_space(w)!=coordinate_space(window(ui.pointer_window))) continue;int x,y;origin(w,&x,&y);if(px>=x && py>=y && px<x+w->w && py<y+w->h) { int localx=px-x,localy=py-y;Window *found=hit(w,&localx,&localy);RET(found ? found->hwnd : w->hwnd); } }RET(0); }
    if(IS("GetWindowThreadProcessId")) { if(A(1)) store(c,A(1),32,getpid());RET(1); }
    if(IS("GetTopWindow")) { for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].parent==A(0)) RET(ui.windows[i].hwnd);RET(0); }
    if(IS("GetWindow")) { Window *w=window(A(0));if(!w) RET(0);if(A(1)==4) RET(w->parent);if(A(1)==5) { for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].parent==w->hwnd) RET(ui.windows[i].hwnd); }RET(0); }
    if(IS("IntersectRect") || IS("UnionRect")) { int a[4],b[4],r[4];for(unsigned i=0;i<4;i++) { a[i]=load(c,A(1)+4*i,32);b[i]=load(c,A(2)+4*i,32); }for(unsigned i=0;i<4;i++) r[i]=IS("IntersectRect") ? (i<2 ? (a[i]>b[i] ? a[i] : b[i]) : (a[i]<b[i] ? a[i] : b[i])) : (i<2 ? (a[i]<b[i] ? a[i] : b[i]) : (a[i]>b[i] ? a[i] : b[i]));int valid=r[2]>r[0] && r[3]>r[1];rectangle(c,A(0),valid ? r[0] : 0,valid ? r[1] : 0,valid ? r[2] : 0,valid ? r[3] : 0);RET(valid); }
    if(IS("GetWindowPlacement")) { Window *w=window(A(0));if(!w) RET(0);store(c,A(1),32,44);store(c,A(1)+4,32,0);store(c,A(1)+8,32,w->visible ? 1 : 0);int x=w->x,y=w->y;if(w->native && !w->native_popup && local_coordinates()) x=y=0;rectangle(c,A(1)+28,x,y,x+w->w,y+w->h);RET(1); }
    if(IS("SetWindowPlacement")) { Window *w=window(A(0));if(!w) RET(0);int x=load(c,A(1)+28,32),y=load(c,A(1)+32,32),width=(int)load(c,A(1)+36,32)-x,height=(int)load(c,A(1)+40,32)-y;if(width>0 && height>0 && width<8192 && height<8192) { w->x=x;w->y=y;position_native(w);resize_canvas(w,width,height);if(w->native) set_pixel_size(w->native,width,height);notify_position(c,w,4);enqueue(w->hwnd,5,0,lparam(width,height-menu_height(w))); }w->visible=load(c,A(1)+8,32)!=0;if(w->native && w->visible) SDL_ShowWindow(w->native);RET(1); }
    if(IS("IsWindow")) RET(window(A(0))!=NULL);
    if(IS("IsWindowVisible")) { Window *w=window(A(0));RET(w && w->visible); }
    if(IS("IsWindowEnabled")) { Window *w=window(A(0));RET(w && !(w->style&0x08000000)); }
    if(IS("EnableWindow")) { Window *w=window(A(0));if(!w) RET(0);int was_disabled=!!(w->style&0x08000000);if(A(1)) w->style&=~UINT64_C(0x08000000);else w->style|=0x08000000;if(was_disabled==!!A(1)) invoke(c,w,0xa,!!A(1),0);RET(was_disabled); }
    if(IS("DestroyWindow")) { Window *w=window(A(0));if(!w) RET(0);destroy_window(c,w);RET(1); }
    if(IS("GetScrollPos")) RET(0);
    if(IS("RegisterClassA")) { uint64_t p=A(0);if(ui.class_count==256) RET(0);Class *cl=&ui.classes[ui.class_count++];cl->proc=load(c,p+8,64);cl->style=load(c,p,32);cl->extra=(unsigned)load(c,p+20,32);cl->background=load(c,p+48,64);cl->cursor=load(c,p+40,64);cl->name=text(c,load(c,p+64,64),0);RET(ui.class_count); }
    if(IS("CreateWindowExA") || IS("CreateWindowExW")) {
        if(!video(c)) RET(0);char *clname=text(c,A(1),IS("CreateWindowExW")),*title=text(c,A(2),IS("CreateWindowExW"));Class *cl=NULL;
        for(unsigned i=0;i<ui.class_count;i++) if(!strcmp(ui.classes[i].name,clname)) { cl=&ui.classes[i];break; }
        unsigned index=1;while(index<MAX_WINDOWS && ui.windows[index].used) index++;if(index==MAX_WINDOWS) { free(clname);free(title);RET(0); }
        Window *w=&ui.windows[index];uint64_t handle=w->hwnd ? w->hwnd+MAX_WINDOWS*16 : WH(index);memset(w,0,sizeof *w);w->used=1;w->zorder=++zserial;w->hwnd=handle;w->class_style=cl ? cl->style : 8;w->class_cursor=cl ? cl->cursor : 0;w->proc=cl ? cl->proc : !strcmp(clname,"EDIT") ? EDIT_PROC : 0;w->class_name=clname;w->title=title;w->style=A(3);w->exstyle=A(0);w->parent=A(8);w->menu=A(9);w->instance=A(10);w->x=(int)A(4);w->y=(int)A(5);w->w=(int)A(6);w->h=(int)A(7);
        if(w->x==INT_MIN) w->x=100;if(w->y==INT_MIN) w->y=100;if(w->w<=0 || w->w>8192) w->w=800;if(w->h<=0 || w->h>8192) w->h=600;
        w->visible=!!(w->style&0x10000000);if((w->style&0x80000000) && !(w->style&0x00c00000)) w->opening_buttons=ui.input_buttons;w->surface=surface(w->w,w->h);
        if(!(w->style&0x40000000)) { w->native=create_native_window(w);if(!w->native) { trap(c,SDL_GetError());RET(0); }SDL_StartTextInput(w->native); }
        uint64_t cs=guest_alloc(c,80);store(c,cs,64,A(11));store(c,cs+8,64,A(10));store(c,cs+16,64,A(9));store(c,cs+24,64,A(8));store(c,cs+32,32,A(7));store(c,cs+36,32,A(6));store(c,cs+40,32,A(5));store(c,cs+44,32,A(4));store(c,cs+48,32,A(3));store(c,cs+56,64,A(2));store(c,cs+64,64,A(1));store(c,cs+72,32,A(0));
        if(w->proc && !invoke(c,w,0x81,0,cs)) { fprintf(stderr,"WinBox window rejected WM_NCCREATE: %s\n",clname);RET(0); }
        invoke(c,w,1,0,cs);guest_free(c,cs);enqueue(w->hwnd,5,0,lparam(w->w,w->h-menu_height(w)));enqueue(w->hwnd,0xf,0,0);
        if(w->native && w->visible) SDL_ShowWindow(w->native);
        fprintf(stderr,"WinBox native window: %s (%dx%d), class %s\n",title,w->w,w->h,clname);RET(w->hwnd);
    }
    if(IS("DefWindowProcA")) { Window *w=window(A(0));if(A(1)==0x20 && w && w->class_cursor) { change_cursor(c,w->class_cursor);RET(1); }if(A(1)==0x81) RET(1);if(A(1)==0x14 && w) { cairo_t *cr=cairo_create(w->surface);color(cr,syscolor(15));cairo_paint(cr);cairo_destroy(cr);RET(1); }if(A(1)==0x10 && w) { int primary=!strcmp(w->class_name,"routeros_main") || !strcmp(w->class_name,"routeros_connect");destroy_window(c,w);if(primary) ui.quit=1; }RET(0); }
    if(IS("CallWindowProcA")) { if(A(0)==EDIT_PROC) RET(edit_proc(c,window(A(1)),A(2),A(3),A(4)));uint64_t args[]={A(1),A(2),A(3),A(4)};callback(c,A(0),args,4);RET(c->r[RAX]); }
    if(IS("GetWindowLongPtrA") || IS("GetWindowLongPtrW") || IS("GetWindowLongA")) { Window *w=window(A(0));if(!w) RET(0);int n=(int)A(1);if(n==-21) RET(w->userdata);if(n==-4) RET(w->proc);if(n==-16) RET(w->style);if(n==-20) RET(w->exstyle);if(n==-12) RET(w->menu);if(n==-6) RET(w->instance);if(n==-8) RET(w->parent);RET(n>=0 && n/8<32 ? w->extra[n/8] : 0); }
    if(IS("SetWindowLongPtrA") || IS("SetWindowLongW")) { Window *w=window(A(0));if(!w) RET(0);int n=(int)A(1);uint64_t old=0;if(n==-21) { old=w->userdata;w->userdata=A(2); }else if(n==-4) { old=w->proc;w->proc=A(2); }else if(n==-16) { old=w->style;w->style=A(2); }else if(n==-20) { old=w->exstyle;w->exstyle=A(2); }else if(n>=0 && n/8<32) { old=w->extra[n/8];w->extra[n/8]=A(2); }RET(old); }
    if(IS("GetClassLongPtrA")) { Window *w=window(A(0));RET(w && (int)A(1)==-24 ? w->proc : 0); }
    if(IS("ShowWindow")) { Window *w=window(A(0));if(!w) RET(0);int old=w->visible;w->visible=A(1)!=0;if(w->native) { if(w->visible) { SDL_ShowWindow(w->native);if((int32_t)A(1)!=4 && (int32_t)A(1)!=8) ui.active=w->hwnd; }else SDL_HideWindow(w->native); }if(w->visible) { enqueue(w->hwnd,0xf,0,0);request_present(w); }RET(old); }
    if(IS("GetClientRect") || IS("GetWindowRect")) { Window *w=window(A(0));if(!w) RET(0);int x=0,y=0;if(IS("GetWindowRect")) { origin(w,&x,&y);y-=menu_height(w); }rectangle(c,A(1),x,y,x+w->w,y+w->h-(IS("GetClientRect") ? menu_height(w) : 0));RET(1); }
    if(IS("GetParent")) { Window *w=window(A(0));RET(w ? w->parent : 0); }
    if(IS("SetWindowPos")) {
        Window *w=window(A(0));if(!w) RET(0);unsigned flags=A(6),changes=position_geometry(w,(int)A(2),(int)A(3),(int)A(4),(int)A(5),flags);if(position_zorder(w,A(1),flags)) changes|=16;position_notify(c,w,flags,changes);RET(1);
    }
    if(IS("DeferWindowPos")) {
        DeferredBatch *batch=position_batch(A(0));if(!batch || !window(A(1)) || batch->count>=4096) RET(0);
        DeferredPosition *items=realloc(batch->positions,(batch->count+1)*sizeof *items);if(!items) RET(0);batch->positions=items;
        batch->positions[batch->count++]=(DeferredPosition){A(1),A(2),(int)A(3),(int)A(4),(int)A(5),(int)A(6),(unsigned)A(7),0};RET(A(0));
    }
    if(IS("BeginDeferWindowPos")) { for(unsigned i=0;i<128;i++) if(!deferred[i].used) { DeferredBatch *batch=&deferred[i];batch->handle=batch->handle ? batch->handle+128*16 : 0x78000000+i*16;batch->used=1;batch->count=0;batch->positions=NULL;RET(batch->handle); }RET(0); }
    if(IS("EndDeferWindowPos")) {
        DeferredBatch *batch=position_batch(A(0));if(!batch) RET(0);size_t count=batch->count;DeferredPosition *items=batch->positions;batch->used=0;batch->positions=NULL;batch->count=0;
        for(size_t i=0;i<count;i++) if(!window(items[i].hwnd)) { free(items);RET(0); }
        for(size_t i=0;i<count;i++) { DeferredPosition *p=&items[i];p->changes=position_geometry(window(p->hwnd),p->x,p->y,p->width,p->height,p->flags);if(position_zorder(window(p->hwnd),p->after,p->flags)) p->changes|=16; }
        for(size_t i=0;i<count && !c->failed;i++) { DeferredPosition *p=&items[i];Window *w=window(p->hwnd);if(w) position_notify(c,w,p->flags,p->changes); }free(items);RET(!c->failed);
    }
    if(IS("AdjustWindowRectEx")) RET(1);
    if(IS("ClientToScreen") || IS("ScreenToClient")) { Window *w=window(A(0));int x=(int)load(c,A(1),32),y=(int)load(c,A(1)+4,32),sign=IS("ClientToScreen") ? 1 : -1;int ox=0,oy=0;origin(w,&ox,&oy);x+=sign*ox;y+=sign*oy;store(c,A(1),32,x);store(c,A(1)+4,32,y);RET(1); }
    if(IS("GetSystemMetrics")) { int n=A(0);video(c);SDL_Rect bounds=work_area();switch(n) { case 0:case 16:case 59:case 61:case 78:RET(bounds.w);case 1:case 17:case 60:case 62:case 79:RET(bounds.h);case 2:case 3:case 9:case 10:case 20:case 21:RET(dpi_units(17));case 4:RET(dpi_units(20));case 15:RET(dpi_units(19));case 54:case 55:RET(dpi_units(18));case 5:case 6:RET(1);case 7:case 8:RET(dpi_units(3));case 32:case 33:RET(dpi_units(8));case 11:case 12:RET(dpi_units(32));case 49:case 50:RET(dpi_units(16));case 80:RET(1);default:RET(0); } }
    if(IS("GetSysColor")) RET(syscolor(A(0)));
    if(IS("SystemParametersInfoA")) { if(!video(c)) RET(0);if(A(0)==48) { video(c);SDL_Rect bounds=work_area();rectangle(c,A(2),bounds.x,bounds.y,bounds.x+bounds.w,bounds.y+bounds.h); }else if(A(0)==0x29) { size_t n=load(c,A(2),32);if(n>1024) RET(0);uint8_t *p=writable_memory(c,A(2),n);if(p) { memset(p,0,n);store(c,A(2),32,n);unsigned positions[]={24,92,160,220,280};for(unsigned i=0;i<5;i++) if(positions[i]+60<=n) { store(c,A(2)+positions[i],32,(uint32_t)-dpi_units(11));store(c,A(2)+positions[i]+16,32,400);memcpy(p+positions[i]+28,"Tahoma",7); } } }RET(1); }
    if(IS("GetMessageA")) { if(ui.head!=ui.tail && !ui.quit && !c->failed) pump(c,0);while(ui.head==ui.tail && !ui.quit && !c->failed) pump(c,1);if(ui.quit || c->failed) RET(0);unsigned selected=ui.head,best=3;
        for(unsigned i=ui.head;i!=ui.tail;i=(i+1)%MAX_MSG) { Window *paint_root=native_root(window(ui.messages[i].hwnd));unsigned rank=ui.messages[i].message==0x113 ? 2 : ui.messages[i].message==0xf && !(paint_root && paint_root->resize_pending) ? 1 : 0;if(rank<best) { selected=i;best=rank;if(!best) break; } }
        Message m=ui.messages[selected];while(selected!=ui.head) { unsigned previous=(selected+MAX_MSG-1)%MAX_MSG;ui.messages[selected]=ui.messages[previous];selected=previous; }ui.head=(ui.head+1)%MAX_MSG;message_input_state(&m);uint8_t *p=writable_memory(c,A(0),48);if(p) memset(p,0,48);store(c,A(0),64,m.hwnd);store(c,A(0)+8,32,m.message);store(c,A(0)+16,64,m.wp);store(c,A(0)+24,64,m.lp);store(c,A(0)+32,32,SDL_GetTicks());store(c,A(0)+36,32,m.cursor_x);store(c,A(0)+40,32,m.cursor_y);RET(1); }
    if(IS("TranslateMessage")) {
        uint32_t message=(uint32_t)load(c,A(0)+8,32);
        if(message!=0x100 && message!=0x104) RET(0);
        uint64_t key=load(c,A(0)+16,64);
        /* SDL_EVENT_TEXT_INPUT supplies printable characters. SDL does not emit it for
           these control keys: Windows TranslateMessage supplies WM_CHAR instead.
           Keep this here so WinBox's accelerator/key filtering runs first. */
        SDL_Keymod mod=ui.input_state ? ui.message_modifiers : SDL_GetModState();
        if((mod&SDL_KMOD_CTRL) && !(mod&SDL_KMOD_ALT) && key>='A' && key<='Z') {
            enqueue(load(c,A(0),64),0x102,key-'A'+1,load(c,A(0)+24,64));RET(1);
        }
        if(key==8 || key==9 || key==13 || key==27) {
            enqueue(load(c,A(0),64),message==0x104 ? 0x106 : 0x102,key,load(c,A(0)+24,64));
            RET(1);
        }
        RET(0);
    }
    if(IS("DispatchMessageA")) { Window *w=window(load(c,A(0),64));uint32_t msg=load(c,A(0)+8,32);if(w && msg==0xf) { if(!w->paint_pending) RET(0);w->paint_pending=0; }uint64_t ret=invoke(c,w,msg,load(c,A(0)+16,64),load(c,A(0)+24,64));if(w && msg==0xf) request_present(w);RET(ret); }
    if(IS("PostMessageA") || IS("SendNotifyMessageA")) { enqueue(A(0),A(1),A(2),A(3));RET(1); }
    if(IS("SendMessageA")) RET(invoke(c,window(A(0)),A(1),A(2),A(3)));
    if(IS("PostQuitMessage")) { ui.quit=1;RET(0); }
    if(IS("InvalidateRect")) { Window *w=window(A(0));if(w) enqueue(w->hwnd,0xf,0,0);RET(1); }
    if(IS("UpdateWindow")) { Window *w=window(A(0));if(w && w->paint_pending) { w->paint_pending=0;invoke(c,w,0xf,0,0);request_present(w); }RET(1); }
    if(IS("GetDC") || IS("GetDCEx") || IS("BeginPaint")) { if(!video(c)) RET(0);Window *w=window(A(0));if(!w) { static cairo_surface_t *desktop;if(!desktop) desktop=surface(1280,800);RET(new_dc(0,desktop)); }uint64_t h=new_dc(w->hwnd,w->surface);if(IS("BeginPaint")) { uint8_t *p=writable_memory(c,A(1),72);if(p) memset(p,0,72);store(c,A(1),64,h);rectangle(c,A(1)+12,0,0,w->w,w->h); }RET(h); }
    if(IS("ReleaseDC") || IS("EndPaint") || IS("DeleteDC")) { uint64_t h=IS("DeleteDC") ? A(0) : IS("EndPaint") ? load(c,A(1),64) : A(1);DC *d=dc(h);if(d) { request_present(window(d->window));discard_dc_states(d);cairo_destroy(d->cr);d->used=0; }RET(1); }
    if(IS("CreateCompatibleDC")) { if(!video(c)) RET(0);static cairo_surface_t *blank;if(!blank) blank=surface(1,1);RET(new_dc(0,blank)); }
    if(IS("CreateCompatibleBitmap") || IS("CreateBitmap") || IS("CreateDIBitmap")) { int w,h;if(IS("CreateBitmap")) { w=A(0);h=A(1); }else if(IS("CreateDIBitmap")) { w=(int)load(c,A(1)+4,32);h=abs((int)load(c,A(1)+8,32)); }else { w=A(1);h=A(2); }uint64_t handle=new_gdi(4);Gdi *o=gdi(handle);if(!o) RET(0);o->surface=surface(w,h);if(!o->surface) RET(0);
        if(IS("CreateBitmap") && A(2)==1 && A(3)==1 && A(4) && w>0 && h>0) {
            size_t stride=((size_t)w+15)/16*2;uint8_t *bits=memory(c,A(4),stride*h);cairo_surface_flush(o->surface);uint8_t *pixels=cairo_image_surface_get_data(o->surface);int ds=cairo_image_surface_get_stride(o->surface);
            if(bits) for(int y=0;y<h;y++) for(int x=0;x<w;x++) { uint32_t pixel=bits[y*stride+x/8]&(0x80>>(x%8)) ? 0xffffffffu : 0xff000000u;memcpy(pixels+y*ds+x*4,&pixel,4); }cairo_surface_mark_dirty(o->surface);
        }
        if(IS("CreateDIBitmap") && (A(2)&4) && A(3)) { unsigned bpp=load(c,A(1)+14,16);int top=(int32_t)load(c,A(1)+8,32)<0;size_t stride=((w*bpp+31)/32)*4;uint8_t *p=memory(c,A(3),stride*h);cairo_surface_flush(o->surface);uint8_t *d=cairo_image_surface_get_data(o->surface);int ds=cairo_image_surface_get_stride(o->surface);if(p && (bpp==1 || bpp==4 || bpp==8 || bpp==24 || bpp==32)) for(int y=0;y<h;y++) for(int x=0;x<w;x++) { uint8_t *row=p+(top ? y : h-1-y)*stride;uint32_t pixel;
            if(bpp<=8) { unsigned index=bpp==8 ? row[x] : bpp==4 ? (row[x/2]>>(x%2 ? 0 : 4))&15 : (row[x/8]>>(7-x%8))&1;uint64_t info=A(4) ? A(4) : A(1),palette=info+load(c,info,32);pixel=0xff000000u|(uint32_t)load(c,palette+index*4,32); }
            else { uint8_t *s=row+x*(bpp/8);pixel=0xff000000u|s[0]|(s[1]<<8)|(s[2]<<16); }memcpy(d+y*ds+x*4,&pixel,4); }cairo_surface_mark_dirty(o->surface); }RET(handle);
    }
    if(IS("CreateSolidBrush") || IS("CreateHatchBrush") || IS("CreatePatternBrush")) { uint64_t h=new_gdi(1);Gdi *o=gdi(h);if(o) o->color=A(IS("CreateHatchBrush") ? 1 : 0);RET(h); }
    if(IS("GetObjectW")) { Gdi *o=gdi(A(0));if(!o) RET(0);if(o->type==3) { if(A(1)<92) RET(0);uint8_t *p=writable_memory(c,A(2),92);if(p) memset(p,0,92);store(c,A(2),32,(uint32_t)-(o->size ? abs(o->size) : 12));store(c,A(2)+4,32,o->font_width);store(c,A(2)+16,32,o->weight ? o->weight : 400);store(c,A(2)+20,8,o->italic);store(c,A(2)+21,8,o->underline);store(c,A(2)+22,8,o->strike);store(c,A(2)+23,8,o->charset);store(c,A(2)+27,8,o->pitch);const char *face=*o->face ? o->face : "Sans";for(unsigned i=0;face[i] && i<31;i++) store(c,A(2)+28+2*i,16,(unsigned char)face[i]);RET(92); }if(!o->surface) RET(0);if(A(1)<32) RET(0);uint8_t *p=writable_memory(c,A(2),32);if(p) memset(p,0,32);store(c,A(2)+4,32,cairo_image_surface_get_width(o->surface));store(c,A(2)+8,32,cairo_image_surface_get_height(o->surface));store(c,A(2)+12,32,cairo_image_surface_get_stride(o->surface));store(c,A(2)+16,16,1);store(c,A(2)+18,16,32);RET(32); }
    if(IS("CreateIconIndirect")) {
        uint64_t h=new_gdi(6);Gdi *o=gdi(h),*bitmap=gdi(load(c,A(0)+24,64)),*mask=gdi(load(c,A(0)+16,64));if(!bitmap) bitmap=mask;
        if(o && bitmap && bitmap->surface) {
            int width=cairo_image_surface_get_width(bitmap->surface),height=cairo_image_surface_get_height(bitmap->surface);o->surface=surface(width,height);cairo_t *copy=cairo_create(o->surface);cairo_set_source_surface(copy,bitmap->surface,0,0);cairo_paint(copy);cairo_destroy(copy);
            if(mask && mask!=bitmap && mask->surface && cairo_image_surface_get_width(mask->surface)>=width && cairo_image_surface_get_height(mask->surface)>=height) {
                cairo_surface_flush(o->surface);cairo_surface_flush(mask->surface);uint8_t *pixels=cairo_image_surface_get_data(o->surface),*bits=cairo_image_surface_get_data(mask->surface);int ds=cairo_image_surface_get_stride(o->surface),ms=cairo_image_surface_get_stride(mask->surface);
                for(int y=0;y<height;y++) for(int x=0;x<width;x++) { uint32_t pixel,bit;memcpy(&pixel,pixels+y*ds+x*4,4);memcpy(&bit,bits+y*ms+x*4,4);if(bit&0xffffff) pixel=0;memcpy(pixels+y*ds+x*4,&pixel,4); }cairo_surface_mark_dirty(o->surface);
            }
        }if(o && o->surface) o->modern_icon=modern_icon_identify(o->surface);const char *dump=getenv("WINBOX_DUMP_ICONS");if(dump && o && o->surface) { char path[1024];snprintf(path,sizeof path,"%s/%llx-icon.png",dump,(unsigned long long)h);cairo_surface_write_to_png(o->surface,path);if(bitmap && bitmap->surface) { snprintf(path,sizeof path,"%s/%llx-color.png",dump,(unsigned long long)h);cairo_surface_write_to_png(bitmap->surface,path); }if(mask && mask->surface) { snprintf(path,sizeof path,"%s/%llx-mask.png",dump,(unsigned long long)h);cairo_surface_write_to_png(mask->surface,path); } }RET(h);
    }
    if(IS("LoadImageA") && (A(2)==1 || A(2)==2) && !(A(5)&0x10)) {
        unsigned group_type=A(2)==1 ? 14 : 12,entry_type=A(2)==1 ? 3 : 1,hotspot=A(2)==2 ? 4 : 0;
        CPU lookup=*c;lookup.r[RCX]=A(0);lookup.r[RDX]=A(1);lookup.r[R8]=group_type;uint64_t group=0;host_api(&lookup,"FindResourceA",&group);if(!group) RET(0);
        uint64_t data=c->base+load(c,group,32);unsigned count=load(c,data+4,16),wanted=A(3) ? A(3) : 32,best=0;int score=INT_MAX;
        if(count>256) RET(0);for(unsigned i=0;i<count;i++) { uint64_t item=data+6+14*i;unsigned width=load(c,item,8);if(!width) width=256;int distance=abs((int)width-(int)wanted)*64+(32-(int)load(c,item+6,16));if(distance<score) { score=distance;best=i; } }
        lookup.r[RDX]=load(c,data+6+14*best+12,16);lookup.r[R8]=entry_type;uint64_t entry=0;host_api(&lookup,"FindResourceA",&entry);if(!entry) RET(0);
        uint64_t info=c->base+load(c,entry,32);unsigned bytes=load(c,entry+4,32),header=load(c,info+hotspot,32),bpp=load(c,info+hotspot+14,16),palette=load(c,info+hotspot+32,32);int width=load(c,info+hotspot+4,32),height=(int)load(c,info+hotspot+8,32)/2;
        if(header<40 || width<=0 || width>256 || height<=0 || height>256 || load(c,info+hotspot+16,32) || (bpp!=1 && bpp!=8 && bpp!=24 && bpp!=32)) RET(0);if(bpp<=8 && !palette) palette=1u<<bpp;
        size_t offset=hotspot+header+(bpp<=8 ? palette*4 : 0),stride=((size_t)width*bpp+31)/32*4,mask_stride=((size_t)width+31)/32*4;if(offset>bytes || (stride+mask_stride)*height>bytes-offset) RET(0);
        uint8_t *bits=memory(c,info+offset,(stride+mask_stride)*height);if(!bits) RET(0);uint64_t handle=new_gdi(A(2)==1 ? 6 : 7);Gdi *icon=gdi(handle);if(!icon) RET(0);icon->surface=surface(width,height);if(!icon->surface) RET(0);uint8_t *pixels=cairo_image_surface_get_data(icon->surface);int ds=cairo_image_surface_get_stride(icon->surface),has_alpha=0;
        if(bpp==32) for(int y=0;y<height;y++) for(int x=0;x<width;x++) if(bits[y*stride+4*x+3]) has_alpha=1;
        for(int y=0;y<height;y++) for(int x=0;x<width;x++) { uint8_t *row=bits+(height-1-y)*stride;uint32_t rgb;if(bpp==1) { unsigned index=(row[x/8]>>(7-x%8))&1;rgb=load(c,info+hotspot+header+4*index,32)&0xffffff; }else if(bpp==8) rgb=load(c,info+hotspot+header+4*row[x],32)&0xffffff;else { uint8_t *v=row+x*(bpp/8);rgb=v[0]|(v[1]<<8)|(v[2]<<16); }unsigned alpha=has_alpha ? row[x*4+3] : bits[stride*height+(height-1-y)*mask_stride+x/8]&(0x80>>(x%8)) ? 0 : 255;
            uint32_t pixel=(alpha<<24)|((((rgb>>16)&255)*alpha/255)<<16)|((((rgb>>8)&255)*alpha/255)<<8)|((rgb&255)*alpha/255);memcpy(pixels+y*ds+4*x,&pixel,4);
        }cairo_surface_mark_dirty(icon->surface);
        if(A(2)==2) {
            if(!video(c)) RET(0);
            SDL_Surface *image=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_ARGB8888,pixels,ds);SDL_Cursor *cursor=image ? SDL_CreateColorCursor(image,load(c,info,16),load(c,info+2,16)) : NULL;if(image) SDL_DestroySurface(image);
            cairo_surface_destroy(icon->surface);icon->surface=NULL;
            if(!cursor) RET(0);icon->cursor=cursor;icon->cursor_shared=1;RET(handle);
        }
        RET(handle);
    }
    if(IS("LoadBitmapA") || IS("LoadImageA")) {
        uint64_t h=new_gdi(4);Gdi *o=gdi(h);if(!o) RET(0);
        if(IS("LoadImageA") && (A(5)&0x10) && A(1)>=65536) { char *path=text(c,A(1),0);for(char *q=path;*q;q++) if(*q=='\\') *q='/';o->surface=cairo_image_surface_create_from_png(path);free(path);if(cairo_surface_status(o->surface)!=CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(o->surface);o->surface=NULL; } }
        else {
            /* Resource lookup shares the guest address space without replacing
             * the caller's Win64 argument registers or stack arguments. */
            CPU lookup=*c;lookup.r[RCX]=A(0);lookup.r[RDX]=A(1);lookup.r[R8]=2;uint64_t entry=0;host_api(&lookup,"FindResourceA",&entry);
            if(lookup.failed) { c->failed=lookup.failed;o->used=0;RET(0); }
            if(entry) { uint64_t info=c->base+load(c,entry,32);uint32_t bytes=load(c,entry+4,32),header=load(c,info,32);int width=load(c,info+4,32),signed_height=load(c,info+8,32);unsigned bpp=load(c,info+14,16),compression=load(c,info+16,32),colors=load(c,info+32,32);
                if(header>=40 && header<=bytes && width>0 && width<=8192 && signed_height && signed_height>=-8192 && signed_height<=8192 && !compression && (bpp==1 || bpp==4 || bpp==8 || bpp==24 || bpp==32)) {
                    int height=abs(signed_height);if(bpp<=8 && !colors) colors=1u<<bpp;size_t offset=header+(bpp<=8 ? colors*4 : 0),stride=((size_t)width*bpp+31)/32*4;
                    if(offset<=bytes && stride*height<=bytes-offset) { uint8_t *bits=memory(c,info+offset,stride*height);o->surface=surface(width,height);if(bits && o->surface) { uint8_t *pixels=cairo_image_surface_get_data(o->surface);int ds=cairo_image_surface_get_stride(o->surface);
                        for(int y=0;y<height;y++) for(int x=0;x<width;x++) { uint8_t *row=bits+(signed_height<0 ? y : height-1-y)*stride;uint32_t pixel;
                            if(bpp<=8) { unsigned index=bpp==8 ? row[x] : bpp==4 ? (row[x/2]>>(x%2 ? 0 : 4))&15 : (row[x/8]>>(7-x%8))&1;pixel=0xff000000u|(uint32_t)load(c,info+header+index*4,32); }
                            else { uint8_t *v=row+x*(bpp/8);pixel=0xff000000u|v[0]|(v[1]<<8)|(v[2]<<16); }memcpy(pixels+y*ds+x*4,&pixel,4);
                        }cairo_surface_mark_dirty(o->surface);
                    } }
                }
            }
        }
        if(!o->surface) { o->used=0;RET(0); }RET(h);
    }
    if(IS("DrawIconEx")) { DC *d=dc(A(0));Gdi *o=gdi(A(3));if(getenv("WINBOX_DUMP_ICONS") && d) { double x1,y1,x2,y2;cairo_clip_extents(d->cr,&x1,&y1,&x2,&y2);cairo_matrix_t matrix;cairo_get_matrix(d->cr,&matrix);fprintf(stderr,"DrawIcon %llx at %d,%d size %lld,%lld flags=%llx clip=%g,%g,%g,%g origin=%g,%g status=%d\n",(unsigned long long)A(3),(int)A(1),(int)A(2),(long long)A(4),(long long)A(5),(unsigned long long)A(8),x1,y1,x2,y2,matrix.x0,matrix.y0,cairo_status(d->cr)); }if(d && o && o->surface) { cairo_save(d->cr);cairo_translate(d->cr,(int)A(1),(int)A(2));int w=cairo_image_surface_get_width(o->surface),h=cairo_image_surface_get_height(o->surface);int width=(int32_t)A(4),height=(int32_t)A(5);if(width>0 && height>0 && w && h) cairo_scale(d->cr,(double)width/w,(double)height/h);if(!((uint32_t)A(8)&2) || !modern_icon_draw(d->cr,o->modern_icon,0,0,w,h)) { cairo_set_source_surface(d->cr,o->surface,0,0);cairo_paint(d->cr); }cairo_restore(d->cr); }RET(1); }
    if(IS("CreatePen")) { uint64_t h=new_gdi(2);Gdi *o=gdi(h);if(o) { o->width=A(1) ? A(1) : 1;o->color=A(2); }RET(h); }
    if(IS("CreateFontA") || IS("CreateFontIndirectW")) { uint64_t h=new_gdi(3);Gdi *o=gdi(h);if(o) { if(IS("CreateFontA")) { o->size=(int)A(0);o->font_width=(int)A(1);o->weight=(int)A(4);o->italic=(uint8_t)A(5);o->underline=(uint8_t)A(6);o->strike=(uint8_t)A(7);o->charset=(uint8_t)A(8);o->pitch=(uint8_t)A(12);char *s=text(c,A(13),0);snprintf(o->face,sizeof o->face,"%s",s);free(s); }else { o->size=(int)load(c,A(0),32);o->font_width=(int)load(c,A(0)+4,32);o->weight=load(c,A(0)+16,32);o->italic=load(c,A(0)+20,8);o->underline=load(c,A(0)+21,8);o->strike=load(c,A(0)+22,8);o->charset=load(c,A(0)+23,8);o->pitch=load(c,A(0)+27,8);char *s=wide(c,A(0)+28);snprintf(o->face,sizeof o->face,"%s",s);free(s); } }RET(h); }
    if(IS("GetStockObject")) { if(!video(c)) RET(0);static uint64_t stock[32];unsigned n=A(0);if(n>=32) RET(0);if(!stock[n]) { stock[n]=new_gdi(n>=10 && n<=17 ? 3 : n>=6 && n<=8 ? 2 : 1);Gdi *o=gdi(stock[n]);o->color=n==0 || n==6 ? 0xffffff : n==4 ? 0x808080 : 0;o->size=dpi_units(11);o->width=1;strcpy(o->face,n==17 ? "MS Shell Dlg" : "Tahoma");if(n==17) o->pitch=0x22;if(n==10 || n==11 || n==16) { o->size=dpi_units(12);strcpy(o->face,"Courier New");o->pitch=0x31;o->charset=n==10 ? 255 : 0; } }RET(stock[n]); }
    if(IS("SelectObject")) { DC *d=dc(A(0));Gdi *o=gdi(A(1));if(!d || !o) RET(0);uint64_t old=0;if(o->type==1) { old=d->brush;d->brush=A(1); }else if(o->type==2) { old=d->pen;d->pen=A(1); }else if(o->type==3) { old=d->font;d->font=A(1);font(d); }else if(o->type==4 && o->surface) { old=d->bitmap;d->bitmap=A(1);cairo_destroy(d->cr);d->surface=o->surface;d->cr=cairo_create(d->surface);font(d); }RET(old); }
    if(IS("DeleteObject")) { Gdi *o=gdi(A(0));if(o) { if(o->surface) cairo_surface_destroy(o->surface);o->used=0; }RET(1); }
    if(IS("SetTextColor") || IS("SetBkColor") || IS("GetTextColor") || IS("SetBkMode")) { DC *d=dc(A(0));if(!d) RET(0);if(IS("GetTextColor")) RET(d->text);uint32_t old=IS("SetTextColor") ? d->text : IS("SetBkColor") ? d->bg : d->bgmode;if(IS("SetTextColor")) d->text=A(1);else if(IS("SetBkColor")) d->bg=A(1);else d->bgmode=A(1);RET(old); }
    if(IS("FillRect") || IS("FrameRect")) { DC *d=dc(A(0));if(!d) RET(0);int x=load(c,A(1),32),y=load(c,A(1)+4,32),r=load(c,A(1)+8,32),b=load(c,A(1)+12,32);Gdi *o=gdi(A(2));color(d->cr,o ? o->color : syscolor((unsigned)A(2)-1));cairo_rectangle(d->cr,x,y,r-x,b-y);if(IS("FrameRect")) { cairo_set_line_width(d->cr,1);cairo_stroke(d->cr); }else cairo_fill(d->cr);RET(1); }
    if(IS("BitBlt")) {
        DC *d=dc(A(0)),*src=dc(A(5));if(!d || !src) RET(0);int x=(int)A(1),y=(int)A(2),width=(int)A(3),height=(int)A(4);if(width<=0 || height<=0 || width>8192 || height>8192) RET(0);
        unsigned rop=(A(8)>>16)&255;cairo_surface_t *pixels=NULL;
        if(rop!=0xcc) {
            pixels=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);cairo_surface_flush(d->surface);cairo_surface_flush(src->surface);
            uint8_t *out=cairo_image_surface_get_data(pixels),*dp=cairo_image_surface_get_data(d->surface),*sp=cairo_image_surface_get_data(src->surface);int os=cairo_image_surface_get_stride(pixels),ds=cairo_image_surface_get_stride(d->surface),ss=cairo_image_surface_get_stride(src->surface);
            int dw=cairo_image_surface_get_width(d->surface),dh=cairo_image_surface_get_height(d->surface),sw=cairo_image_surface_get_width(src->surface),sh=cairo_image_surface_get_height(src->surface);
            double dx=x,dy=y,sx=(int)A(6),sy=(int)A(7);cairo_user_to_device(d->cr,&dx,&dy);cairo_user_to_device(src->cr,&sx,&sy);Gdi *brush=gdi(d->brush);uint32_t pattern=brush ? ((brush->color&255)<<16)|(brush->color&0xff00)|((brush->color>>16)&255) : 0;
            for(int row=0;row<height;row++) for(int col=0;col<width;col++) { int tx=(int)dx+col,ty=(int)dy+row,fx=(int)sx+col,fy=(int)sy+row;uint32_t result=0;
                if(tx>=0 && tx<dw && ty>=0 && ty<dh && fx>=0 && fx<sw && fy>=0 && fy<sh) { uint32_t a,b;memcpy(&a,sp+fy*ss+fx*4,4);memcpy(&b,dp+ty*ds+tx*4,4);
                    for(unsigned bit=0;bit<8;bit++) if(rop&(1u<<bit)) result|=(bit&4 ? pattern : ~pattern)&(bit&2 ? a : ~a)&(bit&1 ? b : ~b);result|=0xff000000;
                }memcpy(out+row*os+col*4,&result,4);
            }cairo_surface_mark_dirty(pixels);
        }
        cairo_save(d->cr);cairo_rectangle(d->cr,x,y,width,height);cairo_clip(d->cr);cairo_set_source_surface(d->cr,pixels ? pixels : src->surface,pixels ? x : x-(int)A(6),pixels ? y : y-(int)A(7));cairo_paint(d->cr);cairo_restore(d->cr);if(pixels) cairo_surface_destroy(pixels);RET(1);
    }
    if(IS("DrawEdge") || IS("DrawFocusRect")) { DC *d=dc(A(0));if(!d) RET(0);int x=load(c,A(1),32),y=load(c,A(1)+4,32),r=load(c,A(1)+8,32),b=load(c,A(1)+12,32);cairo_save(d->cr);cairo_set_line_width(d->cr,1);if(IS("DrawFocusRect")) { double dash[]={1,1};cairo_set_dash(d->cr,dash,2,0);color(d->cr,0);cairo_rectangle(d->cr,x+.5,y+.5,r-x-1,b-y-1);cairo_stroke(d->cr); }else { color(d->cr,(A(2)&(2|8)) ? syscolor(16) : syscolor(20));cairo_move_to(d->cr,x+.5,b-.5);cairo_line_to(d->cr,x+.5,y+.5);cairo_line_to(d->cr,r-.5,y+.5);cairo_stroke(d->cr);color(d->cr,(A(2)&(2|8)) ? syscolor(20) : syscolor(16));cairo_move_to(d->cr,r-.5,y+.5);cairo_line_to(d->cr,r-.5,b-.5);cairo_line_to(d->cr,x+.5,b-.5);cairo_stroke(d->cr);if(A(3)&0x2000) rectangle(c,A(1),x+2,y+2,r-2,b-2); }cairo_restore(d->cr);RET(1); }
    if(IS("SetROP2")) { DC *d=dc(A(0));if(d) cairo_set_operator(d->cr,A(1)==7 ? CAIRO_OPERATOR_DIFFERENCE : CAIRO_OPERATOR_OVER);RET(13); }
    if(IS("GetPixel")) { DC *d=dc(A(0));if(!d) RET(UINT32_MAX);int x=A(1),y=A(2);if(x<0 || y<0 || x>=cairo_image_surface_get_width(d->surface) || y>=cairo_image_surface_get_height(d->surface)) RET(UINT32_MAX);cairo_surface_flush(d->surface);uint32_t pixel;memcpy(&pixel,cairo_image_surface_get_data(d->surface)+y*cairo_image_surface_get_stride(d->surface)+4*x,4);RET(((pixel>>16)&255)|(pixel&0xff00)|((pixel&255)<<16)); }
    if(IS("ScrollWindowEx")) { Window *w=window(A(0));if(w) enqueue(w->hwnd,0xf,0,0);RET(2); }
    if(IS("GetDIBits")) { Gdi *o=gdi(A(1));if(!o || !o->surface) RET(0);int w=cairo_image_surface_get_width(o->surface),h=cairo_image_surface_get_height(o->surface);unsigned bpp=load(c,A(5)+14,16);if(!A(4)) { store(c,A(5),32,40);store(c,A(5)+4,32,w);store(c,A(5)+8,32,h);store(c,A(5)+12,16,1);store(c,A(5)+14,16,32);store(c,A(5)+16,32,0);RET(h); }if(bpp!=24 && bpp!=32) RET(0);unsigned start=A(2),count=A(3);if(start>=(unsigned)h) RET(0);if(count>(unsigned)h-start) count=h-start;size_t stride=((w*bpp+31)/32)*4;uint8_t *out=writable_memory(c,A(4),stride*count);if(!out) RET(0);cairo_surface_flush(o->surface);uint8_t *data=cairo_image_surface_get_data(o->surface);int ds=cairo_image_surface_get_stride(o->surface),top=(int32_t)load(c,A(5)+8,32)<0;for(unsigned y=0;y<count;y++) for(int x=0;x<w;x++) { uint32_t pixel;memcpy(&pixel,data+(top ? start+y : h-1-start-y)*ds+x*4,4);out[y*stride+x*(bpp/8)]=pixel&255;out[y*stride+x*(bpp/8)+1]=(pixel>>8)&255;out[y*stride+x*(bpp/8)+2]=(pixel>>16)&255;if(bpp==32) out[y*stride+x*4+3]=255; }RET(count); }
    if(IS("MoveToEx") || IS("LineTo")) { DC *d=dc(A(0));if(!d) RET(0);int x=A(1),y=A(2);if(IS("LineTo")) { Gdi *p=gdi(d->pen);color(d->cr,p ? p->color : 0);cairo_set_line_width(d->cr,p ? p->width : 1);cairo_move_to(d->cr,d->x+.5,d->y+.5);cairo_line_to(d->cr,x+.5,y+.5);cairo_stroke(d->cr); }else if(A(3)) { store(c,A(3),32,d->x);store(c,A(3)+4,32,d->y); }d->x=x;d->y=y;RET(1); }
    if(IS("TextOutA") || IS("DrawTextExA") || IS("DrawTextExW") || IS("GetTextExtentExPointA")) {
        DC *d=dc(A(0));if(!d) RET(0);int drawing=IS("DrawTextExA") || IS("DrawTextExW");uint64_t p=drawing || IS("GetTextExtentExPointA") ? A(1) : A(3);char *s=text(c,p,IS("DrawTextExW"));int length=(int)(drawing || IS("GetTextExtentExPointA") ? A(2) : A(4));if(length>=0 && (size_t)length<strlen(s) && !IS("DrawTextExW")) s[length]=0;size_t source_length=strlen(s);char *safe=display_utf8(s);free(s);if(!safe) { trap(c,"out of memory preparing GUI text");RET(0); }s=safe;const char *test=getenv("WINBOX_TEST_INPUT");if(!IS("GetTextExtentExPointA") && test && !strcmp(s,test)) ui.test_input_observed=1;font(d);
        cairo_text_extents_t ext;cairo_font_extents_t fe;cairo_text_extents(d->cr,s,&ext);cairo_font_extents(d->cr,&fe);FontMetrics metrics=font_metrics(d);int width=(int)ceil(ext.x_advance),height=metrics.height;
        if(IS("GetTextExtentExPointA")) { if(A(6)) { store(c,A(6),32,width);store(c,A(6)+4,32,height); }if(A(4)) store(c,A(4),32,source_length);if(A(5)) for(size_t i=0;i<source_length;i++) store(c,A(5)+i*4,32,(uint32_t)((i+1)*ext.x_advance/(source_length ? source_length : 1)));free(s);RET(1); }
        int x=A(1),y=A(2);int clipped=0;if(drawing) { x=(int)load(c,A(3),32);y=(int)load(c,A(3)+4,32);int r=load(c,A(3)+8,32),b=load(c,A(3)+12,32);unsigned flags=A(4);int x0=x,y0=y;
            if(flags&0x400) { rectangle(c,A(3),x,y,x+width,y+height);free(s);RET(height); }
            if((flags&0x8000) && width>r-x0) { int avail=r-x0>0 ? r-x0 : 0;size_t keep=strlen(s);
                while(keep) { size_t previous=keep-1;while(previous && (s[previous]&0xc0)==0x80) previous--;while(previous && s[previous-1]==' ') previous--;
                    char *shorter=malloc(previous+4);if(!shorter) break;memcpy(shorter,s,previous);memcpy(shorter+previous,"...",3);shorter[previous+3]=0;
                    cairo_text_extents_t shortened;cairo_text_extents(d->cr,shorter,&shortened);
                    if((int)ceil(shortened.x_advance)<=avail) { free(s);s=shorter;width=(int)ceil(shortened.x_advance);break; }
                    free(shorter);keep=previous;
                }
            }
            if(flags&1) x+=(r-x-width)/2;else if(flags&2) x=r-width;
            if(flags&4) y+=(b-y-height)/2;if(A(5)) store(c,A(5)+16,32,source_length);
            if(!(flags&0x100)) { cairo_save(d->cr);cairo_rectangle(d->cr,x0,y0,r-x0,b-y0);cairo_clip(d->cr);clipped=1; }
        }

        const char *menu_choice=getenv("WINBOX_TEST_MENU_SELECT");if(menu_choice && !ui.test_popup && (IS("TextOutA") || drawing)) { char label[256];size_t used=0;for(const char *p=s;*p && *p!='\t' && used<255;p++) if(*p!='&') label[used++]=*p;label[used]=0;if(!strcmp(label,menu_choice)) { Window *popup=window(d->window);while(popup && !popup->native && window(popup->parent)) popup=window(popup->parent);if(!popup || !popup->native || strcmp(popup->class_name,"routeros_canvas") || !(popup->style&0x80000000)) { popup=NULL;for(int i=MAX_WINDOWS-1;i>0;i--) if(ui.windows[i].used && ui.windows[i].native && !strcmp(ui.windows[i].class_name,"routeros_canvas") && (ui.windows[i].style&0x80000000)) { popup=&ui.windows[i];break; } }if(popup) { double px=x+width/2.,py=y+height/2.;cairo_user_to_device(d->cr,&px,&py);ui.test_popup=popup->hwnd;ui.test_popup_at=SDL_GetTicks()+200;ui.test_popup_x=px;ui.test_popup_y=py; } } }
        if(IS("TextOutA") && getenv("WINBOX_TEST_TERMINAL")) { static char row[2048];static size_t used;static int last_y=-1,last_x=-1,reported;if(y!=last_y || x<last_x) used=0;size_t count=strlen(s);if(count<sizeof row-used) { memcpy(row+used,s,count);used+=count;row[used]=0;if(!reported && strstr(row,"WINBOX_NATIVE_TERMINAL_OK")) { fprintf(stderr,"WinBox smoke terminal output verified\n");reported=1; } }last_y=y;last_x=x; }
        if(d->bgmode==2) { color(d->cr,d->bg);cairo_rectangle(d->cr,x,y,width,height);cairo_fill(d->cr); }color(d->cr,d->text);cairo_move_to(d->cr,x,y+metrics.ascent);cairo_show_text(d->cr,s);if(clipped) cairo_restore(d->cr);free(s);RET(drawing ? height : 1);
    }
    if(IS("GetTextMetricsA")) { DC *d=dc(A(0));if(!d) RET(0);font(d);FontMetrics m=font_metrics(d);Gdi *f=gdi(d->font);uint8_t *p=writable_memory(c,A(1),56);if(p) memset(p,0,56);int values[]={m.height,m.ascent,m.descent,m.internal,m.external,m.average,m.maximum,f && f->weight ? f->weight : 400,0,dpi_units(96),dpi_units(96)};for(unsigned i=0;i<11;i++) store(c,A(1)+4*i,32,values[i]);cairo_text_extents_t thin,wide;cairo_text_extents(d->cr,"i",&thin);cairo_text_extents(d->cr,"W",&wide);int fixed=fabs(thin.x_advance-wide.x_advance)<0.01;store(c,A(1)+44,8,32);store(c,A(1)+45,8,255);store(c,A(1)+46,8,'?');store(c,A(1)+47,8,32);store(c,A(1)+48,8,f ? f->italic : 0);store(c,A(1)+49,8,f ? f->underline : 0);store(c,A(1)+50,8,f ? f->strike : 0);store(c,A(1)+51,8,fixed ? 0x36 : 0x27);store(c,A(1)+52,8,f ? f->charset : 0);RET(1); }
    if(IS("GetCharWidth32A") || IS("GetCharWidthA")) { DC *d=dc(A(0));if(!d) RET(0);font(d);for(uint64_t n=A(1);n<=A(2) && n<256;n++) { char s[2]={(char)(n<128 ? n : '?'),0};cairo_text_extents_t ext;cairo_text_extents(d->cr,s,&ext);store(c,A(3)+(n-A(1))*4,32,(int)ceil(ext.x_advance)); }RET(1); }
    if(IS("GetDeviceCaps")) { switch(A(1)) { case 88:case 90:video(c);RET(dpi_units(96));case 12:RET(32);case 14:RET(1);case 8:video(c);RET(work_area().w);case 10:video(c);RET(work_area().h);default:RET(0); } }
    if(IS("GetClipBox")) { DC *d=dc(A(0));if(!d) RET(0);double x,y,r,b;cairo_clip_extents(d->cr,&x,&y,&r,&b);rectangle(c,A(1),x,y,r,b);RET(2); }
    if(IS("IntersectClipRect") || IS("ExcludeClipRect")) { DC *d=dc(A(0));if(!d) RET(0);if(IS("ExcludeClipRect")) { cairo_rectangle(d->cr,0,0,cairo_image_surface_get_width(d->surface),cairo_image_surface_get_height(d->surface));cairo_set_fill_rule(d->cr,CAIRO_FILL_RULE_EVEN_ODD); }cairo_rectangle(d->cr,(int)A(1),(int)A(2),(int)A(3)-(int)A(1),(int)A(4)-(int)A(2));cairo_clip(d->cr);RET(2); }
    if(IS("SaveDC")) { DC *d=dc(A(0));if(!d || d->save_depth>=1024) RET(0);DCState *state=malloc(sizeof *state);if(!state) RET(0);*state=(DCState){d->font,d->brush,d->pen,d->bitmap,d->text,d->bg,d->bgmode,d->x,d->y,cairo_reference(d->cr),d->surface,d->saved};d->saved=state;cairo_save(d->cr);RET(++d->save_depth); }
    if(IS("RestoreDC")) { DC *d=dc(A(0));if(!d) RET(0);int level=(int)A(1);if(level<0) level=d->save_depth+level+1;if(level<1 || level>d->save_depth) RET(0);while(d->save_depth>=level) { DCState *state=d->saved;if(!state) RET(0);if(d->cr!=state->cr) { cairo_destroy(d->cr);d->cr=cairo_reference(state->cr); }d->surface=state->surface;cairo_restore(d->cr);d->font=state->font;d->brush=state->brush;d->pen=state->pen;d->bitmap=state->bitmap;d->text=state->text;d->bg=state->bg;d->bgmode=state->bgmode;d->x=state->x;d->y=state->y;d->saved=state->next;cairo_destroy(state->cr);free(state);d->save_depth--; }RET(1); }
    if(IS("SelectClipRgn")) { DC *d=dc(A(0));if(d && !A(1)) cairo_reset_clip(d->cr);RET(2); }
    if(IS("OffsetViewportOrgEx") || IS("OffsetWindowOrgEx")) { DC *d=dc(A(0));if(d) cairo_translate(d->cr,(int)A(1),(int)A(2));if(A(3)) { store(c,A(3),32,0);store(c,A(3)+4,32,0); }RET(1); }
    if(IS("PtVisible") || IS("RectVisible")) RET(1);
    if(IS("SetFocus") || IS("SetActiveWindow") || IS("SetCapture")) {
        uint64_t target=A(0),*slot=IS("SetFocus") ? &ui.focus : IS("SetActiveWindow") ? &ui.active : &ui.capture,old=*slot;
        if(target && !window(target)) { c->last_error=1400;RET(0); }if(old==target) RET(old);*slot=target;
        if(IS("SetFocus")) { invoke(c,window(old),8,target,0);if(ui.focus==target && window(target)) { Window *root=window(target);while(root && !root->native && window(root->parent)) root=window(root->parent);if(root && root->native) { ui.active=coordinate_space(root);if(!root->native_popup && SDL_GetKeyboardFocus()!=root->native) SDL_RaiseWindow(root->native); }invoke(c,window(target),7,old,0); } }
        if(IS("SetActiveWindow")) raise_mdi(window(target));
        RET(old);
    }
    if(IS("GetFocus")) RET(ui.focus);
    if(IS("GetActiveWindow")) RET(ui.active);
    if(IS("GetCapture")) RET(ui.capture);
    if(IS("ReleaseCapture")) { ui.capture=0;RET(1); }
    if(IS("GetDoubleClickTime")) RET(500);
    if(IS("GetCursorPos")) { if(ui.ready) { const char *driver=SDL_GetCurrentVideoDriver();if(driver && (!strcmp(driver,"x11") || !strcmp(driver,"windows") || !strcmp(driver,"cocoa"))) { float x,y;SDL_ClearError();SDL_GetGlobalMouseState(&x,&y);if(!*SDL_GetError()) { ui.cursor_x=guest_pixels(NULL,x);ui.cursor_y=guest_pixels(NULL,y); } } }store(c,A(0),32,ui.cursor_x);store(c,A(0)+4,32,ui.cursor_y);RET(1); }
    if(IS("GetMessagePos")) RET(lparam(ui.has_message_position ? ui.message_x : ui.cursor_x,ui.has_message_position ? ui.message_y : ui.cursor_y));
    if(IS("GetKeyState")) { unsigned key=(uint32_t)A(0);SDL_Keymod mod=ui.input_state ? ui.message_modifiers : SDL_GetModState();uint32_t buttons=ui.input_state ? ui.message_buttons : SDL_GetMouseState(NULL,NULL);unsigned bit=key==1 ? SDL_BUTTON_LMASK : key==2 ? SDL_BUTTON_RMASK : key==4 ? SDL_BUTTON_MMASK : key==5 ? SDL_BUTTON_X1MASK : key==6 ? SDL_BUTTON_X2MASK : 0;if(bit) RET(buttons&bit ? 0x8000 : 0);
        unsigned mask=key==16 ? SDL_KMOD_SHIFT : key==17 ? SDL_KMOD_CTRL : key==18 ? SDL_KMOD_ALT : key==160 ? SDL_KMOD_LSHIFT : key==161 ? SDL_KMOD_RSHIFT : key==162 ? SDL_KMOD_LCTRL : key==163 ? SDL_KMOD_RCTRL : key==164 ? SDL_KMOD_LALT : key==165 ? SDL_KMOD_RALT : 0;if(mask) RET(mod&mask ? 0x8000 : 0);if(key==20) RET((mod&SDL_KMOD_CAPS ? 1 : 0)|(ui.keys[key]&0x80 ? 0x8000 : 0));if(key==144) RET((mod&SDL_KMOD_NUM ? 1 : 0)|(ui.keys[key]&0x80 ? 0x8000 : 0));RET(key<256 ? (ui.keys[key]&0x80 ? 0x8000 : 0)|(ui.keys[key]&1) : 0); }
    if(IS("LoadCursorA")) RET(system_cursor(c,(unsigned)A(1)));
    if(IS("GetCursor")) RET(current_cursor(c));
    if(IS("SetCursor")) RET(change_cursor(c,A(0)));
    if(IS("CreateCaret")) { Window *owner=window(A(0));if(!owner) RET(0);int width=(int32_t)A(2),height=(int32_t)A(3);Gdi *bitmap=A(1)>1 ? gdi(A(1)) : NULL;if(bitmap && bitmap->surface) { width=cairo_image_surface_get_width(bitmap->surface);height=cairo_image_surface_get_height(bitmap->surface); }if(width<0 || height<0 || width>8192 || height>8192) { c->last_error=87;RET(0); }caret_destroy();ui.caret.owner=owner->hwnd;ui.caret.w=width ? width : 1;ui.caret.h=height ? height : 1;ui.caret.visibility=-1;ui.caret.gray=A(1)==1;if(bitmap && bitmap->surface) ui.caret.bitmap=cairo_surface_reference(bitmap->surface);caret_changed();RET(1); }
    if(IS("DestroyCaret")) { int exists=ui.caret.owner!=0;caret_destroy();RET(exists); }
    if(IS("ShowCaret") || IS("HideCaret")) { if(!ui.caret.owner || (A(0) && A(0)!=ui.caret.owner)) RET(0);ui.caret.visibility+=IS("ShowCaret") ? 1 : -1;caret_changed();RET(1); }
    if(IS("SetCaretPos")) { if(!ui.caret.owner) RET(0);ui.caret.x=(int32_t)A(0);ui.caret.y=(int32_t)A(1);caret_changed();RET(1); }
    if(IS("GetCaretPos")) { if(!ui.caret.owner || !A(0)) RET(0);store(c,A(0),32,ui.caret.x);store(c,A(0)+4,32,ui.caret.y);RET(1); }
    if(IS("GetCaretBlinkTime")) RET(ui.caret_blink_ms ? ui.caret_blink_ms : 530);
    if(IS("SetCaretBlinkTime")) { ui.caret_blink_ms=(uint32_t)A(0);caret_changed();RET(1); }
    if(IS("ShowCursor")) { ui.cursor_show_count+=A(0) ? 1 : -1;if(ui.ready) { if(ui.cursor_show_count>=0 && current_cursor(c)) SDL_ShowCursor();else SDL_HideCursor(); }RET(ui.cursor_show_count); }
    if(IS("OpenClipboard")) { if(ui.clipboard_open) { c->last_error=5;RET(0); }ui.clipboard_open=1;RET(1); }
    if(IS("CloseClipboard")) { ui.clipboard_open=0;RET(1); }
    if(IS("IsClipboardFormatAvailable")) RET((A(0)==1 || A(0)==13) && video(c) && SDL_HasClipboardText());
    if(IS("EmptyClipboard")) { if(!ui.clipboard_open) RET(0);for(unsigned i=0;i<17;i++) { guest_free(c,ui.clipboard_handles[i]);ui.clipboard_handles[i]=0; }RET(video(c) && SDL_SetClipboardText("")); }
    if(IS("GetClipboardData")) {
        unsigned format=A(0);if(!ui.clipboard_open || (format!=1 && format!=13) || !video(c) || !SDL_HasClipboardText()) RET(0);
        char *utf8=SDL_GetClipboardText();char *encoded=iconv_latin(format==13 ? "UTF-16LE" : "WINDOWS-1252","UTF-8",utf8,strlen(utf8)+1);SDL_free(utf8);if(!encoded) RET(0);
        size_t bytes=0;if(format==13) { while(encoded[bytes] || encoded[bytes+1]) bytes+=2;bytes+=2; }else bytes=strlen(encoded)+1;
        uint64_t handle=guest_alloc(c,bytes);uint8_t *out=writable_memory(c,handle,bytes);if(out) memcpy(out,encoded,bytes);SDL_free(encoded);
        guest_free(c,ui.clipboard_handles[format]);ui.clipboard_handles[format]=handle;RET(handle);
    }
    if(IS("SetClipboardData")) {
        unsigned format=A(0);if(!ui.clipboard_open || !A(1) || (format!=1 && format!=13) || !video(c)) RET(0);
        char *utf8=NULL;
        if(format==13) utf8=wide(c,A(1));else { char *input=guest_string(c,A(1));if(input) utf8=iconv_latin("UTF-8","WINDOWS-1252",input,strlen(input)+1); }
        if(!utf8) RET(0);int rc=SDL_SetClipboardText(utf8);if(format==13) free(utf8);else SDL_free(utf8);
        if(!rc) RET(0);if(ui.clipboard_handles[format]!=A(1)) guest_free(c,ui.clipboard_handles[format]);ui.clipboard_handles[format]=A(1);RET(A(1));
    }
    if(IS("CreateMenu") || IS("CreatePopupMenu")) RET(new_gdi(5));
    if(IS("GetMenu")) { Window *w=window(A(0));RET(w ? w->menu : 0); }
    if(IS("SetMenu")) { Window *w=window(A(0));if(w) { w->menu=A(1);enqueue(w->hwnd,5,0,lparam(w->w,w->h-menu_height(w)));enqueue(w->hwnd,0xf,0,0);request_present(w); }RET(1); }
    if(IS("GetMenuItemCount")) { Menu *m=menu(A(0));RET(m ? (int)m->count : -1); }
    if(IS("InsertMenuItemA")) { Menu *m=menu(A(0));if(!m || m->count>=1024) RET(0);size_t index=m->count;if(A(2)) { if(A(1)<index) index=A(1); }else for(size_t i=0;i<m->count;i++) if(m->items[i].id==A(1)) { index=i;break; }MenuItem *items=realloc(m->items,(m->count+1)*sizeof *items);if(!items) RET(0);m->items=items;memmove(items+index+1,items+index,(m->count-index)*sizeof *items);memset(items+index,0,sizeof *items);m->count++;menu_info(c,items+index,A(3));RET(!c->failed); }
    if(IS("SetMenuItemInfoA")) { MenuItem *item=menu_item(menu(A(0)),A(1),A(2));if(!item) RET(0);menu_info(c,item,A(3));RET(!c->failed); }
    if(IS("EnableMenuItem")) { MenuItem *item=menu_item(menu(A(0)),A(1),A(2)&0x400);if(!item) RET(-1);unsigned old=item->state&3;item->state=(item->state&~3u)|(A(2)&3);RET(old); }
    if(IS("DestroyMenu")) { Gdi *g=gdi(A(0));if(!g || g->type!=5) RET(0);for(size_t i=0;i<g->menu.count;i++) free(g->menu.items[i].text);free(g->menu.items);g->menu.items=NULL;g->menu.count=0;g->used=0;RET(1); }
    if(IS("TrackPopupMenuEx")) { uint32_t command=popup_menu(c,A(0),A(4),(int)A(2),(int)A(3),0,NULL,0,0);if(A(1)&0x100) RET(command);if(command && !(A(1)&0x80)) enqueue(A(4),0x111,command,0);RET(command!=0); }
    if(IS("CreateAcceleratorTableA") || IS("RegisterWindowMessageA") || IS("RegisterClipboardFormatA")) RET(0xc000+(++ui.clipboard_format));
    if(IS("TranslateAcceleratorA")) RET(0);
    if(IS("GetWindowTextLengthA")) { Window *w=window(A(0));RET(w ? strlen(w->title) : 0); }
    if(IS("GetWindowTextA") || IS("GetClassNameA")) { Window *w=window(A(0));const char *s=w ? (IS("GetClassNameA") ? w->class_name : w->title) : "";size_t n=strlen(s);if(!A(2)) RET(0);if(n>=A(2)) n=A(2)-1;uint8_t *p=writable_memory(c,A(1),n+1);if(p) { memcpy(p,s,n);p[n]=0; }RET(n); }
    if(IS("GetWindowTextW")) { Window *w=window(A(0));if(!w || !A(2)) RET(0);size_t n=strlen(w->title);if(n>=A(2)) n=A(2)-1;for(size_t i=0;i<n;i++) store(c,A(1)+2*i,16,(unsigned char)w->title[i]);store(c,A(1)+2*n,16,0);RET(n); }
    if(IS("SetWindowTextA") || IS("SetWindowTextW")) { Window *w=window(A(0));if(w) { free(w->title);w->title=text(c,A(1),IS("SetWindowTextW"));if(w->native) SDL_SetWindowTitle(w->native,w->title);enqueue(w->hwnd,0xf,0,0); }RET(1); }
    if(IS("MessageBoxW")) { char *s=wide(c,A(1)),*title=wide(c,A(2));fprintf(stderr,"WinBox message: %s: %s\n",title,s);free(s);free(title);RET(1); }
    if(IS("DestroyCursor") || IS("DestroyIcon") || IS("LockWindowUpdate")) RET(1);
    if(IS("SetTimer")) { uint64_t id=A(1) ? A(1) : 1;unsigned empty=256;for(unsigned i=0;i<256;i++) { if(timers[i].hwnd==A(0) && timers[i].id==id) { timers[i].interval=A(2)<10 ? 10 : A(2);timers[i].next=SDL_GetTicks()+timers[i].interval;RET(id); }if(!timers[i].hwnd && empty==256) empty=i; }if(empty==256) RET(0);timers[empty].hwnd=A(0);timers[empty].id=id;timers[empty].interval=A(2)<10 ? 10 : A(2);timers[empty].next=SDL_GetTicks()+timers[empty].interval;RET(id); }
    if(IS("KillTimer")) { for(unsigned i=0;i<256;i++) if(timers[i].hwnd==A(0) && timers[i].id==A(1)) timers[i].hwnd=0;RET(1); }
    if(IS("MonitorFromRect")) RET(1);
    if(IS("GetMonitorInfoA")) { rectangle(c,A(1)+4,0,0,1280,800);rectangle(c,A(1)+20,0,0,1280,800);store(c,A(1)+36,32,1);RET(1); }
    if(IS("CoCreateInstance")) { if(A(4)) store(c,A(4),64,0);RET(0x80040154u); } /* No Windows COM classes are registered on the SDL host. */
    if(IS("CoTaskMemFree")) { guest_free(c,A(0));RET(0); }
    if(IS("RegisterDragDrop")) RET(0x80004001u);
    if(IS("RevokeDragDrop")) RET(0x80040100u);
    if(IS("DoDragDrop")) { if(A(3)) store(c,A(3),32,0);RET(0x40101u); }
    if(IS("OleInitialize") || IS("CoInitializeEx")) RET(0);
    if(IS("InitCommonControls")) RET(0);
    return 0;
}
