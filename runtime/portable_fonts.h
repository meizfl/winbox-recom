/* Minimal Fontconfig config for static builds; independent of host XML versions. */
static const char portable_font_config[] =
"<?xml version=\"1.0\"?>\n"
"<!DOCTYPE fontconfig SYSTEM \"urn:fontconfig:fonts.dtd\">\n"
"<fontconfig>\n"
"  <dir>/System/Library/Fonts</dir>\n"
"  <dir>/Library/Fonts</dir>\n"
"  <dir>~/Library/Fonts</dir>\n"
"  <dir>/usr/share/fonts</dir>\n"
"  <dir>/usr/local/share/fonts</dir>\n"
"  <dir prefix=\"xdg\">fonts</dir>\n"
"  <cachedir prefix=\"xdg\">fontconfig</cachedir>\n"
"  <alias><family>Tahoma</family><accept><family>Arial</family><family>Helvetica</family><family>DejaVu Sans</family></accept></alias>\n"
"  <alias><family>Courier New</family><accept><family>Courier</family><family>Menlo</family><family>DejaVu Sans Mono</family></accept></alias>\n"
"  <alias><family>sans-serif</family><prefer><family>Tahoma</family><family>Arial</family><family>Helvetica</family><family>DejaVu Sans</family></prefer></alias>\n"
"  <alias><family>monospace</family><prefer><family>Courier New</family><family>Menlo</family><family>Courier</family><family>DejaVu Sans Mono</family></prefer></alias>\n"
"</fontconfig>\n"
;
