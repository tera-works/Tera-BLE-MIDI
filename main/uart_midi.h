#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void uart_midi_init(uint32_t);
bool uart_midi_write(const uint8_t *,size_t);
void uart_midi_session(void);
void uart_midi_abort_sysex(void);
