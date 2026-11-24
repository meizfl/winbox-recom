/* stb_image is vendored under its MIT/public-domain license; see vendor/README.md. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 8192
#include <stdint.h>
#include "vendor/stb_image.h"
#include <cairo/cairo.h>
cairo_surface_t *wallpaper_load_image(const char *path) {
    int width,height,channels;
    if(!path || !stbi_info(path,&width,&height,&channels) || width<1 || height<1 || width>8192 || height>8192 || (uint64_t)width*height>33554432) return NULL;
    unsigned char *rgba=stbi_load(path,&width,&height,&channels,4);if(!rgba) return NULL;
    cairo_surface_t *image=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);
    if(cairo_surface_status(image)!=CAIRO_STATUS_SUCCESS) { stbi_image_free(rgba);cairo_surface_destroy(image);return NULL; }
    unsigned char *data=cairo_image_surface_get_data(image);int stride=cairo_image_surface_get_stride(image);
    for(int y=0;y<height;y++) { uint32_t *row=(uint32_t *)(data+y*stride);for(int x=0;x<width;x++) { unsigned char *p=rgba+((size_t)y*width+x)*4;unsigned a=p[3];row[x]=(a<<24)|(((p[0]*a+127)/255)<<16)|(((p[1]*a+127)/255)<<8)|((p[2]*a+127)/255); } }
    stbi_image_free(rgba);cairo_surface_mark_dirty(image);return image;
}
