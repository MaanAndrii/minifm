#include "audio_pause.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    audio_request_t r={.mode=AUDIO_RADIO,.station=2,.generation=10,.source="https://onair.lviv.fm/ternopil.fm"};
    state_t s={.mode=AUDIO_RADIO,.station=2};
    unsigned active_connection=r.generation;
    audio_toggle_pause(&r,&s);
    assert(r.paused && s.paused && r.generation!=active_connection);
    assert(r.mode==AUDIO_RADIO && r.station==2 && s.station==2);
    assert(!strcmp(r.source,"https://onair.lviv.fm/ternopil.fm"));
    unsigned paused_request=r.generation;
    audio_toggle_pause(&r,&s);
    assert(!r.paused && !s.paused && r.generation!=paused_request && r.generation!=active_connection);
    assert(r.station==2 && !strcmp(r.source,"https://onair.lviv.fm/ternopil.fm"));
    r.mode=AUDIO_RECORD;unsigned recording=r.generation;
    audio_toggle_pause(&r,&s);assert(r.paused && r.generation==recording);
    audio_toggle_pause(&r,&s);assert(!r.paused && r.generation==recording);
    puts("Pause tests passed: connection invalidation, fresh resume, station/URL retention, recorder unchanged");
    return 0;
}
