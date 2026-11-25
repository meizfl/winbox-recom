/* Presentation-only theme: guest GDI colors, masks and ROPs stay unchanged. */
static struct { int initialized,dark;uint64_t hover,pressed; } theme;
extern const char *host_profile_path(void);
static void theme_init(void) {
    if(theme.initialized) return;theme.initialized=1;
    char path[PATH_MAX],value[16]={0};snprintf(path,sizeof path,"%s/theme.conf",host_profile_path());
    FILE *file=fopen(path,"r");if(file) { if(fgets(value,sizeof value,file)) theme.dark=!strncmp(value,"dark",4);fclose(file); }
    const char *env=getenv("WINBOX_THEME");if(env && (!strcmp(env,"dark") || !strcmp(env,"light"))) theme.dark=!strcmp(env,"dark");
}
static void theme_toggle(void) {
    theme_init();theme.dark=!theme.dark;
    setenv("WINBOX_THEME",theme.dark ? "dark" : "light",1);
    char path[PATH_MAX],temporary[PATH_MAX];snprintf(path,sizeof path,"%s/theme.conf",host_profile_path());snprintf(temporary,sizeof temporary,"%s/theme.conf.%ld.tmp",host_profile_path(),(long)getpid());
    FILE *file=fopen(temporary,"w");if(file) { int ok=fputs(theme.dark ? "dark\n" : "light\n",file)>=0;if(fclose(file)) ok=0;if(!ok || rename(temporary,path)) unlink(temporary); }
    for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].native) request_present(&ui.windows[i]);
    fprintf(stderr,"WinBox theme: %s\n",theme.dark ? "dark" : "light");
}
#include "ui_colors.h"
typedef struct { int x,y,w,h;Window *window;cairo_surface_t *caption;int caption_y,active; } ThemeFrame;
static cairo_surface_t *theme_copy_region(cairo_surface_t *source,int x,int y,int width,int height) {
    if(width<=0 || height<=0) return NULL;cairo_surface_t *copy=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);cairo_t *cr=cairo_create(copy);cairo_set_source_surface(cr,source,-x,-y);cairo_set_operator(cr,CAIRO_OPERATOR_SOURCE);cairo_paint(cr);cairo_destroy(cr);return copy;
}
static int theme_blue(uint32_t pixel) { int r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;return b>r+40 && b>g+40 && r<160 && g<160; }
static uint32_t theme_terminal_pixel(uint32_t pixel) {
    int r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
    if(r==255 && g==127 && b==191) return 0xffed80b7; /* terminal block caret */
    int low=r<g ? r : g;if(b<low) low=b;int high=r>g ? r : g;if(b>high) high=b;
    if(high-low<24 || low==255) return theme_pixel(pixel);
    double coverage=(255-low)/255.;int fr=(int)((r-low)/coverage),fg=(int)((g-low)/coverage),fb=(int)((b-low)/coverage);
    int luma=(54*fr+183*fg+19*fb+128)>>8;
    if(luma<220) { double amount=(220-luma)/(double)(255-luma);fr+=(int)((255-fr)*amount);fg+=(int)((255-fg)*amount);fb+=(int)((255-fb)*amount); }
    r=(int)(17+(fr-17)*coverage);g=(int)(19+(fg-19)*coverage);b=(int)(24+(fb-24)*coverage);
    return 0xff000000u|(r<<16)|(g<<8)|b;
}
static void theme_apply(cairo_surface_t *image,Window *root) {
    theme_init();if(!theme.dark) return;
    static uint32_t gray[256];static int ready;
    if(!ready) { for(unsigned i=0;i<256;i++) gray[i]=theme_pixel(0xff000000u|(i<<16)|(i<<8)|i);ready=1; }
    cairo_surface_flush(image);unsigned char *pixels=cairo_image_surface_get_data(image);int width=cairo_image_surface_get_width(image),height=cairo_image_surface_get_height(image),stride=cairo_image_surface_get_stride(image);
    ThemeFrame frames[64];unsigned count=0;cairo_surface_t *brand=NULL;int brand_x=0,brand_y=0;
    if(root) {
        
        for(unsigned i=1;i<MAX_WINDOWS && count<64;i++) {
            Window *w=&ui.windows[i];if(w!=root || !w->used || !w->title || !*w->title || strcmp(w->class_name,"routeros_dbl_canvas") || w->w<80 || w->h<40) continue;
            int visible=1;for(Window *p=w;p && !p->native;p=window(p->parent)) if(!p->visible) visible=0;if(!visible) continue;
            int x=0,y=0;
            if(x<0 || y<0 || x+w->w>width || y+w->h>height) continue;
            ThemeFrame *frame=&frames[count++];*frame=(ThemeFrame){x,y,w->w,w->h,w,NULL,y+2,0};int first=-1,last=-1;
            for(int row=2;row<dpi_units(64) && row<w->h-2;row++) {
                int blue=0,samples=0;for(int col=12;col<w->w-40;col+=16) { uint32_t pixel;memcpy(&pixel,pixels+(y+row)*stride+(x+col)*4,4);blue+=theme_blue(pixel);samples++; }
                if(samples && blue*2>samples) { if(first<0) first=row;last=row; }else if(first>=0) break;
            }
            frame->active=first>=0;int band_height=frame->active ? last-first+1 : dpi_units(18);frame->caption_y=y+(frame->active ? first : 2);
            frame->caption=theme_copy_region(image,x+2,frame->caption_y,w->w-4-(frame->active ? 0 : dpi_units(36)),band_height);
            if(!frame->active && frame->caption) {
                cairo_surface_flush(frame->caption);unsigned char *data=cairo_image_surface_get_data(frame->caption);int pitch=cairo_image_surface_get_stride(frame->caption),cw=cairo_image_surface_get_width(frame->caption);
                for(int py=0;py<band_height;py++) for(int px=0;px<cw;px++) { uint32_t pixel;memcpy(&pixel,data+py*pitch+px*4,4);int v=(54*((pixel>>16)&255)+183*((pixel>>8)&255)+19*(pixel&255)+128)>>8;v=v<191 ? 20+v*28/191 : 48+(v-191)*188/64;pixel=0xff000000u|(v<<16)|((v+2>255 ? 255 : v+2)<<8)|(v+6>255 ? 255 : v+6);memcpy(data+py*pitch+px*4,&pixel,4); }
                /* The inactive guest caption includes a raised bottom bevel.
                 * Flatten it into the caption background, rather than restoring
                 * the light GDI highlight over the dark window surface. */
                for(int py=band_height-dpi_units(2);py<band_height;py++) if(py>=0)
                    for(int px=0;px<cw;px++) { uint32_t background=0xff303236;memcpy(data+py*pitch+px*4,&background,4); }
                cairo_surface_mark_dirty(frame->caption);
            }
        }
        /* The original blue brand rail is an image, not a light UI surface. */
        if(width>=80 && height>=80) {
            int first=-1,last=-1,top=24;
            for(int x=0;x<64 && x<width;x++) { int blue=0,total=0;for(int y=top;y<height;y+=8) { uint32_t pixel;memcpy(&pixel,pixels+y*stride+x*4,4);blue+=theme_blue(pixel);total++; }if(total && blue*2>total) { if(first<0) first=x;last=x; }else if(first>=0) break; }
            if(first>=0) { int run=0;for(int y=0;y<height;y++) { uint32_t pixel;memcpy(&pixel,pixels+y*stride+first*4,4);run=theme_blue(pixel) ? run+1 : 0;if(run==10) { brand_x=first;brand_y=y-9;brand=theme_copy_region(image,first,brand_y,last-first+1,height-brand_y);break; } } }
        }
    }
    int terminal_context=0;for(Window *p=root;p;p=window(p->parent)) if(p->title && !strncmp(p->title,"Terminal",8)) { terminal_context=1;break; }
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
        uint32_t pixel;memcpy(&pixel,pixels+y*stride+4*x,4);unsigned r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;int terminal=0;
        terminal=terminal_context && (!count || y>frames[0].caption_y+dpi_units(20));
        pixel=terminal ? theme_terminal_pixel(pixel) : (pixel>>24)==255 && r==g && g==b ? gray[r] : theme_pixel(pixel);memcpy(pixels+y*stride+4*x,&pixel,4);
    }
    cairo_surface_mark_dirty(image);cairo_t *cr=cairo_create(image);
    if(brand) { cairo_set_source_surface(cr,brand,brand_x,brand_y);cairo_paint(cr);cairo_surface_destroy(brand); }
    for(unsigned i=0;i<count;i++) { ThemeFrame *frame=&frames[i];if(frame->caption) { cairo_set_source_surface(cr,frame->caption,frame->x+2,frame->caption_y);cairo_paint(cr);cairo_surface_destroy(frame->caption); }if(!frame->active) { cairo_set_source_rgb(cr,48/255.,50/255.,54/255.);cairo_rectangle(cr,frame->x+2,frame->caption_y+dpi_units(18),frame->w-4,dpi_units(2));cairo_fill(cr); }cairo_set_line_width(cr,1);cairo_set_source_rgb(cr,.08,.10,.13);cairo_rectangle(cr,frame->x+.5,frame->y+.5,frame->w-1,frame->h-1);cairo_stroke(cr);cairo_set_source_rgb(cr,.19,.21,.25);cairo_rectangle(cr,frame->x+1.5,frame->y+1.5,frame->w-3,frame->h-3);cairo_stroke(cr); }
    cairo_destroy(cr);
}
static int theme_control(Window *w,int x,int y) {
    if(!w || !w->native || !menu_height(w) || w->w<dpi_units(320) || (strcmp(w->class_name,"routeros_main") && strcmp(w->class_name,"routeros_connect"))) return 0;
    return x>=w->w-dpi_units(82) && x<w->w-dpi_units(4) && y>=dpi_units(1) && y<menu_height(w)-dpi_units(1);
}
static void theme_control_paint(cairo_t *cr,Window *w) {
    if(!theme_control(w,w->w-dpi_units(40),dpi_units(8))) return;
    int left=(int)lround(w->w/dpi_scale)-82,hover=theme.hover==w->hwnd,pressed=theme.pressed==w->hwnd;
    double background=theme.dark ? (pressed ? .27 : hover ? .24 : .19) : (pressed ? .77 : hover ? .9 : .86);
    cairo_save(cr);cairo_scale(cr,dpi_scale,dpi_scale);cairo_set_source_rgb(cr,background,background,background+(theme.dark ? .025 : 0));cairo_rectangle(cr,left+.5,1.5,77,16);cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr,theme.dark ? .39 : .68,theme.dark ? .43 : .68,theme.dark ? .50 : .68);cairo_set_line_width(cr,1);cairo_stroke(cr);
    cairo_set_source_rgb(cr,theme.dark ? .94 : .16,theme.dark ? .82 : .19,theme.dark ? .49 : .31);
    if(theme.dark) {
        cairo_arc(cr,left+10,9,3,0,6.283185307);cairo_fill(cr);
        for(unsigned i=0;i<8;i++) { double angle=i*0.785398163;cairo_move_to(cr,left+10+4.5*cos(angle),9+4.5*sin(angle));cairo_line_to(cr,left+10+6*cos(angle),9+6*sin(angle)); }cairo_stroke(cr);
    }else {
        cairo_arc(cr,left+10,9,5,0,6.283185307);cairo_fill(cr);cairo_set_source_rgb(cr,background,background,background);cairo_arc(cr,left+12,7,4.5,0,6.283185307);cairo_fill(cr);
    }
    cairo_set_source_rgb(cr,theme.dark ? .91 : .08,theme.dark ? .93 : .08,theme.dark ? .96 : .08);cairo_select_font_face(cr,"Tahoma",0,0);cairo_set_font_size(cr,11);cairo_move_to(cr,left+23,13);cairo_show_text(cr,theme.dark ? "Light" : "Dark");cairo_restore(cr);
}
