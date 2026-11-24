/* Win32/GDI use framebuffer pixels. SDL uses native desktop/window points.
 * Convert only at that boundary; never upscale the completed GDI frame. */
static double dpi_density=1.0,dpi_scale=1.0;
static double native_density(SDL_Window *native) { double value=native ? SDL_GetWindowPixelDensity(native) : dpi_density;return value>0 && value<=8 ? value : 1; }
static int native_points(SDL_Window *native,double pixels) { return (int)lround(pixels/native_density(native)); }
static int guest_pixels(SDL_Window *native,double points) { return (int)lround(points*native_density(native)); }
static int dpi_units(double value) { return (int)lround(value*dpi_scale); }
static void dpi_initialize(void) {
    SDL_Window *probe=SDL_CreateWindow("WinBox DPI",64,64,SDL_WINDOW_HIDDEN|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(probe) { dpi_density=native_density(probe);dpi_scale=SDL_GetWindowDisplayScale(probe);SDL_DestroyWindow(probe); }
    if(!(dpi_scale>=.5 && dpi_scale<=8)) dpi_scale=1;
    fprintf(stderr,"WinBox HiDPI: content %.3g, pixel density %.3g, DPI %d\n",dpi_scale,dpi_density,dpi_units(96));
}
static SDL_Window *create_pixel_popup(SDL_Window *parent,int x,int y,int width,int height,SDL_WindowFlags flags) {
    return SDL_CreatePopupWindow(parent,native_points(parent,x),native_points(parent,y),native_points(parent,width),native_points(parent,height),flags|SDL_WINDOW_HIGH_PIXEL_DENSITY);
}
static void set_pixel_size(SDL_Window *native,int width,int height) { SDL_SetWindowSize(native,native_points(native,width),native_points(native,height)); }
static void get_pixel_position(SDL_Window *native,int *x,int *y) { SDL_GetWindowPosition(native,x,y);*x=guest_pixels(native,*x);*y=guest_pixels(native,*y); }
