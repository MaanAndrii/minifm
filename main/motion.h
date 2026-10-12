#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {bool valid;int rotation;uint32_t lifts;} motion_status_t;
void motion_start(void);
void motion_snapshot(motion_status_t *out);
