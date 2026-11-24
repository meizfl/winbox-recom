#include "rt.h"
#include <SDL3/SDL.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"cursor check failed at %d: %s (err=%u)\n",__LINE__,#x,c.last_error);exit(1); } } while(0)
static uint64_t api4(CPU *c,const char *name,uint64_t a,uint64_t b,uint64_t d,uint64_t e) {
    c->r[RCX]=a;c->r[RDX]=b;c->r[R8]=d;c->r[R9]=e;
    if(!native_import(c,name) || c->failed) { fprintf(stderr,"API %s failed\n",name);exit(1); }
    return c->r[RAX];
}
int main(void) {
    CPU c;CHECK(cpu_init(&c));c.r[RSP]=c.stack_base+c.stack_size-264;
    /* The EXE loads its column-drag cursor through
     * LoadImageA(hinst, 101, IMAGE_CURSOR=2, 32, 32, LR_VGACOLOR=0x80). */
    store(&c,c.r[RSP]+40,64,32);store(&c,c.r[RSP]+48,64,0x80);
    uint64_t cursor=api4(&c,"USER32.dll!LoadImageA",c.base,101,2,0);
    CHECK(cursor);
    CHECK(api4(&c,"USER32.dll!SetCursor",cursor,0,0,0));
    /* Standard system cursors must keep working. */
    uint64_t arrow=api4(&c,"USER32.dll!LoadCursorA",0,32512,0,0);CHECK(arrow);
    CHECK(api4(&c,"USER32.dll!SetCursor",arrow,0,0,0));
    /* Unknown resource id must fail cleanly, not trap. */
    c.r[RCX]=c.base;c.r[RDX]=9999;c.r[R8]=2;c.r[R9]=0;store(&c,c.r[RSP]+40,64,32);store(&c,c.r[RSP]+48,64,0x80);
    if(!native_import(&c,"USER32.dll!LoadImageA")) { fprintf(stderr,"dispatch failed\n");return 1; }
    CHECK(!c.failed);
    printf("cursor resource bridge: ok (handle=%llx)\n",(unsigned long long)cursor);
    return 0;
}
