#pragma once
#include "midi_parser.h"
typedef bool (*ble_packet_fn)(void *,const uint8_t *,size_t);
typedef struct {bool sysex;uint16_t limit;uint32_t errors;ble_packet_fn sink;void *ctx;} ble_encoder_t;
typedef struct {midi_parser_t midi;uint32_t errors;uint16_t last_timestamp;} ble_decoder_t;
void ble_encoder_init(ble_encoder_t *,ble_packet_fn,void *);
bool ble_encode(ble_encoder_t *,const uint8_t *,size_t,uint16_t,uint8_t);
void ble_decoder_init(ble_decoder_t *,midi_sink_fn,void *);
bool ble_decode(ble_decoder_t *,const uint8_t *,size_t);
