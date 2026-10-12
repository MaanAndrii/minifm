#pragma once
#include <ctype.h>
#include <string.h>
typedef enum { STREAM_MP3, STREAM_AAC, STREAM_PLAYLIST, STREAM_WEBPAGE, STREAM_UNSUPPORTED } stream_format_t;
static inline stream_format_t stream_format(const char *type)
{
    char mime[80];unsigned i=0;
    while(type[i] && type[i]!=';' && i<sizeof(mime)-1){mime[i]=(char)tolower((unsigned char)type[i]);i++;}
    while(i && isspace((unsigned char)mime[i-1]))i--;
    mime[i]=0;
    if(strstr(mime,"mpegurl") || strstr(mime,"scpls"))return STREAM_PLAYLIST;
    if(!strncmp(mime,"text/",5))return STREAM_WEBPAGE;
    if(!strcmp(mime,"audio/aac") || !strcmp(mime,"audio/aacp") || !strcmp(mime,"audio/x-aac") || !strcmp(mime,"application/aac"))return STREAM_AAC;
    if(!mime[0] || !strcmp(mime,"audio/mpeg") || !strcmp(mime,"audio/mp3") || !strcmp(mime,"application/octet-stream"))return STREAM_MP3;
    return STREAM_UNSUPPORTED;
}
