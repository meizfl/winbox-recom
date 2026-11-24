#ifndef WINBOX_WINDOWS_COMMAND_LINE_H
#define WINBOX_WINDOWS_COMMAND_LINE_H
#include <stdlib.h>
#include <string.h>
static void windows_free_arguments(char **argv) { if(argv) { for(size_t i=0;argv[i];i++) free(argv[i]);free(argv); } }
/* CommandLineToArgv-style quoting: backslashes are special only before quotes. */
static char **windows_split_command_line(const char *line,int *count) {
    size_t length=strlen(line);if(length>65535) return NULL;
    char **argv=calloc(1025,sizeof(*argv));if(!argv) return NULL;*count=0;
    while(*line) {
        while(*line==' ' || *line=='\t') line++;
        if(!*line) break;
        if(*count==1024) goto fail;
        char *arg=malloc(length+1);if(!arg) goto fail;argv[(*count)++]=arg;int quoted=0;
        while(*line && (quoted || (*line!=' ' && *line!='\t'))) {
            size_t slashes=0;while(*line=='\\') { slashes++;line++; }
            if(*line=='"') {
                for(size_t i=0;i<slashes/2;i++) *arg++='\\';
                if(slashes&1) { *arg++='"';line++; }
                else if(quoted && line[1]=='"') { *arg++='"';line+=2; }
                else { quoted=!quoted;line++; }
            }else {
                while(slashes--) *arg++='\\';
                if(*line && (quoted || (*line!=' ' && *line!='\t'))) *arg++=*line++;
            }
        }
        *arg=0;
    }
    return argv;
fail:
    windows_free_arguments(argv);return NULL;
}
static char *windows_quote_arguments(int argc,char *const *argv) {
    size_t size=1;for(int i=0;i<argc;i++) size+=strlen(argv[i])*2+4;
    char *line=malloc(size),*out=line;if(!out) return NULL;
    for(int i=0;i<argc;i++) {
        if(i) *out++=' ';*out++='"';const char *p=argv[i];
        while(*p) {
            size_t slashes=0;while(*p=='\\') { slashes++;p++; }
            size_t emit=(*p=='"' || !*p) ? slashes*2 : slashes;
            while(emit--) *out++='\\';
            if(*p=='"') *out++='\\';
            if(*p) *out++=*p++;
        }
        *out++='"';
    }
    *out=0;return line;
}
#endif
