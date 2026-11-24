#include <stdio.h>
extern int ui_theme_composition_test(void);
extern int ui_drag_render_benchmark(void);
int main(void) { if(!ui_theme_composition_test() || !ui_drag_render_benchmark()) { fputs("Overlapping MDI theme composition failed\n",stderr);return 1; }puts("Overlapping MDI captions, borders, movement and original surfaces: passed");return 0; }
