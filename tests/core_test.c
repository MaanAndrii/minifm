#include "core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    struct tm t={.tm_year=126,.tm_yday=283,.tm_wday=0,.tm_hour=7,.tm_min=30};
    int64_t last=-1;
    assert(!core_alarm_due(&t,7,30,1,false,&last));
    assert(!core_alarm_due(&t,7,30,2,true,&last));
    assert(core_alarm_due(&t,7,30,1,true,&last));
    assert(!core_alarm_due(&t,7,30,1,true,&last));
    t.tm_hour=6;assert(!core_alarm_due(&t,7,30,1,true,&last));
    t.tm_hour=7;assert(!core_alarm_due(&t,7,30,1,true,&last));
    t.tm_yday++;t.tm_wday=1;assert(core_alarm_due(&t,7,30,127,true,&last));
    t.tm_yday--;t.tm_wday=0;assert(!core_alarm_due(&t,7,30,127,true,&last));
    assert(core_recording_name("rec-20261011-123000-12345678-1.wav"));
    assert(!core_recording_name("../secret.wav"));assert(!core_recording_name("a/b.wav"));
    assert(!core_recording_name("x.part"));assert(!core_recording_name("x.wav.exe"));assert(!core_recording_name(".wav"));
    uint8_t header[44];core_wav_header(header,32000,16000);
    assert(!memcmp(header,"RIFF",4));assert(!memcmp(header+8,"WAVEfmt ",8));
    assert(header[4]==0x24 && header[5]==0x7d);assert(header[22]==1 && header[34]==16);
    assert(header[24]==0x80 && header[25]==0x3e);assert(header[40]==0 && header[41]==0x7d);
    puts("core tests passed: calendar scheduling, repeated-hour protection, file paths, WAV header");
    return 0;
}
