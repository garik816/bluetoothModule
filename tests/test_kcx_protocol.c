#include "../main/kcx_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    kcx_state_t s;
    kcx_state_init(&s);
    assert(kcx_parse_line(&s, "OK+") & KCX_EVENT_READY);
    assert(kcx_parse_line(&s, "OK+VERS:KCX_BT_RTX_V1.7") & KCX_EVENT_VERSION);
    assert(strcmp(s.version, "KCX_BT_RTX_V1.7") == 0);
    assert(kcx_parse_line(&s, "OK+BT_EMITTER") & KCX_EVENT_MODE);
    assert(s.transmitter);
    assert(kcx_parse_line(&s, "OK+STATUS:0") & KCX_EVENT_LINK);
    assert(!s.connected);
    kcx_scan_clear(&s);
    assert(kcx_parse_line(&s, "SCAN") & KCX_EVENT_SCAN);
    assert(kcx_parse_line(&s, "New Devices:1") & KCX_EVENT_SCAN);
    assert(kcx_parse_line(&s, "MacAdd:0xF44EFDECD39D") & KCX_EVENT_DEVICE);
    assert(kcx_parse_line(&s, "Name:Headphones XYZ") & KCX_EVENT_DEVICE);
    assert(kcx_parse_line(&s, "MacAdd:0xf44efdecd39d") & KCX_EVENT_DEVICE);
    assert(s.device_count == 1);
    assert(strcmp(s.devices[0].mac, "f44efdecd39d") == 0);
    assert(strcmp(s.devices[0].name, "Headphones XYZ") == 0);
    assert(kcx_parse_line(&s, "MacAdd:0x123456789ABC") & KCX_EVENT_DEVICE);
    assert(kcx_parse_line(&s, "Name:Speaker") & KCX_EVENT_DEVICE);
    assert(kcx_parse_line(&s, "ALL Devices=2") & KCX_EVENT_SCAN);
    assert(s.scan_finished && s.device_count == 2);
    assert(kcx_parse_line(&s, "Delete_Vmlink") & KCX_EVENT_ERASE_ACK);
    assert(kcx_parse_line(&s, "OK+ADDLINKADD=123456789abc") & KCX_EVENT_ADD_ACK);
    assert(kcx_parse_line(&s, "OK+STATUS:1") & KCX_EVENT_LINK);
    assert(s.connected);
    assert(!(kcx_parse_line(&s, "OK+STATUS:1") & KCX_EVENT_LINK));
    char mac[13];
    assert(kcx_normalize_mac("AA:BB:CC:DD:EE:FF", mac));
    assert(strcmp(mac, "aabbccddeeff") == 0);
    assert(!kcx_normalize_mac("xxyyzzwwwwww", mac));
    assert(!kcx_normalize_mac("123456789abc1", mac));
    assert(kcx_parse_line(&s, "CMD ERR!") & KCX_EVENT_ERROR);
    assert(kcx_parse_line(&s, "Name:orphan") == KCX_EVENT_NONE);
    puts("KCX v1.7 parser tests: PASS");
    return 0;
}
