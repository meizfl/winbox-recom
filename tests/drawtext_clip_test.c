#include "rt.h"
#include <SDL3/SDL.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"clip check failed at %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static uint64_t api4(CPU *c,const char *name,uint64_t a,uint64_t b,uint64_t d,uint64_t e) {
    c->r[RCX]=a;c->r[RDX]=b;c->r[R8]=d;c->r[R9]=e;
    CHECK(native_import(c,name));CHECK(!c->failed);
    return c->r[RAX];
}
static uint64_t api(CPU *c,const char *name,uint64_t a,uint64_t b) { return api4(c,name,a,b,0,0); }
int main(void) {
    CPU c;CHECK(cpu_init(&c));c.r[RSP]=c.stack_base+c.stack_size-264;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"SDL_VIDEO_DRIVER","dummy",true);
    uint64_t dc=api(&c,"GDI32.dll!CreateCompatibleDC",0,0);
    uint64_t bitmap=api4(&c,"GDI32.dll!CreateCompatibleBitmap",0,300,40,0);
    api(&c,"GDI32.dll!SelectObject",dc,bitmap);
    /* background */
    uint64_t rect=guest_alloc(&c,16);store(&c,rect,32,0);store(&c,rect+4,32,0);store(&c,rect+8,32,300);store(&c,rect+12,32,40);
    uint64_t brush=api(&c,"GDI32.dll!CreateSolidBrush",0xffffff,0);
    CHECK(api4(&c,"USER32.dll!FillRect",dc,rect,brush,0));
    /* long label into a 100px-wide column rect, the way WinBox draws headers */
    const char *label="Very Long Column Header Text";
    uint64_t text=guest_string_alloc(&c,label);
    uint64_t draw=guest_alloc(&c,16);store(&c,draw,32,10);store(&c,draw+4,32,10);store(&c,draw+8,32,110);store(&c,draw+12,32,30);
    /* DT_SINGLELINE=0x20 | DT_END_ELLIPSIS=0x8000 | DT_VCENTER=4 | DT_LEFT=0 */
    uint64_t params=guest_alloc(&c,24);store(&c,params,32,24);
    store(&c,c.r[RSP]+40,64,0x8024);store(&c,c.r[RSP]+48,64,params);
    api4(&c,"USER32.dll!DrawTextExA",dc,text,(uint64_t)strlen(label),draw);
    /* inside the rect: glyph pixels must exist (not pure background) */
    unsigned inside_hits=0,outside_hits=0;
    for(int yy=10;yy<32;yy++) { if(api4(&c,"GDI32.dll!GetPixel",dc,15,yy,0)!=0xffffff) inside_hits++;if(api4(&c,"GDI32.dll!GetPixel",dc,130,yy,0)!=0xffffff) outside_hits++; }
    CHECK(inside_hits>0);
    /* beyond the right edge of the rect (x=130 > 110): nothing may be drawn there */
    CHECK(outside_hits==0);
    /* same without DT_END_ELLIPSIS: hard clip, still nothing past the edge */
    CHECK(api4(&c,"USER32.dll!FillRect",dc,rect,brush,0));
    store(&c,c.r[RSP]+40,64,0x24);store(&c,c.r[RSP]+48,64,params);
    api4(&c,"USER32.dll!DrawTextExA",dc,text,(uint64_t)strlen(label),draw);
    inside_hits=0;outside_hits=0;
    for(int yy=10;yy<32;yy++) { if(api4(&c,"GDI32.dll!GetPixel",dc,15,yy,0)!=0xffffff) inside_hits++;if(api4(&c,"GDI32.dll!GetPixel",dc,130,yy,0)!=0xffffff) outside_hits++; }
    CHECK(inside_hits>0);
    CHECK(outside_hits==0);
    /* DT_NOCLIP=0x100 must still paint past the rect edge */
    CHECK(api4(&c,"USER32.dll!FillRect",dc,rect,brush,0));
    store(&c,c.r[RSP]+40,64,0x124);store(&c,c.r[RSP]+48,64,params);
    api4(&c,"USER32.dll!DrawTextExA",dc,text,(uint64_t)strlen(label),draw);
    outside_hits=0;
    for(int yy=10;yy<32;yy++) if(api4(&c,"GDI32.dll!GetPixel",dc,130,yy,0)!=0xffffff) outside_hits++;
    CHECK(outside_hits>0);
    printf("DrawTextExA clipping: ok\n");
    return 0;
}
