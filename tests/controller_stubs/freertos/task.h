#pragma once
static inline int xTaskCreate(void (*fn)(void *),const char *name,int size,void *arg,int priority,void *handle){return 1;}
static inline void vTaskDelay(int ticks){}
