#pragma once
#include "midi_parser.h"
#define MIDI_NAME_MAX 29
typedef void (*name_set_fn)(const char *);
typedef struct {
 uint8_t prefix[7],used,mode,length;
 char name[MIDI_NAME_MAX+1];
 uint16_t timestamp;
 midi_sink_fn sink;void *ctx;name_set_fn set;
} name_filter_t;
void name_filter_init(name_filter_t *,midi_sink_fn,void *,name_set_fn);
void name_filter_reset(name_filter_t *);
bool name_filter_abort(name_filter_t *);
void name_filter_feed(name_filter_t *,const uint8_t *,size_t,uint16_t,uint8_t);
bool name_valid(const char *);
