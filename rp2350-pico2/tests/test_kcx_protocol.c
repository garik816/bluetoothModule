/* Regression tests: actual KCX_BT_EMITTER v1.7 UART lines (Pico 2). */
#include "../src/kcx_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    kcx_state_t s;
    kcx_state_init(&s);
    assert(s.device_count==0 && !s.connected && s.pending_name_index==-1);

    assert(kcx_parse_line(&s,"POWER ON")==KCX_EVENT_NONE);
    assert(kcx_parse_line(&s,"SCAN....")&KCX_EVENT_SCAN);
    assert(kcx_parse_line(&s,"MacAdd:")==KCX_EVENT_NONE);
    assert(s.device_count==0);
    unsigned event=kcx_parse_line(&s,"MacAdd:6e73594c1838,Name:WH-1000XM3");
    assert(event&KCX_EVENT_DEVICE);
    assert(!(event&KCX_EVENT_ERROR));
    assert(s.device_count==1);
    assert(strcmp(s.devices[0].mac,"6e73594c1838")==0);
    assert(strcmp(s.devices[0].name,"WH-1000XM3")==0);

    assert(kcx_parse_line(&s,"CON ONE")==KCX_EVENT_NONE);
    event=kcx_parse_line(&s,"CONNECT=>MacAdd:6e73594c1838,Name:WH-1000XM3");
    assert(event&KCX_EVENT_DEVICE);
    assert(event&KCX_EVENT_LINK);
    assert(s.device_count==1); /* do not duplicate same address */
    assert(s.connected && s.status_known);
    assert(strcmp(s.connected_mac,"6e73594c1838")==0);
    assert(strcmp(s.connected_name,"WH-1000XM3")==0);
    assert(!(kcx_parse_line(&s,"OK+STATUS:1")&KCX_EVENT_LINK));
    assert(kcx_parse_line(&s,"OK+STATUS:0")&KCX_EVENT_LINK);
    assert(!s.connected && s.connected_mac[0]=='\0');
    assert(strcmp(s.last_mac,"6e73594c1838")==0);
    assert(kcx_parse_line(&s,"OK+VMLINK")&KCX_EVENT_READY);
    assert(kcx_parse_line(&s,"Last_Add=0xDEADBEEF0001")&KCX_EVENT_SAVED);
    assert(strcmp(s.last_mac,"deadbeef0001")==0);
    assert(kcx_parse_line(&s,"Last_Add=0xDEADBEEF0001")==KCX_EVENT_NONE);
    assert(kcx_parse_line(&s,"Last_Add=invalid")==KCX_EVENT_NONE);
    assert(strcmp(s.last_mac,"deadbeef0001")==0);
    assert(kcx_parse_line(&s,"VM_MacAdd0=0xaaaaaaaaaaaa")==KCX_EVENT_NONE);
    assert(strcmp(s.last_mac,"deadbeef0001")==0);

    kcx_scan_clear(&s);
    assert(kcx_parse_line(&s,"MacAdd:0xF44EFDECD39D")&KCX_EVENT_DEVICE);
    assert(kcx_parse_line(&s,"Name:Headphones XYZ")&KCX_EVENT_DEVICE);
    assert(kcx_parse_line(&s,"MacAdd:0xf44efdecd39d")&KCX_EVENT_DEVICE);
    assert(s.device_count==1 && strcmp(s.devices[0].name,"Headphones XYZ")==0);
    assert(kcx_parse_line(&s,"MacAdd:0x123456789ABC,Name:Speaker")&KCX_EVENT_DEVICE);
    assert(s.device_count==2);
    assert(strcmp(s.devices[1].name,"Speaker")==0);
    assert(kcx_parse_line(&s,"ALL Devices=2")&KCX_EVENT_SCAN);
    assert(s.scan_finished);
    assert(kcx_parse_line(&s,"OK+BT_EMITTER")&KCX_EVENT_MODE);
    assert(s.transmitter);
    assert(kcx_parse_line(&s,"OK+VERS:KCX_BT_RTX_V1.7")&KCX_EVENT_VERSION);
    assert(strcmp(s.version,"KCX_BT_RTX_V1.7")==0);
    assert(kcx_parse_line(&s,"Delete_Vmlink")&KCX_EVENT_ERASE_ACK);
    assert(s.last_mac[0]=='\0');
    assert(kcx_parse_line(&s,"OK+ADDLINKADD=123456789abc")&KCX_EVENT_ADD_ACK);

    assert(kcx_parse_line(&s,"CMD ERR!")&KCX_EVENT_ERROR);
    assert(kcx_parse_line(&s,"Name:orphan")==KCX_EVENT_NONE);
    assert(kcx_parse_line(&s,"MacAdd:0x123456789abc9")==KCX_EVENT_NONE);
    assert(kcx_parse_line(&s,"MacAdd:,Name:") == KCX_EVENT_NONE);

    char normalized[13];
    assert(kcx_normalize_mac("AA:BB:CC:DD:EE:FF",normalized));
    assert(strcmp(normalized,"aabbccddeeff")==0);
    assert(!kcx_normalize_mac("123456789abc1",normalized));
    assert(!kcx_normalize_mac("xxyyzzwwwwww",normalized));

    puts("KCX v1.7 Pico 2 real-device parser tests: PASS");
    return 0;
}
