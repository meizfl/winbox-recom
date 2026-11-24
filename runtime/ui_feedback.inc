/* Stable frame presentation, Win32 carets, and native pointer shapes. */
static Window *native_root(Window *w) {
    for(unsigned i=0;w && !w->native && i<MAX_WINDOWS;i++) w=window(w->parent);
    return w;
}

static int frame_unready(Window *root) {
    for(unsigned i=1;i<MAX_WINDOWS;i++) {
        Window *w=&ui.windows[i];if(!w->used || !w->visible) continue;
        Window *p=w;int visible=1;
        for(unsigned depth=0;p && !p->native && depth<MAX_WINDOWS;depth++) { if(!p->visible) visible=0;p=window(p->parent); }
        if(!visible || p!=root) continue;
        if(w->painting || ((!root->frame || root->resize_pending) && w->paint_pending)) return 1;
    }
    return 0;
}

static int upload_frame(Window *w,cairo_surface_t *frame) {
    int width=cairo_image_surface_get_width(frame),height=cairo_image_surface_get_height(frame);
    SDL_Surface *source=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_ARGB8888,cairo_image_surface_get_data(frame),cairo_image_surface_get_stride(frame));
    SDL_Surface *destination=SDL_GetWindowSurface(w->native);
    int ok=0;
    if(source && destination) {
        /* Keep the completed image at its device-pixel size while a larger
         * native buffer is waiting for WM_SIZE/WM_PAINT. Do not stretch it. */
        uint32_t background=0xffe2e2e2;
        if(width>2 && height>2) memcpy(&background,cairo_image_surface_get_data(frame)+(height-2)*cairo_image_surface_get_stride(frame)+4*(width-2),4);
        SDL_FillSurfaceRect(destination,NULL,background);
        /* Both surfaces are device pixels. Scaling here would blur text on
         * Retina/fractional Wayland and would hide a DPI/layout mismatch. */
        if(getenv("WINBOX_TEST_PIXEL_GEOMETRY")) { int points_w,points_h;SDL_GetWindowSize(w->native,&points_w,&points_h);fprintf(stderr,"WinBox pixel geometry: points=%dx%d buffer=%dx%d frame=%dx%d\n",points_w,points_h,destination->w,destination->h,width,height); }
        ok=SDL_BlitSurface(source,NULL,destination,NULL) && SDL_UpdateWindowSurface(w->native);
    }
    static int reported;
    if(!ok && !reported) { fprintf(stderr,"WinBox presentation failed (%s): %s\n",SDL_GetCurrentVideoDriver(),SDL_GetError());reported=1; }
    if(ok) reported=0;
    if(source) SDL_DestroySurface(source);
    return ok;
}

