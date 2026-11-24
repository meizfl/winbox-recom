/* WinBox Recom addition to SDL: software Wayland window surfaces via wl_shm.
 * Independent staging memory and three released-buffer slots prevent writing
 * into a buffer while the compositor reads it. No EGL/GPU driver is required. */
#include "SDL_internal.h"
#ifdef SDL_VIDEO_DRIVER_WAYLAND
#include "SDL_waylandvideo.h"
#include "SDL_waylandwindow.h"
#include "SDL_waylandshmbuffer.h"

#define WB_FRAMEBUFFER "winbox.wayland.shm.framebuffer"
typedef struct {
    Wayland_SHMPool *pool;
    struct wl_buffer *buffer;
    void *pixels;
    bool busy;
} WB_Buffer;
typedef struct {
    int width, height, pitch;
    void *pixels;
    WB_Buffer slots[3];
} WB_Framebuffer;

static void WB_Released(void *userdata, struct wl_buffer *buffer)
{
    WB_Buffer *slot = userdata;
    slot->busy = false;
}
static const struct wl_buffer_listener WB_BufferListener = { WB_Released };

static void WB_DestroyFramebuffer(SDL_VideoDevice *device, SDL_Window *window)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    WB_Framebuffer *fb = SDL_GetPointerProperty(props, WB_FRAMEBUFFER, NULL);
    if (!fb) return;
    SDL_ClearProperty(props, WB_FRAMEBUFFER);
    for (int i = 0; i < 3; i++) {
        if (fb->slots[i].buffer) wl_buffer_destroy(fb->slots[i].buffer);
        Wayland_ReleaseSHMPool(fb->slots[i].pool);
    }
    SDL_free(fb->pixels);
    SDL_free(fb);
}

static bool WB_CreateFramebuffer(SDL_VideoDevice *device, SDL_Window *window,
                                 SDL_PixelFormat *format, void **pixels, int *pitch)
{
    WB_DestroyFramebuffer(device, window);
    WB_Framebuffer *fb = SDL_calloc(1, sizeof(*fb));
    if (!fb) return false;
    SDL_GetWindowSizeInPixels(window, &fb->width, &fb->height);
    if (fb->width <= 0 || fb->height <= 0 || fb->width > 16384 || fb->height > 16384) {
        SDL_free(fb);
        return SDL_SetError("Invalid Wayland SHM framebuffer size");
    }
    fb->pitch = fb->width * 4;
    size_t size = (size_t)fb->pitch * fb->height;
    fb->pixels = SDL_calloc(1, size);
    SDL_SetPointerProperty(SDL_GetWindowProperties(window), WB_FRAMEBUFFER, fb);
    if (!fb->pixels) goto fail;
    for (int i = 0; i < 3; i++) {
        WB_Buffer *slot = &fb->slots[i];
        slot->pool = Wayland_AllocSHMPool((int)size);
        if (!slot->pool) goto fail;
        slot->buffer = Wayland_AllocBufferFromPoolCallback(slot->pool, fb->width,
                           fb->height, &slot->pixels, &WB_BufferListener, slot);
        if (!slot->buffer) goto fail;
    }
    *format = SDL_PIXELFORMAT_ARGB8888;
    *pixels = fb->pixels;
    *pitch = fb->pitch;
    return true;
fail:
    WB_DestroyFramebuffer(device, window);
    return false;
}

static bool WB_UpdateFramebuffer(SDL_VideoDevice *device, SDL_Window *window,
                                 const SDL_Rect *rects, int numrects)
{
    WB_Framebuffer *fb = SDL_GetPointerProperty(SDL_GetWindowProperties(window), WB_FRAMEBUFFER, NULL);
    SDL_WindowData *data = window->internal;
    if (!fb) return SDL_SetError("Missing Wayland SHM framebuffer");
    if (data->shell_surface_status != WAYLAND_SHELL_SURFACE_STATUS_SHOWN &&
        data->shell_surface_status != WAYLAND_SHELL_SURFACE_STATUS_WAITING_FOR_FRAME)
        return true;
    WB_Buffer *slot = NULL;
    for (int attempt = 0; attempt < 2 && !slot; attempt++) {
        for (int i = 0; i < 3; i++) if (!fb->slots[i].busy) { slot = &fb->slots[i]; break; }
        if (!slot && attempt == 0 && WAYLAND_wl_display_roundtrip(data->waylandData->display) < 0)
            return SDL_SetError("Wayland display disconnected");
    }
    if (!slot) return SDL_SetError("Wayland SHM buffers are still in use");
    SDL_memcpy(slot->pixels, fb->pixels, (size_t)fb->pitch * fb->height);
    slot->busy = true;
    wl_surface_attach(data->surface, slot->buffer, 0, 0);
    wl_surface_damage(data->surface, 0, 0, SDL_MAX_SINT32, SDL_MAX_SINT32);
    wl_surface_commit(data->surface);
    WAYLAND_wl_display_flush(data->waylandData->display);
    return true;
}

void WB_WaylandFramebufferInit(SDL_VideoDevice *device)
{
    device->CreateWindowFramebuffer = WB_CreateFramebuffer;
    device->UpdateWindowFramebuffer = WB_UpdateFramebuffer;
    device->DestroyWindowFramebuffer = WB_DestroyFramebuffer;
    device->checked_texture_framebuffer = true;
}
#endif
