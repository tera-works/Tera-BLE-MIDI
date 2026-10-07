#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"
enum { MIDI_SHORT=0, MIDI_REALTIME=1, MIDI_SYSEX=2 };
typedef void (*midi_sink_fn)(void *,const uint8_t *,size_t,uint16_t,uint8_t);
typedef struct {
 uint8_t running,status,need,used,data[2],sx[MIDI_CHUNK],sx_used;
 bool in_sysex,sysex_aborted;
 uint16_t timestamp,sx_timestamp;
 uint32_t valid,structured,invalid,bytes;
 midi_sink_fn sink;void *ctx;
} midi_parser_t;
void midi_init(midi_parser_t *,midi_sink_fn,void *);
void midi_reset(midi_parser_t *);
void midi_feed(midi_parser_t *,uint8_t,uint16_t);
void midi_flush(midi_parser_t *);
