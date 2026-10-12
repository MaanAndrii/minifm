#pragma once
#include <stddef.h>
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
static inline int nvs_open(const char *name,int mode,nvs_handle_t *out){*out=1;return 0;}
static inline int nvs_get_blob(int nvs,const char *key,void *data,size_t *size){return -1;}
static inline int nvs_set_blob(int nvs,const char *key,const void *data,size_t size){return 0;}
static inline int nvs_commit(int nvs){return 0;}
static inline void nvs_close(int nvs){}
