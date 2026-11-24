#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
/* Unlike a Cairo PNG, this checks the window system's actual presentation path.
 * Run manually with SDL_VIDEO_DRIVER=wayland to exercise wl_shm buffers. */
int main(void) {
    if(!SDL_Init(SDL_INIT_VIDEO)) goto fail;
    SDL_Window *window=SDL_CreateWindow("WinBox surface regression",320,200,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(!window) goto fail;
    for(int frame=0;frame<40;frame++) {
        SDL_PumpEvents();
        if(frame==10) SDL_SetWindowSize(window,480,300);
        if(frame==25) SDL_SetWindowSize(window,360,240);
        SDL_Surface *surface=SDL_GetWindowSurface(window);
        if(!surface || !SDL_FillSurfaceRect(surface,NULL,0xff804020) || !SDL_UpdateWindowSurface(window)) goto fail;
        SDL_Delay(20);
    }
    const char *expected=getenv("WINBOX_EXPECT_DENSITY");int logical_w,logical_h,pixels_w,pixels_h;SDL_GetWindowSize(window,&logical_w,&logical_h);SDL_GetWindowSizeInPixels(window,&pixels_w,&pixels_h);
    if(expected && (fabs(SDL_GetWindowPixelDensity(window)-atof(expected))>.01 || pixels_w!=lround(logical_w*atof(expected)) || pixels_h!=lround(logical_h*atof(expected)))) { SDL_SetError("HiDPI framebuffer does not match requested density");goto fail; }
    printf("Native density: %.3g, points %dx%d, pixels %dx%d\n",SDL_GetWindowPixelDensity(window),logical_w,logical_h,pixels_w,pixels_h);
    printf("Native window presentation: %s, 40 frames and two resizes\n",SDL_GetCurrentVideoDriver());
    SDL_DestroyWindow(window);SDL_Quit();return 0;
fail:
    fprintf(stderr,"Native window presentation failed: %s\n",SDL_GetError());SDL_Quit();return 1;
}
