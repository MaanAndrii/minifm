#pragma once
#include "app.h"
static inline void audio_toggle_pause(audio_request_t *request,state_t *state)
{
    request->paused=!request->paused;state->paused=request->paused;
    if(request->mode==AUDIO_RADIO)request->generation++;
}
