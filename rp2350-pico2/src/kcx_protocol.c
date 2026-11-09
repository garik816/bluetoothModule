#include "kcx_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void kcx_state_init(kcx_state_t *s)
{
    memset(s, 0, sizeof(*s));
    s->pending_name_index = -1;
}

void kcx_scan_clear(kcx_state_t *s)
{
    memset(s->devices, 0, sizeof(s->devices));
    s->device_count = 0;
    s->pending_name_index = -1;
    s->scanning = true;
    s->scan_finished = false;
}

bool kcx_normalize_mac(const char *p, char result[13])
{
    if (!p || !result) return false;
    while (*p == ' ' || *p == '\t') ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    char out[13];
    int n = 0;
    for (; *p && n < 12; ++p) {
        if (*p == ':' || *p == '-' || *p == ' ') continue;
        if (!isxdigit((unsigned char)*p)) return false;
        out[n++] = (char)tolower((unsigned char)*p);
    }
    if (n != 12) return false;
    if (*p && isxdigit((unsigned char)*p)) return false; /* reject 13+ hex chars */
    out[12] = 0;
    memcpy(result, out, 13);
    return true;
}

static void copy_clean(char *dest, size_t capacity, const char *src)
{
    if (!capacity) return;
    if (!src) src = "";
    while (*src == ' ' || *src == '\t' || *src == ':') ++src;
    size_t i = 0;
    while (*src && *src != '\r' && *src != '\n' && i + 1 < capacity)
        dest[i++] = *src++;
    while (i && (dest[i-1] == ' ' || dest[i-1] == '\t')) --i;
    dest[i] = 0;
}

unsigned kcx_parse_line(kcx_state_t *s, const char *line)
{
    if (!s || !line) return KCX_EVENT_NONE;
    while (*line == ' ' || *line == '\t') ++line;
    if (!*line) return KCX_EVENT_NONE;

    if (strncmp(line, "MacAdd:", 7) == 0) {
        char mac[13];
        if (!kcx_normalize_mac(line + 7, mac)) return KCX_EVENT_ERROR;
        size_t i = 0;
        while (i < s->device_count && strcmp(s->devices[i].mac, mac) != 0) ++i;
        if (i == s->device_count) {
            if (i == KCX_MAX_DEVICES) return KCX_EVENT_NONE;
            snprintf(s->devices[i].mac, sizeof(s->devices[i].mac), "%s", mac);
            snprintf(s->devices[i].name, sizeof(s->devices[i].name), "%s", "Unknown device");
            ++s->device_count;
        }
        s->pending_name_index = (int)i;
        s->scanning = true;
        return KCX_EVENT_DEVICE;
    }
    if (strncmp(line, "Name:", 5) == 0 && s->pending_name_index >= 0 &&
        (size_t)s->pending_name_index < s->device_count) {
        copy_clean(s->devices[s->pending_name_index].name,
                   sizeof(s->devices[0].name), line + 5);
        s->pending_name_index = -1;
        return KCX_EVENT_DEVICE;
    }
    if (strncmp(line, "SCAN", 4) == 0 || strncmp(line, "New Devices:", 12) == 0) {
        s->scanning = true;
        return KCX_EVENT_SCAN;
    }
    if (strncmp(line, "ALL Devices", 11) == 0) {
        s->scanning = false;
        s->scan_finished = true;
        s->pending_name_index = -1;
        return KCX_EVENT_SCAN;
    }
    if (strncmp(line, "OK+STATUS:", 10) == 0) {
        bool online = line[10] == '1';
        bool changed = !s->status_known || online != s->connected;
        s->status_known = true;
        s->connected = online;
        return changed ? KCX_EVENT_LINK : KCX_EVENT_NONE;
    }
    if (strncmp(line, "OK+BT_EMITTER", 13) == 0 ||
        strncmp(line, "OK+BT_RECEIVER", 14) == 0) {
        s->mode_known = true;
        s->transmitter = strncmp(line, "OK+BT_EMITTER", 13) == 0;
        return KCX_EVENT_MODE;
    }
    if (strncmp(line, "OK+VERS", 7) == 0) {
        const char *p = strchr(line, ':');
        copy_clean(s->version, sizeof(s->version), p ? p+1 : line+7);
        return KCX_EVENT_VERSION;
    }
    if (strncmp(line, "Delete_Vmlink", 13) == 0) return KCX_EVENT_ERASE_ACK;
    if (strncmp(line, "OK+ADDLINKADD", 13) == 0) return KCX_EVENT_ADD_ACK;
    if (strncmp(line, "CMD ERR!", 8) == 0 || strstr(line, "Not in emitter mode!") ||
        strstr(line, "Addr More than 10") || strstr(line, "Name More than 10")) {
        return KCX_EVENT_ERROR;
    }
    if (strncmp(line, "OK+", 3) == 0) {
        s->ready = true;
        return KCX_EVENT_READY;
    }
    return KCX_EVENT_NONE;
}
