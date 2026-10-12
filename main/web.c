#include "app.h"
#include "core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
extern const unsigned char index_start[] asm("_binary_index_html_start");
extern const unsigned char index_end[] asm("_binary_index_html_end");
static atomic_int downloads;
typedef struct {httpd_req_t *request;char path[192],name[96];} download_t;
static bool authorized(httpd_req_t *req)
{
    char token[34];size_t size=httpd_req_get_hdr_value_len(req,"X-Device-Token");
    if(size!=32 || httpd_req_get_hdr_value_str(req,"X-Device-Token",token,sizeof(token))!=ESP_OK)goto deny;
    unsigned difference=0;for(int i=0;i<32;i++)difference|=(unsigned char)token[i]^(unsigned char)g_settings.token[i];
    if(!difference)return true;
deny:
    httpd_resp_send_err(req,HTTPD_401_UNAUTHORIZED,"Enter the device access key shown on the display");return false;
}
static esp_err_t send_json(httpd_req_t *req,cJSON *j)
{
    if(!j)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);
    if(!body)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t err=httpd_resp_send(req,body,HTTPD_RESP_USE_STRLEN);free(body);return err;
}
static esp_err_t index_page(httpd_req_t *req)
{
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; media-src 'self' blob:; frame-ancestors 'none'");
    return httpd_resp_send(req,(const char *)index_start,index_end-index_start-1);
}
static esp_err_t status(httpd_req_t *req) {if(!authorized(req))return ESP_OK;return send_json(req,app_status_json());}
static esp_err_t settings(httpd_req_t *req) {if(!authorized(req))return ESP_OK;return send_json(req,app_settings_json());}
static esp_err_t command(httpd_req_t *req)
{
    if(!authorized(req))return ESP_OK;
    if(req->content_len<=0 || req->content_len>16384)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid command length");
    char *body=calloc(1,req->content_len+1);if(!body)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    int read=0;while(read<req->content_len) {int n=httpd_req_recv(req,body+read,req->content_len-read);if(n<=0){free(body);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Incomplete request");}read+=n;}
    cJSON *j=cJSON_ParseWithLength(body,req->content_len);free(body);
    if(!cJSON_IsObject(j)){cJSON_Delete(j);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid JSON object");}
    cJSON *action=cJSON_GetObjectItemCaseSensitive(j,"action");bool restart=cJSON_IsString(action)&&!strcmp(action->valuestring,"restart");
    state_t state;app_snapshot(NULL,&state);
    esp_err_t err=restart?(state.mode==AUDIO_RECORD || audio_record_busy()?ESP_ERR_INVALID_STATE:ESP_OK):app_command(j);cJSON_Delete(j);
    if(err!=ESP_OK) {httpd_resp_set_status(req,err==ESP_ERR_INVALID_STATE?"409 Conflict":"400 Bad Request");cJSON *error=cJSON_CreateObject();cJSON_AddStringToObject(error,"error",esp_err_to_name(err));return send_json(req,error);}
    cJSON *ok=cJSON_CreateObject();cJSON_AddBoolToObject(ok,"ok",true);esp_err_t result=send_json(req,ok);
    if(restart){vTaskDelay(pdMS_TO_TICKS(200));esp_restart();}return result;
}
static esp_err_t recordings(httpd_req_t *req)
{
    if(!authorized(req))return ESP_OK;
    state_t state;app_snapshot(NULL,&state);cJSON *j=cJSON_CreateObject();cJSON *files=cJSON_AddArrayToObject(j,"files");
    uint64_t total=0,free_bytes=0;if(state.sd_ready)esp_vfs_fat_info(BSP_SD_MOUNT_POINT,&total,&free_bytes);
    cJSON_AddNumberToObject(j,"freeBytes",free_bytes);cJSON_AddBoolToObject(j,"ready",state.sd_ready);
    DIR *dir=state.sd_ready?opendir(BSP_SD_MOUNT_POINT "/recordings"):NULL;
    if(dir){struct dirent *e;int count=0;while((e=readdir(dir))&&count<256){if(!core_recording_name(e->d_name))continue;
        char path[256];snprintf(path,sizeof(path),BSP_SD_MOUNT_POINT "/recordings/%.80s",e->d_name);struct stat st;if(stat(path,&st)||!S_ISREG(st.st_mode))continue;
        cJSON *file=cJSON_CreateObject();cJSON_AddStringToObject(file,"name",e->d_name);cJSON_AddNumberToObject(file,"bytes",st.st_size);cJSON_AddNumberToObject(file,"seconds",st.st_size>44?(st.st_size-44)/32000:0);cJSON_AddItemToArray(files,file);count++;
    }closedir(dir);}return send_json(req,j);
}
static void download_task(void *arg)
{
    download_t *download=arg;httpd_req_t *req=download->request;
    FILE *f=fopen(download->path,"rb");
    if(!f)httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Recording unavailable");
    else {
        httpd_resp_set_type(req,"audio/wav");httpd_resp_set_hdr(req,"Cache-Control","no-store");
        char disposition[128];snprintf(disposition,sizeof(disposition),"attachment; filename=\"%s\"",download->name);httpd_resp_set_hdr(req,"Content-Disposition",disposition);
        char block[2048];size_t n;esp_err_t err=ESP_OK;
        while((n=fread(block,1,sizeof(block),f))>0){err=httpd_resp_send_chunk(req,block,n);if(err!=ESP_OK)break;}
        if(ferror(f))err=ESP_FAIL;
        fclose(f);if(err==ESP_OK)httpd_resp_send_chunk(req,NULL,0);
    }
    httpd_req_async_handler_complete(req);free(download);atomic_fetch_sub(&downloads,1);vTaskDelete(NULL);
}
static esp_err_t file_route(httpd_req_t *req)
{
    if(!authorized(req))return ESP_OK;
    char query[128],name[96],path[192];
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK || httpd_query_key_value(query,"name",name,sizeof(name))!=ESP_OK || !core_recording_name(name))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid recording name");
    snprintf(path,sizeof(path),BSP_SD_MOUNT_POINT "/recordings/%s",name);
    state_t state;app_snapshot(NULL,&state);
    if(!state.sd_ready)return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"microSD unavailable");
    if(req->method==HTTP_DELETE){
        if(state.mode==AUDIO_FILE || state.mode==AUDIO_RECORD || audio_file_busy(name)){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Stop recording/playback before deletion");}
        if(unlink(path))return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Recording unavailable or busy");
        return httpd_resp_sendstr(req,"Deleted");
    }
    if(atomic_load(&downloads)>=2){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Two downloads already active");}
    download_t *download=calloc(1,sizeof(*download));if(!download)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    strcpy(download->path,path);strcpy(download->name,name);
    if(httpd_req_async_handler_begin(req,&download->request)!=ESP_OK){free(download);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Cannot start download");}
    atomic_fetch_add(&downloads,1);
    if(xTaskCreate(download_task,"download",6144,download,3,NULL)!=pdPASS){httpd_resp_send_err(download->request,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");httpd_req_async_handler_complete(download->request);atomic_fetch_sub(&downloads,1);free(download);}
    return ESP_OK;
}
void web_start(void)
{
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.stack_size=8192;config.max_uri_handlers=8;
    config.recv_wait_timeout=5;config.send_wait_timeout=5;config.lru_purge_enable=true;
    httpd_handle_t server;ESP_ERROR_CHECK(httpd_start(&server,&config));
    httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=index_page},
        {.uri="/api/status",.method=HTTP_GET,.handler=status},
        {.uri="/api/settings",.method=HTTP_GET,.handler=settings},
        {.uri="/api/command",.method=HTTP_POST,.handler=command},
        {.uri="/api/recordings",.method=HTTP_GET,.handler=recordings},
        {.uri="/api/file",.method=HTTP_GET,.handler=file_route},
        {.uri="/api/file",.method=HTTP_DELETE,.handler=file_route}
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));
}
