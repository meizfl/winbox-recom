/* Wallpaper belongs to the workspace layer, below all MDI children. */
#define WALLPAPER_CHOOSE 0x7fff7101u
#define WALLPAPER_REMOVE 0x7fff7102u
extern cairo_surface_t *wallpaper_load_image(const char *);
static struct { int initialized,width,height;cairo_surface_t *image,*scaled; } wallpaper;
static int wallpaper_command(CPU *,Window *,uint32_t);
static void wallpaper_path(char *path,size_t size) { snprintf(path,size,"%s/wallpaper.png",host_profile_path()); }
static void wallpaper_invalidate(void) {
    if(wallpaper.scaled) cairo_surface_destroy(wallpaper.scaled);wallpaper.scaled=NULL;wallpaper.width=wallpaper.height=0;
    for(unsigned i=1;i<MAX_WINDOWS;i++) if(ui.windows[i].used && ui.windows[i].native) request_present(&ui.windows[i]);
}
static void wallpaper_init(void) {
    if(wallpaper.initialized) return;wallpaper.initialized=1;char path[PATH_MAX];wallpaper_path(path,sizeof path);wallpaper.image=wallpaper_load_image(path);
}
static int wallpaper_set(const char *path) {
    wallpaper_init();cairo_surface_t *image=path ? wallpaper_load_image(path) : NULL;if(path && !image) return 0;
    char destination[PATH_MAX],temporary[PATH_MAX];wallpaper_path(destination,sizeof destination);snprintf(temporary,sizeof temporary,"%s/wallpaper.%ld.tmp",host_profile_path(),(long)getpid());
    if(path) { if(cairo_surface_write_to_png(image,temporary)!=CAIRO_STATUS_SUCCESS || rename(temporary,destination)) { unlink(temporary);cairo_surface_destroy(image);return 0; } }
    else if(unlink(destination) && errno!=ENOENT) return 0;
    if(wallpaper.image) cairo_surface_destroy(wallpaper.image);wallpaper.image=image;wallpaper_invalidate();return 1;
}
static int wallpaper_workspace(Window *w) {
    Window *parent=window(w->parent);return parent && !strcmp(parent->class_name,"routeros_main") && !strcmp(w->class_name,"routeros_dbl_canvas") && (!w->title || !*w->title);
}
static void wallpaper_paint(cairo_t *cr,Window *w,int x,int y) {
    if(!wallpaper_workspace(w)) return;wallpaper_init();if(!wallpaper.image) return;
    if(!wallpaper.scaled || wallpaper.width!=w->w || wallpaper.height!=w->h) {
        wallpaper_invalidate();wallpaper.width=w->w;wallpaper.height=w->h;
        wallpaper.scaled=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w->w,w->h);cairo_t *scaled=cairo_create(wallpaper.scaled);
        double iw=cairo_image_surface_get_width(wallpaper.image),ih=cairo_image_surface_get_height(wallpaper.image),scale=fmax(w->w/iw,w->h/ih);
        cairo_translate(scaled,(w->w-iw*scale)/2,(w->h-ih*scale)/2);cairo_scale(scaled,scale,scale);cairo_set_source_surface(scaled,wallpaper.image,0,0);cairo_pattern_set_filter(cairo_get_source(scaled),CAIRO_FILTER_BILINEAR);cairo_pattern_set_extend(cairo_get_source(scaled),CAIRO_EXTEND_PAD);cairo_paint(scaled);cairo_destroy(scaled);
    }
    cairo_set_source_surface(cr,wallpaper.scaled,x,y);cairo_paint(cr);
}
static void wallpaper_settings(uint64_t handle) {
    int settings=0;
    for(unsigned i=1;i<MAX_WINDOWS && !settings;i++) { Window *w=&ui.windows[i];Gdi *bar=w->used && w->native ? gdi(w->menu) : NULL;if(!bar || bar->type!=5) continue;
        for(size_t j=0;j<bar->menu.count;j++) if(bar->menu.items[j].submenu==handle) { char *label=menu_label(bar->menu.items[j].text);settings=label && !strcmp(label,"Settings");free(label);if(settings) break; }
    }
    if(!settings) return;Gdi *g=gdi(handle);if(!g || g->type!=5) return;wallpaper_init();
    for(size_t i=0;i<g->menu.count;i++) { Gdi *sub=gdi(g->menu.items[i].submenu);if(!sub || sub->type!=5) continue;for(size_t j=0;j<sub->menu.count;j++) if(sub->menu.items[j].id==WALLPAPER_REMOVE) { sub->menu.items[j].state=wallpaper.image ? 0 : 3;return; } }
    uint64_t handle_sub=new_gdi(5);Gdi *sub=gdi(handle_sub);if(!sub) return;
    sub->menu.items=calloc(2,sizeof(MenuItem));if(!sub->menu.items) { memset(sub,0,sizeof *sub);return; }sub->menu.count=2;
    sub->menu.items[0]=(MenuItem){.text=strdup("Select wallpaper..."),.id=WALLPAPER_CHOOSE};
    sub->menu.items[1]=(MenuItem){.text=strdup("Remove wallpaper"),.id=WALLPAPER_REMOVE,.state=wallpaper.image ? 0 : 3};
    MenuItem *items=realloc(g->menu.items,(g->menu.count+2)*sizeof *items);if(!items) { for(size_t i=0;i<sub->menu.count;i++) free(sub->menu.items[i].text);free(sub->menu.items);memset(sub,0,sizeof *sub);return; }g->menu.items=items;
    items[g->menu.count++]=(MenuItem){.type=0x800};
    items[g->menu.count++]=(MenuItem){.text=strdup("Wallpaper"),.submenu=handle_sub};
}
