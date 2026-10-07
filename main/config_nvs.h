#pragma once
#include <stdint.h>
uint32_t config_load(uint32_t *source);
void config_save_later(uint32_t);
void config_activity(void);
void config_link_changed(void);
const char *config_name(void);
void config_name_later(const char *);
