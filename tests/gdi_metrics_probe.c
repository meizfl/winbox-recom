/* Build normally against the lifted runtime, or with winegcc -DWINE_REFERENCE
 * to query real Windows GDI on the isolated reference display. */
#include <stdio.h>
#include <string.h>
#ifdef WINE_REFERENCE
#include <windows.h>
#else
#include "rt.h"
static CPU cpu;
static uint64_t call(const char *name,uint64_t a,uint64_t b,uint64_t d,uint64_t e) { cpu.r[RCX]=a;cpu.r[RDX]=b;cpu.r[R8]=d;cpu.r[R9]=e;if(!native_import(&cpu,name) || cpu.failed) { fprintf(stderr,"GDI probe failed: %s\n",name);return 0; }return cpu.r[RAX]; }
#endif
int main(void) {
    const char *faces[]={"Tahoma","Courier New","MS Shell Dlg"};int sizes[]={-11,-12,-11};
#ifdef WINE_REFERENCE
    HDC dc=GetDC(NULL);
#else
    if(!cpu_init(&cpu)) return 1;cpu.r[RSP]=cpu.stack_base+cpu.stack_size-512;uint64_t dc=call("USER32.dll!GetDC",0,0,0,0),metrics=guest_alloc(&cpu,56),extent=guest_alloc(&cpu,8);
#endif
    puts("[");
    for(unsigned i=0;i<3;i++) {
#ifdef WINE_REFERENCE
        HFONT font=CreateFontA(sizes[i],0,0,0,400,0,0,0,DEFAULT_CHARSET,0,0,0,0,faces[i]);SelectObject(dc,font);TEXTMETRICA m;GetTextMetricsA(dc,&m);SIZE s;GetTextExtentPoint32A(dc,"Safe Mode",9,&s);int values[]={m.tmHeight,m.tmAscent,m.tmDescent,m.tmInternalLeading,m.tmExternalLeading,m.tmAveCharWidth,m.tmMaxCharWidth};int width=s.cx;
#else
        for(unsigned n=4;n<14;n++) store(&cpu,cpu.r[RSP]+40+8*(n-4),64,n==4 ? 400 : 0);store(&cpu,cpu.r[RSP]+112,64,guest_string_alloc(&cpu,faces[i]));uint64_t font=call("GDI32.dll!CreateFontA",(uint64_t)sizes[i],0,0,0);call("GDI32.dll!SelectObject",dc,font,0,0);call("GDI32.dll!GetTextMetricsA",dc,metrics,0,0);int values[7];for(unsigned n=0;n<7;n++) values[n]=load(&cpu,metrics+4*n,32);store(&cpu,cpu.r[RSP]+40,64,0);store(&cpu,cpu.r[RSP]+48,64,0);store(&cpu,cpu.r[RSP]+56,64,extent);call("GDI32.dll!GetTextExtentExPointA",dc,guest_string_alloc(&cpu,"Safe Mode"),9,1000);int width=load(&cpu,extent,32);
#endif
        printf("  {\"face\":\"%s\",\"size\":%d,\"height\":%d,\"ascent\":%d,\"descent\":%d,\"internal_leading\":%d,\"external_leading\":%d,\"average\":%d,\"maximum\":%d,\"text_width\":%d}%s\n",faces[i],sizes[i],values[0],values[1],values[2],values[3],values[4],values[5],values[6],width,i==2 ? "" : ",");
    }
    puts("]");return 0;
}
