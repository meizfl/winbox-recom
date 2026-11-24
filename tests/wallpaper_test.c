#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cairo/cairo.h>
extern int ui_wallpaper_composition_test(const char *);
int main(void) {
    char directory[]="/tmp/winbox-wallpaper-XXXXXX";if(!mkdtemp(directory)) return 1;setenv("WINBOX_PROFILE",directory,1);
    char path[1024];snprintf(path,sizeof path,"%s/source.png",directory);
    cairo_surface_t *image=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,50,40);cairo_t *cr=cairo_create(image);cairo_set_source_rgb(cr,44/255.,88/255.,166/255.);cairo_paint(cr);cairo_destroy(cr);cairo_surface_write_to_png(image,path);cairo_surface_destroy(image);
    int ok=ui_wallpaper_composition_test(path);unlink(path);rmdir(directory);
    if(!ok) { fputs("Wallpaper composition failed\n",stderr);return 1; }
    puts("Wallpaper: workspace isolation, MDI overlap, theme colors, persistence, invalid image and removal passed");return 0;
}
