#include "core.h"
#include <string.h>
#include <ctype.h>
bool core_alarm_due(const struct tm *t, unsigned h, unsigned m, unsigned days,
                    bool enabled, int64_t *last_day)
{
    if (!enabled || !(days & (1U << t->tm_wday)) || t->tm_hour != (int)h || t->tm_min != (int)m) return false;
    int64_t day = (int64_t)(t->tm_year + 1900) * 366 + t->tm_yday;
    // Prevent a repeated alarm when the clock moves backwards or DST repeats an hour.
    if (day <= *last_day) return false;
    *last_day = day;
    return true;
}
bool core_recording_name(const char *s)
{
    if (!s) return false;
    size_t n = strlen(s);
    if (n < 5 || n > 80 || strcmp(s+n-4, ".wav")) return false;
    for (size_t i=0; i<n-4; ++i) if (!isalnum((unsigned char)s[i]) && s[i]!='-' && s[i]!='_') return false;
    return true;
}
static void le16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }
static void le32(uint8_t *p, uint32_t v) { for (int i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8)); }
void core_wav_header(uint8_t h[44], uint32_t bytes, uint32_t rate)
{
    memset(h,0,44); memcpy(h,"RIFF",4); le32(h+4,36+bytes); memcpy(h+8,"WAVEfmt ",8);
    le32(h+16,16); le16(h+20,1); le16(h+22,1); le32(h+24,rate); le32(h+28,rate*2);
    le16(h+32,2); le16(h+34,16); memcpy(h+36,"data",4); le32(h+40,bytes);
}
