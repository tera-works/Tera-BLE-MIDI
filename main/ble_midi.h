#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {uint32_t generation,start_us;} ble_submit_context_t;
void ble_midi_init(void);
bool ble_midi_ready(void);
uint16_t ble_midi_payload(void);
uint32_t ble_midi_generation(void);
bool ble_midi_submit(void *,const uint8_t *,size_t);
