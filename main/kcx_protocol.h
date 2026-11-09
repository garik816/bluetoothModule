#pragma once
/* KCX_BT_EMITTER v1.7 UART response parser (no ESP-IDF dependencies).
 * Based on publicly observed KCX 1.7 responses: MacAdd, Name, OK+STATUS etc.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KCX_MAX_DEVICES 24
#define KCX_MAC_LENGTH 12
#define KCX_DEVICE_NAME_LENGTH 56

typedef struct {
    char mac[KCX_MAC_LENGTH + 1];  /* 12 hex digits, lower case, no 0x */
    char name[KCX_DEVICE_NAME_LENGTH];
} kcx_device_t;

typedef struct {
    kcx_device_t devices[KCX_MAX_DEVICES];
    size_t device_count;
    int pending_name_index;
    bool connected;
    bool status_known;
    bool mode_known;
    bool transmitter;
    bool ready;
    bool scanning;
    bool scan_finished;
    char version[48];
} kcx_state_t;

enum {
    KCX_EVENT_NONE        = 0,
    KCX_EVENT_DEVICE      = 1u << 0,
    KCX_EVENT_LINK        = 1u << 1,
    KCX_EVENT_READY       = 1u << 2,
    KCX_EVENT_SCAN        = 1u << 3,
    KCX_EVENT_ERASE_ACK   = 1u << 4,
    KCX_EVENT_ADD_ACK     = 1u << 5,
    KCX_EVENT_ERROR       = 1u << 6,
    KCX_EVENT_MODE        = 1u << 7,
    KCX_EVENT_VERSION     = 1u << 8,
};

void kcx_state_init(kcx_state_t *state);
void kcx_scan_clear(kcx_state_t *state);
bool kcx_normalize_mac(const char *text, char result[13]);
unsigned kcx_parse_line(kcx_state_t *state, const char *line);
