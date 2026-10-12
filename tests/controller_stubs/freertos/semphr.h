#pragma once
typedef void *SemaphoreHandle_t;
static inline void *xSemaphoreCreateMutex(void){return (void *)1;}
static inline void xSemaphoreTake(void *m,int timeout){}
static inline void xSemaphoreGive(void *m){}
