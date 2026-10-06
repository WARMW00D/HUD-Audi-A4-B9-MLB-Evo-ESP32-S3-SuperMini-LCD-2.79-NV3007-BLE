#pragma once
using SemaphoreHandle_t = void*;
#define portMAX_DELAY 0xffffffff
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return (void*)1;}
inline void xSemaphoreTake(void*,unsigned){}
inline void xSemaphoreGive(void*){}
