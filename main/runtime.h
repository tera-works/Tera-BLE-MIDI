#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "config.h"
typedef struct {
 uint32_t uart_to_ble,ble_to_uart,uart_errors,rx_overflow,tx_overflow,notify_overflow,
  notify_errors,unconnected_drop,stale_drop,parse_uart,parse_ble,current_baud,baud_source,baud_score,
  ci_units,latency,mtu,connected,subscribed,adv_fast,notify_latency_max_us,uart_latency_max_us,
  notify_latency_sum,notify_latency_count,uart_latency_sum,uart_latency_count,baud_searching,
  led_notify,led_uart;
#if CONFIG_TERA_DEBUG
 uint32_t notify_queue_sum,notify_queue_max,notify_api_sum,notify_api_max;
#endif
} runtime_stats_t;
extern runtime_stats_t stats;
#define STAT_INC(x) __atomic_fetch_add(&stats.x,1,__ATOMIC_RELAXED)
#define STAT_SET(x,v) __atomic_store_n(&stats.x,(v),__ATOMIC_RELAXED)
#define STAT_GET(x) __atomic_load_n(&stats.x,__ATOMIC_RELAXED)
enum {SOURCE_DEFAULT,SOURCE_NVS,SOURCE_AUTO};
void debug_init(void);
void probe(bool);
void record_max(uint32_t *,uint32_t);
void record_latency(bool notify,uint32_t value);

void record_notify_stages(uint32_t queue_us,uint32_t api_us);