static void caret_changed(void) {
    ui.caret.phase=1;ui.caret.next=SDL_GetTicks()+(ui.caret_blink_ms ? ui.caret_blink_ms : 530);
    request_present(window(ui.caret.owner));
}
static void caret_destroy(void) {
    request_present(window(ui.caret.owner));if(ui.caret.bitmap) cairo_surface_destroy(ui.caret.bitmap);memset(&ui.caret,0,sizeof ui.caret);
}
static void caret_tick(uint64_t now) {
    if(!ui.caret.owner || ui.caret.visibility<0 || ui.caret_blink_ms==UINT32_MAX || getenv("WINBOX_TEST_CARET_SOLID")) return;
    if(now>=ui.caret.next) { ui.caret.phase=!ui.caret.phase;ui.caret.next=now+(ui.caret_blink_ms ? ui.caret_blink_ms : 530);request_present(window(ui.caret.owner)); }
}
static void paint_caret(cairo_t *cr,Window *root) {
    Window *owner=window(ui.caret.owner);
    if(!owner || ui.caret.visibility<0 || (!ui.caret.phase && !getenv("WINBOX_TEST_CARET_SOLID")) || native_root(owner)!=root) return;
    for(Window *w=owner;w;w=window(w->parent)) { if(!w->visible) return;if(w->native) break; }
    int ox,oy,rx,ry;origin(owner,&ox,&oy);origin(root,&rx,&ry);int x=ox-rx+ui.caret.x,y=oy-ry+menu_height(root)+ui.caret.y;
    cairo_save(cr);cairo_rectangle(cr,ox-rx,oy-ry+menu_height(root),owner->w,owner->h-menu_height(owner));cairo_clip(cr);cairo_set_operator(cr,CAIRO_OPERATOR_DIFFERENCE);cairo_set_source_rgb(cr,1,1,1);
    if(ui.caret.bitmap || ui.caret.gray) {
        if(ui.caret.bitmap) cairo_surface_flush(ui.caret.bitmap);
        for(int row=0;row<ui.caret.h;row++) for(int col=0;col<ui.caret.w;col++) {
            int draw=((row+col)&1)==0;
            if(ui.caret.bitmap) { uint32_t pixel;memcpy(&pixel,cairo_image_surface_get_data(ui.caret.bitmap)+row*cairo_image_surface_get_stride(ui.caret.bitmap)+4*col,4);draw=!(pixel&0xffffff); }
            if(draw) cairo_rectangle(cr,x+col,y+row,1,1);
        }
    }else cairo_rectangle(cr,x,y,ui.caret.w,ui.caret.h);
    cairo_fill(cr);cairo_restore(cr);
    if(getenv("WINBOX_TEST_CARET_SOLID")) fprintf(stderr,"WinBox smoke caret drawn %d %d %d %d\n",x,y,ui.caret.w,ui.caret.h);
}

static uint64_t system_cursor(CPU *c,unsigned id) {
    static uint64_t cursors[12];int index;
    switch(id) { case 32512:index=0;break;case 32513:index=1;break;case 32514:index=2;break;case 32515:index=3;break;case 32650:index=4;break;case 32642:index=5;break;case 32643:index=6;break;case 32644:index=7;break;case 32645:index=8;break;case 32646:index=9;break;case 32648:index=10;break;case 32649:index=11;break;default:c->last_error=1814;return 0; }
    if(!cursors[index]) {
        if(!video(c)) return 0;uint64_t handle=new_gdi(7);Gdi *g=gdi(handle);if(!g) return 0;
        SDL_SystemCursor shapes[]={SDL_SYSTEM_CURSOR_DEFAULT,SDL_SYSTEM_CURSOR_TEXT,SDL_SYSTEM_CURSOR_WAIT,SDL_SYSTEM_CURSOR_CROSSHAIR,SDL_SYSTEM_CURSOR_PROGRESS,SDL_SYSTEM_CURSOR_NWSE_RESIZE,SDL_SYSTEM_CURSOR_NESW_RESIZE,SDL_SYSTEM_CURSOR_EW_RESIZE,SDL_SYSTEM_CURSOR_NS_RESIZE,SDL_SYSTEM_CURSOR_MOVE,SDL_SYSTEM_CURSOR_NOT_ALLOWED,SDL_SYSTEM_CURSOR_POINTER};
        g->cursor=SDL_CreateSystemCursor(shapes[index]);g->cursor_shared=1;cursors[index]=handle;
    }
    return cursors[index];
}
static uint64_t current_cursor(CPU *c) {
    if(!ui.cursor_initialized) { ui.cursor=system_cursor(c,32512);ui.cursor_initialized=1; }
    return ui.cursor;
}
static uint64_t change_cursor(CPU *c,uint64_t handle) {
    uint64_t old=current_cursor(c);Gdi *g=gdi(handle);
    if(handle && (!g || g->type!=7)) { c->last_error=6;return old; }
    ui.cursor=handle;
    if(g && g->cursor) SDL_SetCursor(g->cursor);
    if(!handle || ui.cursor_show_count<0) SDL_HideCursor();else SDL_ShowCursor();
    return old;
}
