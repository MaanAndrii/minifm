from pathlib import Path
import subprocess
root=Path(__file__).resolve().parent.parent
source=(root/'main/vendor/audio_codec_data_i2s.c').read_text()
start=source.index('static int _i2s_drv_enable(')
end=source.index('\n#if ESP_IDF_VERSION',source.index('\n}\n',start)+3)
helper=source[start:end]
stubs='''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_IDF_VERSION 50000
#define ESP_IDF_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))
#define ESP_OK 0
#define ESP_CODEC_DEV_OK 0
#define ESP_CODEC_DEV_NOT_FOUND -1
#define ESP_CODEC_DEV_DRV_ERR -2
typedef void *i2s_chan_handle_t;
typedef struct {void *out_handle,*in_handle;bool tx_running,rx_running;} i2s_data_t;
static int enables,disables,fail;
static int i2s_channel_enable(void *h){enables++;return fail;}
static int i2s_channel_disable(void *h){disables++;return fail;}
'''
checks='''
int main(void){
 i2s_data_t d={(void*)1,(void*)2,true,true};
 assert(_i2s_drv_enable(&d,true,false)==0 && disables==1 && !d.tx_running);
 assert(_i2s_drv_enable(&d,true,false)==0 && disables==1);
 assert(_i2s_drv_enable(&d,true,true)==0 && enables==1 && d.tx_running);
 assert(_i2s_drv_enable(&d,true,true)==0 && enables==1);
 assert(_i2s_drv_enable(&d,false,false)==0 && disables==2 && !d.rx_running && d.tx_running);
 fail=1;assert(_i2s_drv_enable(&d,true,false)==-2 && d.tx_running);
 fail=0;assert(_i2s_drv_enable(&d,true,false)==0 && !d.tx_running);
 d.in_handle=NULL;assert(_i2s_drv_enable(&d,false,false)==-1);
 return 0;
}
'''
p=root/'.tools/host/i2s_state_test.c';p.write_text(stubs+helper+checks)
binary=p.with_suffix('.exe')
subprocess.run([str(root/'.tools/host/tcc/tcc.exe'),str(p),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
print('I2S state tests passed: repeat calls, independent RX/TX, failed transition retry, missing handle')
