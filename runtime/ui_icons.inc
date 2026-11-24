/* Original vector pictograms for the fixed WinBox resource IDs. Coordinates
 * live on a 16-unit grid and are rendered at the actual destination scale. */
#include "icon_resources.h"
typedef struct { const unsigned char *data;size_t size,offset; } IconPNG;
static cairo_status_t icon_png_read(void *opaque,unsigned char *out,unsigned length) {
    IconPNG *png=opaque;if(length>png->size-png->offset) return CAIRO_STATUS_READ_ERROR;
    memcpy(out,png->data+png->offset,length);png->offset+=length;return CAIRO_STATUS_SUCCESS;
}
static cairo_surface_t *icon_atlas(unsigned size) {
    static cairo_surface_t *atlases[3];static int loaded;extern const uint8_t guest_image[];extern const size_t guest_image_size;
    if(!loaded) {
        loaded=1;
        for(size_t i=0;i+32<guest_image_size;i++) if(!memcmp(guest_image+i,"\x89PNG\r\n\x1a\n",8)) {
            const unsigned char *p=guest_image+i;unsigned width=((uint32_t)p[16]<<24)|((uint32_t)p[17]<<16)|((uint32_t)p[18]<<8)|p[19],height=((uint32_t)p[20]<<24)|((uint32_t)p[21]<<16)|((uint32_t)p[22]<<8)|p[23];
            unsigned cell=height/4,index=cell==16 ? 0 : cell==24 ? 1 : cell==32 ? 2 : 3;
            if(index<3 && (width==32*cell || width==32*cell+1) && height==4*cell && !atlases[index]) {
                IconPNG input={p,guest_image_size-i,0};cairo_surface_t *image=cairo_image_surface_create_from_png_stream(icon_png_read,&input);
                if(cairo_surface_status(image)==CAIRO_STATUS_SUCCESS) atlases[index]=image;else cairo_surface_destroy(image);
            }
        }
    }
    return size==16 ? atlases[0] : size==24 ? atlases[1] : size==32 ? atlases[2] : NULL;
}
static unsigned modern_icon_identify(cairo_surface_t *image) {
    int size=cairo_image_surface_get_width(image);if(size!=cairo_image_surface_get_height(image) || size<16 || size>32) return 0;
    cairo_surface_flush(image);unsigned char *pixels=cairo_image_surface_get_data(image);int stride=cairo_image_surface_get_stride(image);uint64_t hash=UINT64_C(14695981039346656037);int gray=1;
    for(int y=0;y<size;y++) for(int x=0;x<size;x++) {
        uint32_t p;memcpy(&p,pixels+y*stride+4*x,4);if(!(p>>24)) p=0;
        unsigned values[]={(p>>16)&255,(p>>8)&255,p&255,p>>24};
        if(abs((int)values[0]-(int)values[1])>2 || abs((int)values[1]-(int)values[2])>2) gray=0;
        for(unsigned i=0;i<4;i++) hash=(hash^values[i])*UINT64_C(1099511628211);
    }
    for(unsigned i=0;i<sizeof control_icon_fingerprints/sizeof *control_icon_fingerprints;i++) if(control_icon_fingerprints[i].size==(unsigned)size && control_icon_fingerprints[i].hash==hash) return control_icon_fingerprints[i].kind;
    cairo_surface_t *atlas=icon_atlas(size);if(!atlas) return 0;cairo_surface_flush(atlas);unsigned char *reference=cairo_image_surface_get_data(atlas);int rs=cairo_image_surface_get_stride(atlas);unsigned disabled=0;
    for(unsigned icon=0;icon<128;icon++) {
        int match=1,shape=1,visible=0;
        for(int y=0;y<size && shape;y++) for(int x=0;x<size;x++) {
            uint32_t a,b;memcpy(&a,pixels+y*stride+4*x,4);memcpy(&b,reference+((icon/32)*size+y)*rs+((icon%32)*size+x)*4,4);
            if(!!(a>>24)!=!!(b>>24)) { shape=match=0;break; }
            if(b>>24) visible=1;
            if((b>>24)==255 && ((a^b)&0xffffff)) match=0;
        }
        if(shape && match && visible) return icon+1;
        if(shape && gray && visible && !disabled) disabled=(icon+1)|256;
    }
    return disabled;
}
static void icon_rgb(cairo_t *cr,unsigned rgb) { cairo_set_source_rgb(cr,((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.); }
static void icon_line(cairo_t *cr,double x,double y,double a,double b) { cairo_move_to(cr,x,y);cairo_line_to(cr,a,b);cairo_stroke(cr); }
static void icon_box(cairo_t *cr,double x,double y,double w,double h) { cairo_rectangle(cr,x,y,w,h);cairo_stroke(cr); }
static void icon_dot(cairo_t *cr,double x,double y,double r) { cairo_arc(cr,x,y,r,0,6.283185307);cairo_fill(cr); }
static void icon_arrow(cairo_t *cr,double x,double y,double dx,double dy) {
    icon_line(cr,x,y,x+dx,y+dy);double length=hypot(dx,dy),ux=dx/length,uy=dy/length;
    cairo_move_to(cr,x+dx-ux*2.5-uy*2,y+dy-uy*2.5+ux*2);cairo_line_to(cr,x+dx,y+dy);cairo_line_to(cr,x+dx-ux*2.5+uy*2,y+dy-uy*2.5-ux*2);cairo_stroke(cr);
}
static void icon_check(cairo_t *cr) { cairo_move_to(cr,3,8);cairo_line_to(cr,6.5,11.5);cairo_line_to(cr,13,4.5);cairo_stroke(cr); }
static void icon_cross(cairo_t *cr) { icon_line(cr,4,4,12,12);icon_line(cr,12,4,4,12); }
static void icon_person(cairo_t *cr,double x,double y) { cairo_arc(cr,x,y,2,0,6.283185307);cairo_stroke(cr);cairo_arc(cr,x,y+7,4,3.141592654,6.283185307);cairo_stroke(cr); }
static void icon_gear(cairo_t *cr) {
    for(unsigned i=0;i<32;i++) { double angle=i*0.196349541,r=(i%4==0 || i%4==3) ? 6 : 4.8;if(!i) cairo_move_to(cr,8+r*cos(angle),8+r*sin(angle));else cairo_line_to(cr,8+r*cos(angle),8+r*sin(angle)); }cairo_close_path(cr);cairo_stroke(cr);cairo_arc(cr,8,8,2,0,6.283185307);cairo_stroke(cr);
}
static void icon_page(cairo_t *cr) { cairo_move_to(cr,4,2);cairo_line_to(cr,9,2);cairo_line_to(cr,12,5);cairo_line_to(cr,12,14);cairo_line_to(cr,4,14);cairo_close_path(cr);cairo_stroke(cr);cairo_move_to(cr,9,2);cairo_line_to(cr,9,5);cairo_line_to(cr,12,5);cairo_stroke(cr); }
static void icon_monitor(cairo_t *cr) { icon_box(cr,2,3,12,8);icon_line(cr,8,11,8,14);icon_line(cr,5,14,11,14); }
static void icon_wifi(cairo_t *cr) {
    cairo_arc(cr,8,13,10,3.926990817,5.497787144);cairo_stroke(cr);cairo_arc(cr,8,13,6.5,3.926990817,5.497787144);cairo_stroke(cr);cairo_arc(cr,8,13,3,3.926990817,5.497787144);cairo_stroke(cr);icon_dot(cr,8,13,1);
}
static void icon_switch(cairo_t *cr) { icon_box(cr,2,4,12,8);for(int i=0;i<3;i++) icon_box(cr,4+i*3,6,1,2);icon_line(cr,4,10,12,10); }
static void icon_link(cairo_t *cr) { cairo_save(cr);cairo_translate(cr,8,8);cairo_rotate(cr,-.785398163);cairo_arc(cr,0,-3,3,3.141592654,6.283185307);cairo_line_to(cr,3,1);cairo_stroke(cr);cairo_arc(cr,0,3,3,0,3.141592654);cairo_line_to(cr,-3,-1);cairo_stroke(cr);icon_line(cr,0,-3,0,3);cairo_restore(cr); }
static int icon_finish(cairo_t *cr,cairo_path_t *path,int result) {
    cairo_restore(cr);cairo_new_path(cr);if(path) { cairo_append_path(cr,path);cairo_path_destroy(path); }return result;
}
static int modern_icon_draw(cairo_t *cr,unsigned kind,double x,double y,double width,double height) {
    if(!kind) return 0;
    cairo_path_t *caller_path=cairo_copy_path(cr);cairo_new_path(cr);
    cairo_save(cr);cairo_translate(cr,x,y);cairo_scale(cr,width/16.,height/16.);cairo_set_operator(cr,CAIRO_OPERATOR_OVER);cairo_set_line_width(cr,1.4);cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
    unsigned id=kind>=1000 ? kind-1000 : (kind&255)-1;int disabled=kind<1000 && (kind&256);unsigned ink=0x3e566b,blue=0x326fa5,green=0x348456,red=0xbb4653,purple=0x795795,orange=0xaa772d;
    if(kind>=1000) {
        if(id==5 || id==9 || id==10 || id==12 || id==14 || id==16 || id==21 || (id>=24 && id<=27)) ink=0x949ba3;
        icon_rgb(cr,ink);
        switch(id) {
        case 0:case 25:case 27: cairo_move_to(cr,4,6);cairo_line_to(cr,8,10);cairo_line_to(cr,12,6);cairo_stroke(cr);break;
        case 1:case 3:case 4: icon_box(cr,2.5,2.5,11,11);break;
        case 2:case 5:case 14:case 15: if(id==15) icon_rgb(cr,green);icon_check(cr);break;
        case 6:case 8: cairo_arc(cr,8,8,5.5,0,6.283185307);cairo_stroke(cr);break;
        case 7:case 9: icon_dot(cr,8,8,2);break;
        case 10:case 11:case 28: if(id==11) icon_rgb(cr,blue);icon_line(cr,3,8,13,8);icon_line(cr,8,3,8,13);break;
        case 12:case 13:case 29: if(id==13) icon_rgb(cr,red);icon_line(cr,3,8,13,8);break;
        case 16:case 17: if(id==17) icon_rgb(cr,red);icon_cross(cr);break;
        case 18:case 19: icon_rgb(cr,id==19 ? orange : ink);icon_box(cr,2.5,3,11,8);cairo_move_to(cr,5,11);cairo_line_to(cr,5,14);cairo_line_to(cr,8,11);cairo_stroke(cr);icon_line(cr,5,6,11,6);icon_line(cr,5,8.5,9,8.5);break;
        case 20: cairo_move_to(cr,2,3);cairo_line_to(cr,14,3);cairo_line_to(cr,9.5,8);cairo_line_to(cr,9.5,13);cairo_line_to(cr,6.5,14);cairo_line_to(cr,6.5,8);cairo_close_path(cr);cairo_stroke(cr);break;
        case 21:case 22: icon_rgb(cr,id==22 ? blue : ink);icon_box(cr,2,2,8,10);icon_box(cr,6,5,8,9);break;
        case 23: icon_box(cr,3,3,10,11);icon_box(cr,6,1.5,4,3);icon_line(cr,6,8,10,8);icon_line(cr,6,11,10,11);break;
        case 24:case 26: cairo_move_to(cr,4,10);cairo_line_to(cr,8,6);cairo_line_to(cr,12,10);cairo_stroke(cr);break;
        case 30: icon_rgb(cr,blue);cairo_move_to(cr,4,5);cairo_curve_to(cr,14,1,16,13,7,13);cairo_stroke(cr);icon_arrow(cr,8,5,-5,0);break;
        case 31: icon_arrow(cr,8,2,0,9);icon_line(cr,3,13,13,13);break;
        default:return icon_finish(cr,caller_path,0);
        }
        return icon_finish(cr,caller_path,1);
    }
    if(disabled || id==66 || (id>=111 && id<=115)) blue=green=red=purple=orange=ink=0x92999f;
    if(id==21) { cairo_translate(cr,16,0);cairo_scale(cr,-1,1); }
    icon_rgb(cr,(id==39 || id==43 || id==34 || id==67 || id==74 || id==92 || id==95 || id==86 || id==99 || id==104 || id==109) ? red : (id==25 || id==26 || id==77 || id==107) ? ink : (id==3 || id==5 || id==24 || id==32 || id==65 || id==75 || id==81 || id==85 || id==94 || id==96 || id==97 || id==117) ? green : (id==84 || id==89 || id==106) ? purple : (id==31 || id==40 || id==72 || id==78 || id==80 || id==98) ? orange : blue);
    switch(id) {
    case 0:break;
    case 1:case 73: icon_box(cr,1.5,2,4,4);icon_box(cr,10.5,2,4,4);icon_box(cr,1.5,10,4,4);icon_box(cr,10.5,10,4,4);icon_line(cr,5.5,4,10.5,12);icon_line(cr,5.5,12,10.5,4);break;
    case 2:case 80: cairo_move_to(cr,2,4);cairo_line_to(cr,6,4);cairo_line_to(cr,7.5,6);cairo_line_to(cr,14,6);cairo_line_to(cr,13,13);cairo_line_to(cr,2,13);cairo_close_path(cr);cairo_stroke(cr);icon_line(cr,2,7,13,7);break;
    case 3: icon_line(cr,2,2,2,14);icon_line(cr,2,14,14,14);cairo_move_to(cr,4,11);cairo_line_to(cr,7,8);cairo_line_to(cr,10,9);cairo_line_to(cr,14,3);cairo_stroke(cr);break;
    case 4:case 90: icon_box(cr,9,2,5,12);icon_arrow(cr,10,8,-8,0);break;
    case 5:case 24:case 32:case 75: icon_switch(cr);break;
    case 6:case 7: icon_box(cr,2,2,12,7);cairo_select_font_face(cr,"Sans",0,1);cairo_set_font_size(cr,6.5);cairo_move_to(cr,3,7.5);cairo_show_text(cr,id==6 ? "IP" : "v6");icon_line(cr,8,10,8,14);icon_line(cr,4,12,12,12);break;
    case 8:case 9:case 76:case 79:case 87:case 114: icon_page(cr);icon_line(cr,6,8,10,8);icon_line(cr,6,11,10,11);break;
    case 10: icon_box(cr,3,2,9,12);icon_arrow(cr,10,8,-8,0);break;
    case 11: icon_page(cr);icon_line(cr,6,9,10,9);icon_line(cr,8,7,8,11);break;
    case 12:case 14:case 30:case 48:case 49:case 56:case 58:case 118: icon_monitor(cr);if(id==48) icon_box(cr,5,1,9,8);else if(id==118) icon_arrow(cr,8,1,0,7);else { icon_line(cr,5,6,11,6);icon_line(cr,5,8,9,8); }break;
    case 13:case 44:case 68: icon_dot(cr,8,3,1.8);icon_dot(cr,3,12,1.8);icon_dot(cr,13,12,1.8);icon_line(cr,7,5,4,10);icon_line(cr,9,5,12,10);icon_line(cr,5,12,11,12);break;
    case 15:case 37:case 84: icon_link(cr);break;
    case 16: icon_box(cr,1.5,3,13,10);cairo_move_to(cr,4,6);cairo_line_to(cr,6,8);cairo_line_to(cr,4,10);cairo_stroke(cr);icon_line(cr,8,10,11,10);break;
    case 17: icon_switch(cr);icon_line(cr,8,1,8,5);icon_line(cr,6,3,10,3);break;
    case 18: icon_box(cr,2,3,12,10);icon_line(cr,5,10,5,8);icon_line(cr,8,10,8,6);icon_line(cr,11,10,11,5);break;
    case 19: icon_line(cr,3,13,11,5);icon_line(cr,9,3,13,7);icon_line(cr,4,2,4,5);icon_line(cr,2.5,3.5,5.5,3.5);icon_dot(cr,13,12,1);break;
    case 20:case 38:case 65:case 66:case 67:case 71: icon_person(cr,id==20 || id==38 || id==71 ? 6 : 8,4);if(id==20) { icon_dot(cr,12,8,1.5);icon_line(cr,12,9.5,12,14);icon_line(cr,12,12,14,12); }else if(id==38 || id==71) { cairo_arc(cr,12,5,1.6,4.2,7.3);cairo_stroke(cr);cairo_arc(cr,12,12,3,4.4,6.28);cairo_stroke(cr); }break;
    case 21:case 27:case 88:case 89:case 115: cairo_move_to(cr,4,5);cairo_curve_to(cr,14,1,16,13,7,13);cairo_stroke(cr);icon_arrow(cr,8,5,-5,0);break;
    case 22:case 36:case 45:case 64: icon_arrow(cr,2,5,11,0);icon_arrow(cr,14,11,-11,0);if(id==22) { icon_line(cr,8,5,8,11); }break;
    case 23:case 29:case 42:case 54:case 116: icon_wifi(cr);if(id==42) { cairo_set_font_size(cr,5);cairo_move_to(cr,12,6);cairo_show_text(cr,"6"); }break;
    case 25:case 77: icon_gear(cr);break;
    case 26:case 107: cairo_move_to(cr,10,2);cairo_curve_to(cr,4,1,5,7,7,8);cairo_line_to(cr,2,13);cairo_line_to(cr,4,15);cairo_line_to(cr,9,10);cairo_curve_to(cr,14,11,16,6,13,4);cairo_line_to(cr,11,7);cairo_line_to(cr,9,5);cairo_close_path(cr);cairo_stroke(cr);break;
    case 28: icon_box(cr,2,2,12,12);icon_line(cr,2,5,14,5);icon_line(cr,8,7,8,12);icon_line(cr,5.5,9.5,10.5,9.5);break;
    case 31:case 35:case 39:case 40:case 41:case 43: cairo_move_to(cr,8,1.5);cairo_line_to(cr,13.5,4);cairo_line_to(cr,13,9);cairo_curve_to(cr,12,12,10,13.5,8,14.5);cairo_curve_to(cr,6,13.5,4,12,3,9);cairo_line_to(cr,2.5,4);cairo_close_path(cr);cairo_stroke(cr);if(id==39) icon_link(cr);else if(id==43 || id==41) { icon_box(cr,6,7,4,4);cairo_arc(cr,8,7,2,3.141592654,6.283185307);cairo_stroke(cr); }else { icon_line(cr,8,5,8,10);icon_dot(cr,8,12,.6); }break;
    case 33: cairo_arc(cr,8,8,5.5,0,4.71238898);cairo_line_to(cr,8,8);cairo_close_path(cr);cairo_stroke(cr);cairo_move_to(cr,9,1.5);cairo_line_to(cr,9,7);cairo_line_to(cr,14.5,7);cairo_stroke(cr);break;
    case 34:case 108: cairo_arc(cr,8,8,6,0,6.283185307);cairo_stroke(cr);cairo_arc(cr,8,8,2.5,0,6.283185307);cairo_stroke(cr);break;
    case 52:case 53:case 55:case 57: icon_box(cr,4,2,8,8);icon_line(cr,8,10,8,14);for(int i=0;i<3;i++) icon_line(cr,6+i*2,3,6+i*2,5);break;
    case 69: for(int i=0;i<4;i++) icon_box(cr,2+(i%2)*7,2+(i/2)*7,5,5);break;
    case 70:case 91:case 110: cairo_move_to(cr,5,3);cairo_line_to(cr,12,8);cairo_line_to(cr,5,13);cairo_close_path(cr);cairo_stroke(cr);break;
    case 72: cairo_arc(cr,8,6,4,0,6.283185307);cairo_stroke(cr);icon_line(cr,6,10,6,13);icon_line(cr,10,10,10,13);icon_line(cr,6,14.5,10,14.5);break;
    case 74:case 92:case 93:case 94: cairo_move_to(cr,3,10);cairo_line_to(cr,10,3);cairo_line_to(cr,13,6);cairo_line_to(cr,6,13);cairo_line_to(cr,2,14);cairo_close_path(cr);cairo_stroke(cr);icon_line(cr,8,5,11,8);break;
    case 78: cairo_move_to(cr,2,4);cairo_line_to(cr,8,1.5);cairo_line_to(cr,14,4);cairo_line_to(cr,14,12);cairo_line_to(cr,8,14.5);cairo_line_to(cr,2,12);cairo_close_path(cr);cairo_move_to(cr,2,4);cairo_line_to(cr,8,7);cairo_line_to(cr,14,4);cairo_move_to(cr,8,7);cairo_line_to(cr,8,14);cairo_stroke(cr);break;
    case 81:case 85:case 86:case 109:case 112:case 113: if(id==81 || id==109) { cairo_arc(cr,8,8,6,0,6.283185307);cairo_stroke(cr); }if(id==85 || id==112) icon_check(cr);else if(id==86 || id==113) icon_cross(cr);else if(id==109) icon_line(cr,4,12,12,4);else { icon_line(cr,4,8,12,8);icon_line(cr,8,4,8,12); }break;
    case 82: icon_arrow(cr,7,8,-5,0);icon_arrow(cr,9,8,5,0);break;
    case 83: icon_box(cr,4,4,8,8);icon_line(cr,8,1,8,4);icon_line(cr,8,12,8,15);icon_line(cr,1,8,4,8);icon_line(cr,12,8,15,8);icon_dot(cr,8,8,1);break;
    case 95:case 96: icon_box(cr,2,4,12,9);icon_line(cr,2,7,14,7);icon_line(cr,8,3,12,3);if(id==96) icon_line(cr,10,1,10,5);break;
    case 97:case 98:case 99:case 111: for(int i=0;i<3;i++) icon_line(cr,3,4+i*4,13,4+i*4);break;
    case 100:case 101: cairo_arc(cr,8,8,6,0,6.283185307);cairo_stroke(cr);cairo_save(cr);cairo_translate(cr,8,8);cairo_scale(cr,.45,1);cairo_arc(cr,0,0,6,0,6.283185307);cairo_stroke(cr);cairo_restore(cr);icon_line(cr,2,8,14,8);break;
    case 102: cairo_move_to(cr,2,2);cairo_line_to(cr,14,2);cairo_line_to(cr,2,14);cairo_close_path(cr);cairo_stroke(cr);icon_line(cr,5,5,9,5);break;
    case 103: icon_link(cr);break;
    case 104: icon_line(cr,8,2,8,10);icon_dot(cr,8,13,1);break;
    case 105:case 106: icon_box(cr,2,3,12,10);icon_arrow(cr,8,id==105 ? 4 : 12,0,id==105 ? 8 : -8);break;
    case 117: cairo_arc(cr,8,8,5.5,-.8,3.941592654);cairo_stroke(cr);icon_line(cr,8,1,8,8);break;
    default:return icon_finish(cr,caller_path,0);
    }
    return icon_finish(cr,caller_path,1);
}
