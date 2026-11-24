#ifndef WINBOX_UI_COLORS_H
#define WINBOX_UI_COLORS_H
#include <stdint.h>
static uint32_t theme_pixel(uint32_t pixel) {
    unsigned alpha=pixel>>24;if(!alpha) return pixel;
    int r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
    if(alpha<255) { r=r*255/alpha;g=g*255/alpha;b=b*255/alpha; }
    int y=(54*r+183*g+19*b+128)>>8,target=236-(217*y+127)/255;
    int high=r>g ? r : g;if(b>high) high=b;int low=r<g ? r : g;if(b<low) low=b;
    if(target>=y) { int delta=target-y,denominator=255-y;if(denominator) { r+=(255-r)*delta/denominator;g+=(255-g)*delta/denominator;b+=(255-b)*delta/denominator; } }
    else { r=r*target/y;g=g*target/y;b=b*target/y; }
    if(high-low<24) { r=r>2 ? r-2 : 0;b=b<250 ? b+5 : 255; }
    if(alpha<255) { r=r*alpha/255;g=g*alpha/255;b=b*alpha/255; }
    return (alpha<<24)|(r<<16)|(g<<8)|b;
}
#endif
