#pragma once
#include "midi_parser.h"
typedef struct {
 midi_parser_t evidence;uint32_t baud,errors;bool searching,confirmed;
 unsigned candidate;uint32_t trial_activity_ms;bool trial_active;int last_score;
} baud_detect_t;
void baud_init(baud_detect_t *,uint32_t);
bool baud_supported(uint32_t);
int baud_score(const baud_detect_t *);
bool baud_confident(const baud_detect_t *);
bool baud_observe(baud_detect_t *,const uint8_t *,size_t,unsigned,uint32_t);
bool baud_advance(baud_detect_t *);
