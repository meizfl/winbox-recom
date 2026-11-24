#include <SDL3/SDL.h>
#include <stdio.h>
/* Unlike a Cairo PNG, this checks the window system's actual presentation path.
 * Run manually with SDL_VIDEO_DRIVER=wayland to exercise wl_shm buffers. */
int main(void) {
    if(!SDL_Init(SDL_INIT_VIDEO)) goto fail;
    SDL_Window *window=SDL_CreateWindow("WinBox surface regression",320,200,SDL_WINDOW_RESIZABLE);
    if(!window) goto fail;
    for(int frame=0;frame<40;frame++) {
        SDL_PumpEvents();
        if(frame==10) SDL_SetWindowSize(window,480,300);
        if(frame==25) SDL_SetWindowSize(window,360,240);
        SDL_Surface *surface=SDL_GetWindowSurface(window);
        if(!surface || !SDL_FillSurfaceRect(surface,NULL,0xff804020) || !SDL_UpdateWindowSurface(window)) goto fail;
        SDL_Delay(20);
    }
    printf("Native window presentation: %s, 40 frames and two resizes\n",SDL_GetCurrentVideoDriver());
    SDL_DestroyWindow(window);SDL_Quit();return 0;
fail:
    fprintf(stderr,"Native window presentation failed: %s\n",SDL_GetError());SDL_Quit();return 1;
}
