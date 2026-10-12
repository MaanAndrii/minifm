#include "app.h"
#include "core.h"
#include "stream_format.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_audio_simple_dec.h"
#include "impl/esp_aac_dec.h"
#include "bsp/esp-bsp.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
static esp_codec_dev_handle_t speaker,microphone;
static atomic_bool recording_busy,file_busy;
static atomic_uint file_sequence;
static int current_volume=-1;
bool audio_record_busy(void) {return atomic_load(&recording_busy);}
bool audio_file_busy(const char *name) {(void)name;return atomic_load(&file_busy)||atomic_load(&recording_busy);}
static bool active(uint32_t gen,bool *paused)
{
    audio_request_t r;int volume;app_audio_request(&r,&volume);
    if(paused)*paused=r.paused;
    if(volume!=current_volume) {esp_codec_dev_set_out_vol(speaker,volume);current_volume=volume;}
    return r.generation==gen;
}
static bool open_output(int rate)
{
    esp_codec_dev_close(microphone);esp_codec_dev_close(speaker);
    esp_codec_dev_sample_info_t info={.sample_rate=rate,.channel=1,.bits_per_sample=16};
    if(esp_codec_dev_open(speaker,&info)!=ESP_OK)return false;
    current_volume=-1;return true;
}
static bool write_pcm(int16_t *pcm,int samples,int channels)
{
    if(channels==2)for(int i=0;i<samples;i++)pcm[i]=(int16_t)(((int32_t)pcm[i*2]+pcm[i*2+1])/2);
    return esp_codec_dev_write(speaker,pcm,samples*sizeof(int16_t))==ESP_OK;
}
static void tone(uint32_t gen)
{
    if(!open_output(16000)) {app_audio_finished(gen,"Speaker unavailable");return;}
    int16_t pcm[320];int phase=0;
    while(active(gen,NULL)) {
        bool beep=(esp_timer_get_time()/400000)%2==0;
        for(int i=0;i<320;i++,phase++)pcm[i]=beep?(int16_t)(7000*sinf(2.0f*3.14159265f*880*(phase%16000)/16000)):0;
        if(!write_pcm(pcm,320,1))break;
    }
    esp_codec_dev_close(speaker);
}
typedef struct {int metadata_interval;stream_format_t format;} stream_headers_t;
static esp_err_t stream_event(esp_http_client_event_t *event)
{
    if(event->event_id!=HTTP_EVENT_ON_HEADER)return ESP_OK;
    stream_headers_t *headers=event->user_data;
    if(!strcasecmp(event->header_key,"icy-metaint"))headers->metadata_interval=atoi(event->header_value);
    if(!strcasecmp(event->header_key,"Content-Type")) {
        headers->format=stream_format(event->header_value);
    }
    return ESP_OK;
}
static bool play_aac(esp_http_client_handle_t http,uint32_t generation,char failure[128])
{
    esp_audio_simple_dec_handle_t decoder=NULL;
    esp_aac_dec_cfg_t aac_cfg={.aac_plus_enable=true};
    esp_audio_simple_dec_cfg_t config={.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_AAC,
        .dec_cfg=&aac_cfg,.cfg_size=sizeof(aac_cfg),.use_frame_dec=false};
    uint8_t *input=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    unsigned capacity=8192;
    uint8_t *pcm=heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool good=false;int rate=0;int64_t last_pcm=esp_timer_get_time();
    if(!input || !pcm || esp_audio_simple_dec_open(&config,&decoder)!=ESP_AUDIO_ERR_OK) {
        snprintf(failure,128,"AAC decoder initialization failed");goto done;
    }
    while(active(generation,NULL)) {
        int count=esp_http_client_read(http,(char *)input,4096);
        if(!active(generation,NULL))break;
        if(count<0 || (count==0 && esp_http_client_is_complete_data_received(http)))break;
        esp_audio_simple_dec_raw_t raw={.buffer=input,.len=count};
        while(raw.len && active(generation,NULL)) {
            esp_audio_simple_dec_out_t out={.buffer=pcm,.len=capacity};
            esp_audio_err_t result=esp_audio_simple_dec_process(decoder,&raw,&out);
            if(result==ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                if(out.needed_size<=capacity || out.needed_size>32768) {
                    snprintf(failure,128,"AAC PCM frame too large");goto done;
                }
                uint8_t *larger=heap_caps_realloc(pcm,out.needed_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
                if(!larger){snprintf(failure,128,"Not enough memory for AAC PCM");goto done;}
                pcm=larger;capacity=out.needed_size;continue;
            }
            if(result!=ESP_AUDIO_ERR_OK || raw.consumed>raw.len || (!raw.consumed && !out.decoded_size)) {
                snprintf(failure,128,"AAC stream decode error (%d)",result);goto done;
            }
            if(out.decoded_size) {
                esp_audio_simple_dec_info_t info={0};
                if(esp_audio_simple_dec_get_info(decoder,&info)!=ESP_AUDIO_ERR_OK ||
                    info.bits_per_sample!=16 || info.channel<1 || info.channel>2 ||
                    !info.sample_rate || out.decoded_size>capacity || out.decoded_size%(2*info.channel)) {
                    snprintf(failure,128,"Unsupported AAC PCM format");goto done;
                }
                if((int)info.sample_rate!=rate){rate=info.sample_rate;if(!open_output(rate))goto done;}
                if(!write_pcm((int16_t *)pcm,out.decoded_size/(2*info.channel),info.channel))goto done;
                if(!good)ESP_LOGI("audio","AAC/AAC+ playback: %d Hz, %u channels; minimum free stack=%u bytes",
                    rate,info.channel,(unsigned)uxTaskGetStackHighWaterMark(NULL));
                good=true;last_pcm=esp_timer_get_time();app_audio_status(generation,"Playing AAC",0,0);
            }
            raw.len-=raw.consumed;raw.buffer+=raw.consumed;
        }
        if(esp_timer_get_time()-last_pcm>10000000){snprintf(failure,128,"AAC stream produced no audio");break;}
        if(!count)vTaskDelay(pdMS_TO_TICKS(10));
    }
done:
    if(decoder)esp_audio_simple_dec_close(decoder);
    free(pcm);free(input);return good;
}
static bool radio_connection(audio_request_t *r,char failure[128])
{
    stream_headers_t headers={0};
    esp_http_client_config_t config={.url=r->source,.timeout_ms=1500,.buffer_size=2048,
        .crt_bundle_attach=esp_crt_bundle_attach,.user_agent="AMOLED-Radio/0.1",
        .event_handler=stream_event,.user_data=&headers};
    esp_http_client_handle_t http=esp_http_client_init(&config);if(!http)return false;
    esp_http_client_set_header(http,"Icy-MetaData","0");
    int redirects=0;bool good=false;uint8_t *input=NULL;int16_t *pcm=NULL;mp3dec_t *decoder=NULL;
    while(active(r->generation,NULL)) {
        memset(&headers,0,sizeof(headers));
        if(esp_http_client_open(http,0)!=ESP_OK)goto done;
        if(esp_http_client_fetch_headers(http)<0)goto done;
        int status=esp_http_client_get_status_code(http);
        if(status>=300 && status<400 && redirects++<5) {esp_http_client_close(http);if(esp_http_client_set_redirection(http)!=ESP_OK)goto done;continue;}
        if(status!=200)goto done;
        break;
    }
    if(headers.metadata_interval>0) {
        snprintf(failure,128,"Forced ICY metadata is not supported yet");goto done;
    }
    if(headers.format==STREAM_PLAYLIST){snprintf(failure,128,"Playlist URL: use its direct audio stream");goto done;}
    if(headers.format==STREAM_WEBPAGE){snprintf(failure,128,"URL returned text instead of audio");goto done;}
    if(headers.format==STREAM_UNSUPPORTED){snprintf(failure,128,"Unsupported audio format; use MP3 or AAC/AAC+");goto done;}
    if(headers.format==STREAM_AAC){good=play_aac(http,r->generation,failure);goto done;}
    input=heap_caps_malloc(16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    pcm=heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    decoder=heap_caps_malloc(sizeof(*decoder),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!input||!pcm||!decoder)goto done;
    mp3dec_init(decoder);int used=0,rate=0;int64_t last_pcm=esp_timer_get_time();
    while(active(r->generation,NULL)) {
        if(used<8192) {
            int count=esp_http_client_read(http,(char *)input+used,16384-used);
            if(count>0)used+=count;
            else if(count<0 || esp_http_client_is_complete_data_received(http))break;
        }
        if(!active(r->generation,NULL))break;
        mp3dec_frame_info_t info={0};int samples=mp3dec_decode_frame(decoder,input,used,pcm,&info);
        if(info.frame_bytes>0) {used-=info.frame_bytes;memmove(input,input+info.frame_bytes,used);}
        if(samples>0) {
            if(info.hz!=rate) {rate=info.hz;if(!open_output(rate))break;}
            if(!write_pcm(pcm,samples,info.channels))break;
            if(!good)ESP_LOGI("audio","MP3 playback started; minimum free stack=%u bytes",
                (unsigned)uxTaskGetStackHighWaterMark(NULL));
            last_pcm=esp_timer_get_time();good=true;app_audio_status(r->generation,"Playing MP3",0,0);
        } else {
            if(used==16384) {memmove(input,input+used-4096,4096);used=4096;}
            if(esp_timer_get_time()-last_pcm>10LL*1000000)break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
done:
    free(decoder);free(pcm);free(input);esp_http_client_cleanup(http);esp_codec_dev_close(speaker);return good;
}
static void radio(audio_request_t *r)
{
    int failures=0;
    while(active(r->generation,NULL)) {
        app_audio_status(r->generation,"Connecting to radio",0,0);
        char failure[128]="Stream unavailable; retrying";
        bool played=radio_connection(r,failure);if(!active(r->generation,NULL))break;
        if(played)failures=0;
        failures++;
        state_t state;app_snapshot(NULL,&state);
        if(state.ringing) {app_audio_status(r->generation,"Radio unavailable; local alarm tone",0,0);tone(r->generation);break;}
        app_audio_status(r->generation,failure,0,0);
        int ticks=(failures>5?50:failures*10);
        for(int i=0;i<ticks && active(r->generation,NULL);i++)vTaskDelay(pdMS_TO_TICKS(100));
    }
}
static bool header(FILE *f,uint32_t bytes)
{
    uint8_t h[44];core_wav_header(h,bytes,16000);
    long pos=ftell(f);if(fseek(f,0,SEEK_SET))return false;
    bool ok=fwrite(h,1,44,f)==44 && fflush(f)==0;
    if(fseek(f,pos<44?44:pos,SEEK_SET))ok=false;
    return ok;
}
static void record(audio_request_t *r)
{
    atomic_store(&recording_busy,true);FILE *f=NULL;uint32_t bytes=0;bool ok=true;
    char base[96],partial[192],final[192];
    time_t wall=time(NULL);struct tm local;localtime_r(&wall,&local);
    char stamp[40];strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",&local);
    snprintf(base,sizeof(base),"rec-%s-%08lx-%u",stamp,(unsigned long)esp_random(),atomic_fetch_add(&file_sequence,1));
    snprintf(partial,sizeof(partial),BSP_SD_MOUNT_POINT "/recordings/%s.part",base);
    snprintf(final,sizeof(final),BSP_SD_MOUNT_POINT "/recordings/%s.wav",base);
    esp_codec_dev_close(speaker);esp_codec_dev_close(microphone);
    esp_codec_dev_sample_info_t info={.sample_rate=16000,.channel=2,.bits_per_sample=16};
    if(esp_codec_dev_open(microphone,&info)!=ESP_OK) {app_audio_finished(r->generation,"Microphone unavailable");goto done;}
    esp_codec_dev_set_in_gain(microphone,24.0f);
    f=fopen(partial,"wb+");if(!f) {app_audio_finished(r->generation,"Cannot create recording on microSD");goto done;}
    if(!header(f,0)) {ok=false;goto finish;}
    int16_t stereo[640],mono[320];uint32_t last_checkpoint=0;
    while(active(r->generation,NULL)) {
        bool paused;active(r->generation,&paused);
        if(esp_codec_dev_read(microphone,stereo,sizeof(stereo))!=ESP_OK) {ok=false;break;}
        int peak=0;
        for(int i=0;i<320;i++) { // First microphone: avoids phase cancellation when summing two microphones.
            mono[i]=stereo[i*2];int amplitude=abs((int)mono[i]);if(amplitude>peak)peak=amplitude;
        }
        if(!paused) {
            if(bytes>0x7fffffffU-sizeof(mono))break;
            size_t wrote=fwrite(mono,1,sizeof(mono),f);bytes+=wrote;
            if(wrote!=sizeof(mono)){ok=false;break;}
            if(bytes-last_checkpoint>=32000) {if(!header(f,bytes)){ok=false;break;}last_checkpoint=bytes;}
        }
        app_audio_status(r->generation,paused?"Recording paused":"Recording microphone",peak,bytes);
    }
finish:
    if(!header(f,bytes))ok=false;
    if(fclose(f))ok=false;
    f=NULL;
    if(ok && rename(partial,final))ok=false;
    app_audio_finished(r->generation,ok?"Recording saved":"SD write error; partial file retained");
done:
    if(f)fclose(f);
    esp_codec_dev_close(microphone);atomic_store(&recording_busy,false);
}
static uint32_t read32(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void play_file(audio_request_t *r)
{
    atomic_store(&file_busy,true);char path[192];snprintf(path,sizeof(path),BSP_SD_MOUNT_POINT "/recordings/%.80s",r->source);
    FILE *f=fopen(path,"rb");const char *result="Recording unavailable";
    if(f) {
        uint8_t h[44];
        if(fread(h,1,44,f)==44 && !memcmp(h,"RIFF",4) && !memcmp(h+8,"WAVEfmt ",8) && !memcmp(h+36,"data",4)
           && h[20]==1 && h[22]==1 && h[34]==16 && read32(h+24)==16000 && open_output(16000)) {
            int16_t pcm[320];size_t count;result="Playback finished";
            while(active(r->generation,NULL) && (count=fread(pcm,sizeof(int16_t),320,f))>0)if(!write_pcm(pcm,count,1)){result="Speaker write failed";break;}
        } else result="Unsupported WAV format";
        fclose(f);
    }
    esp_codec_dev_close(speaker);atomic_store(&file_busy,false);app_audio_finished(r->generation,result);
}
static void recover_recordings(void)
{
    DIR *dir=opendir(BSP_SD_MOUNT_POINT "/recordings");if(!dir)return;struct dirent *e;
    while((e=readdir(dir))) {
        size_t n=strlen(e->d_name);if(n<6 || n>80 || strcmp(e->d_name+n-5,".part"))continue;
        char p[256],dest[256];snprintf(p,sizeof(p),BSP_SD_MOUNT_POINT "/recordings/%.80s",e->d_name);
        struct stat st;if(stat(p,&st)||st.st_size<44||st.st_size>0x7fffffff)continue;
        FILE *f=fopen(p,"rb+");if(!f)continue;bool ok=header(f,((uint32_t)st.st_size-44)&~1U);fclose(f);
        snprintf(dest,sizeof(dest),BSP_SD_MOUNT_POINT "/recordings/%.*s.wav",(int)(n-5),e->d_name);if(ok)rename(p,dest);
    }
    closedir(dir);
}
static void audio_task(void *arg)
{
    (void)arg;
    if(esp_aac_dec_register()!=ESP_AUDIO_ERR_OK)ESP_LOGE("audio","AAC decoder registration failed");
    speaker=bsp_audio_codec_speaker_init();microphone=bsp_audio_codec_microphone_init();
    if(!speaker||!microphone) {audio_request_t r;int volume;app_audio_request(&r,&volume);app_audio_status(r.generation,"Audio hardware initialization failed",0,0);vTaskDelete(NULL);return;}
    esp_err_t mounted=bsp_sdcard_mount();bool sd=mounted==ESP_OK;
    if(sd) {mkdir(BSP_SD_MOUNT_POINT "/recordings",0775);recover_recordings();}
    app_sd_ready(sd);
    uint32_t handled=0;
    for(;;) {
        audio_request_t r;int volume;app_audio_request(&r,&volume);
        if(r.generation!=handled) {
            handled=r.generation;
            switch(r.mode) {case AUDIO_RADIO:if(!r.paused)radio(&r);break;case AUDIO_RECORD:record(&r);break;case AUDIO_FILE:play_file(&r);break;case AUDIO_TONE:tone(r.generation);break;default:break;}
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
// Compiler stack report: mp3dec_decode_frame alone needs 16,304 bytes,
// excluding its callers and nested synthesis functions. IDF stack sizes are bytes.
void audio_start(void) {configASSERT(xTaskCreatePinnedToCore(audio_task,"audio",32768,NULL,5,NULL,1)==pdPASS);}
