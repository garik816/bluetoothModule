#pragma once
/*
 * KCX_BT_EMITTER v1.7 native auto reconnect recovery.
 * Automatic reconnection NEVER uses AT+PAIR, AT+DELVMLINK or writes KCX NVM.
 * It uses AT+RESET only when link is down and Last_Add is known.
 */
#include <stdbool.h>
#include <stdint.h>
#define KCX_AUTO_BOOT_GRACE_MS 60000u
#define KCX_AUTO_RETRY_MS      90000u
#define KCX_AUTO_RESUME_MS     10000u
typedef struct {
    bool enabled;
    bool paused;                /* user deliberately disconnected */
    bool was_connected;
    uint32_t next_attempt_ms;
    uint32_t attempts;
} kcx_auto_t;
void kcx_auto_init(kcx_auto_t *a,uint32_t now);
void kcx_auto_on_kcx_boot(kcx_auto_t *a,uint32_t now);
void kcx_auto_on_link(kcx_auto_t *a,uint32_t now,bool connected);
void kcx_auto_postpone(kcx_auto_t *a,uint32_t now,uint32_t duration_ms);
void kcx_auto_pause(kcx_auto_t *a);
void kcx_auto_resume(kcx_auto_t *a,uint32_t now);
void kcx_auto_set_enabled(kcx_auto_t *a,bool enabled,uint32_t now);
void kcx_auto_mark_attempt(kcx_auto_t *a,uint32_t now);
bool kcx_auto_should_retry(kcx_auto_t *a,uint32_t now,
                           bool last_mac_known,bool kcx_ready,bool status_known,
                           bool connected,bool mode_known,bool transmitter,
                           bool manual_busy,bool scanning);
