#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <cairo.h>
#include <cairo-svg.h>
const uint8_t guest_image[1]={0};
const size_t guest_image_size=0;
#include "ui_icons.inc"
#include "ui_colors.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"vector icon check at %d: %s\n",__LINE__,#x);return 1; } } while(0)
static const char *names[128]={
 [0]="empty",[1]="bridge",[2]="folder",[3]="graph",[4]="logout",[5]="interfaces",[6]="ip",[7]="ipv6",[8]="log",[9]="list",[10]="exit",[11]="support-file",[12]="screen",[13]="mesh",[14]="application",[15]="mpls",[16]="terminal",[17]="new-interface",[18]="queues",[19]="quick-set",[20]="radius",[21]="redo",[22]="routing",[23]="wireless-ap",[24]="switch",[25]="system",[26]="tools",[27]="undo",[28]="new-winbox",[29]="wireless",[30]="window",[31]="certificate",[32]="ports",[33]="statistics",[34]="dude",[35]="secure-link",[36]="transfer",[37]="link",[38]="users",[39]="wireguard",[40]="firewall",[41]="wifi-lock",[42]="wifi",[43]="dot1x",[44]="iot",[45]="openflow",[48]="windows",[49]="monitor",[52]="socket",[53]="ethernet",[54]="access-point",[55]="usb",[56]="network-card",[57]="radio",[58]="adapter",[64]="tunnel",[65]="user",[66]="user-disabled",[67]="user-blocked",[68]="network",[69]="grid",[70]="play",[71]="user-group",[72]="light",[73]="bridge-port",[74]="comment",[75]="ethernet-switch",[76]="document",[77]="settings",[78]="package",[79]="new-document",[80]="directory",[81]="add",[82]="bidirectional",[83]="ethernet-interface",[84]="ppp",[85]="enable",[86]="disable",[87]="file",[88]="back",[89]="return",[90]="leave",[91]="enter",[92]="erase",[93]="edit",[94]="edit-enabled",[95]="remove-entry",[96]="add-entry",[97]="layers-active",[98]="layers-warning",[99]="layers-error",[100]="internet",[101]="globe",[102]="ruler",[103]="connections",[104]="warning",[105]="download",[106]="upload",[107]="maintenance",[108]="target",[109]="blocked",[110]="run",[111]="list-disabled",[112]="enable-disabled",[113]="disable-disabled",[114]="file-disabled",[115]="refresh-disabled",[116]="wifi-signal",[117]="power",[118]="download-to-device"
};
static uint32_t pixel(cairo_surface_t *surface,int x,int y) { uint32_t value;cairo_surface_flush(surface);memcpy(&value,cairo_image_surface_get_data(surface)+y*cairo_image_surface_get_stride(surface)+4*x,4);return value; }
int main(int argc,char **argv) {
    unsigned drawn=0;
    for(unsigned kind=1;kind<=1031;kind++) {
        if(kind>128 && kind<1000) continue;
        for(int size=16;size<=32;size+=4) {
            cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,size,size);cairo_t *cr=cairo_create(surface);
            int ok=modern_icon_draw(cr,kind,0,0,size,size);cairo_destroy(cr);
            if(!ok || kind==1) { cairo_surface_destroy(surface);continue; }
            unsigned ink=0;for(int y=0;y<size;y++) for(int x=0;x<size;x++) if(pixel(surface,x,y)>>24) ink++;
            CHECK(ink>=8);CHECK(!(pixel(surface,0,0)>>24));CHECK(!(pixel(surface,size-1,0)>>24));CHECK(!(pixel(surface,0,size-1)>>24));CHECK(!(pixel(surface,size-1,size-1)>>24));
            cairo_surface_destroy(surface);if(size==16) drawn++;
        }
    }
    CHECK(drawn>=125);
    /* cairo_save does not preserve the current path. An icon following text
     * used to connect an arc to that text position and draw diagonal tails. */
    cairo_surface_t *path_surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,96,64);cairo_t *path_cr=cairo_create(path_surface);
    cairo_move_to(path_cr,2,60);cairo_line_to(path_cr,12,60);cairo_path_t *before=cairo_copy_path(path_cr);
    CHECK(modern_icon_draw(path_cr,16,60,5,24,24));cairo_path_t *after=cairo_copy_path(path_cr);
    CHECK(before->num_data==after->num_data);
    for(int i=0;i<before->num_data;i++) CHECK(!memcmp(&before->data[i],&after->data[i],sizeof(cairo_path_data_t)));
    for(int y=31;y<59;y++) for(int x=0;x<96;x++) CHECK(!(pixel(path_surface,x,y)>>24));
    cairo_path_destroy(before);cairo_path_destroy(after);cairo_destroy(path_cr);cairo_surface_destroy(path_surface);

    if(argc==3 && !strcmp(argv[1],"--export")) {
        mkdir(argv[2],0755);
        for(unsigned id=1;id<128;id++) if(names[id]) {
            char path[4096];snprintf(path,sizeof path,"%s/%03u-%s.svg",argv[2],id,names[id]);
            cairo_surface_t *svg=cairo_svg_surface_create(path,16,16);cairo_t *cr=cairo_create(svg);modern_icon_draw(cr,id+1,0,0,16,16);cairo_destroy(cr);cairo_surface_destroy(svg);
        }
        for(int dark=0;dark<2;dark++) {
            cairo_surface_t *sheet=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,960,700);cairo_t *cr=cairo_create(sheet);cairo_set_source_rgb(cr,.9,.9,.9);cairo_paint(cr);
            for(unsigned i=0;i<160;i++) { unsigned kind=i<128 ? i+1 : 1000+i-128;int x=(i%16)*60,y=(i/16)*70;modern_icon_draw(cr,kind,x+14,y+5,32,32);char label[16];snprintf(label,sizeof label,"%u",i<128 ? i : 1000+i-128);cairo_set_source_rgb(cr,.1,.1,.1);cairo_set_font_size(cr,10);cairo_move_to(cr,x+18,y+54);cairo_show_text(cr,label); }
            cairo_destroy(cr);cairo_surface_flush(sheet);
            if(dark) { unsigned char *data=cairo_image_surface_get_data(sheet);int stride=cairo_image_surface_get_stride(sheet);for(int y=0;y<700;y++) for(int x=0;x<960;x++) { uint32_t value;memcpy(&value,data+y*stride+4*x,4);value=theme_pixel(value);memcpy(data+y*stride+4*x,&value,4); }cairo_surface_mark_dirty(sheet); }
            char path[4096];snprintf(path,sizeof path,"%s/gallery-%s.png",argv[2],dark ? "dark" : "light");cairo_surface_write_to_png(sheet,path);cairo_surface_destroy(sheet);
        }
    }
    printf("Vector icons: %u glyphs checked at 16, 20, 24, 28 and 32 px; transparent corners preserved\n",drawn);return 0;
}
