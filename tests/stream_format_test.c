#include "stream_format.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(stream_format("audio/mpeg")==STREAM_MP3);
    assert(stream_format("audio/aacp")==STREAM_AAC);
    assert(stream_format("Audio/AAC; codecs=HE-AAC")==STREAM_AAC);
    assert(stream_format("audio/x-aac")==STREAM_AAC);
    assert(stream_format("application/octet-stream")==STREAM_MP3);
    assert(stream_format("application/x-mpegURL")==STREAM_PLAYLIST);
    assert(stream_format("audio/x-scpls")==STREAM_PLAYLIST);
    assert(stream_format("text/html; charset=utf-8")==STREAM_WEBPAGE);
    assert(stream_format("audio/ogg")==STREAM_UNSUPPORTED);
    puts("Stream format tests passed: Lviv MP3, Ternopil AAC+, MIME parameters, playlist and HTML errors");
    return 0;
}
