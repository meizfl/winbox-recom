/* Cache presentation pixels only. Hashing the backing store also catches writes
 * through persistent DCs, DIB pointers and software ROPs without changing GDI. */
typedef struct { uint64_t hash;unsigned context;cairo_surface_t *image; } ThemeCache;
static cairo_user_data_key_t theme_cache_key;
static int theme_cache_enabled=1;
static uint64_t theme_cache_builds;
static void theme_cache_destroy(void *opaque) { ThemeCache *cache=opaque;if(cache->image) cairo_surface_destroy(cache->image);free(cache); }
static uint64_t theme_surface_hash(cairo_surface_t *image) {
    cairo_surface_flush(image);const unsigned char *p=cairo_image_surface_get_data(image);size_t bytes=(size_t)cairo_image_surface_get_stride(image)*cairo_image_surface_get_height(image);
    uint64_t a=0x9e3779b185ebca87ULL,b=0xc2b2ae3d27d4eb4fULL,c=0x165667b19e3779f9ULL,d=0x85ebca77c2b2ae63ULL;
    while(bytes>=32) { uint64_t v[4];memcpy(v,p,32);a=(a^v[0])*0x9e3779b185ebca87ULL;b=(b^v[1])*0xc2b2ae3d27d4eb4fULL;c=(c^v[2])*0x165667b19e3779f9ULL;d=(d^v[3])*0x85ebca77c2b2ae63ULL;p+=32;bytes-=32; }
    a^=b^(c<<13)^(c>>51)^(d<<37)^(d>>27);while(bytes--) a=(a^*p++)*1099511628211ULL;return a;
}
static cairo_surface_t *theme_cached_layer(Window *w) {
    unsigned context=w->title && *w->title && !strcmp(w->class_name,"routeros_dbl_canvas");
    for(Window *p=w;p;p=window(p->parent)) if(p->title && !strncmp(p->title,"Terminal",8)) { context|=2;break; }
    uint64_t hash=theme_surface_hash(w->surface);ThemeCache *cache=cairo_surface_get_user_data(w->surface,&theme_cache_key);
    if(!cache) { cache=calloc(1,sizeof *cache);if(!cache) return NULL;if(cairo_surface_set_user_data(w->surface,&theme_cache_key,cache,theme_cache_destroy)!=CAIRO_STATUS_SUCCESS) { free(cache);return NULL; } }
    if(!cache->image || cache->hash!=hash || cache->context!=context) {
        cairo_surface_t *image=theme_copy_region(w->surface,0,0,w->w,w->h);if(!image || cairo_surface_status(image)!=CAIRO_STATUS_SUCCESS) { if(image) cairo_surface_destroy(image);return NULL; }
        theme_apply(image,w);if(cache->image) cairo_surface_destroy(cache->image);cache->image=image;cache->hash=hash;cache->context=context;theme_cache_builds++;
    }
    return cache->image;
}
