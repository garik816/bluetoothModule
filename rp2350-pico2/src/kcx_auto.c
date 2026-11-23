#include "kcx_auto.h"
#include <stddef.h>
static bool reached(uint32_t now,uint32_t deadline)
{
    return (int32_t)(now-deadline)>=0; /* wrap-safe for delays < 2^31 ms */
}
void kcx_auto_init(kcx_auto_t *a,uint32_t now)
{
    if(!a)return;
    *a=(kcx_auto_t){.enabled=true,.next_attempt_ms=now+KCX_AUTO_BOOT_GRACE_MS};
}
void kcx_auto_on_kcx_boot(kcx_auto_t *a,uint32_t now)
{
    if(!a)return;
    a->was_connected=false;
    a->next_attempt_ms=now+KCX_AUTO_BOOT_GRACE_MS;
}
void kcx_auto_on_link(kcx_auto_t *a,uint32_t now,bool connected)
{
    if(!a)return;
    if(connected)a->was_connected=true;
    else if(a->was_connected){
        a->was_connected=false;
        a->next_attempt_ms=now+KCX_AUTO_BOOT_GRACE_MS;
    }
}
void kcx_auto_postpone(kcx_auto_t *a,uint32_t now,uint32_t duration_ms)
{
    if(a)a->next_attempt_ms=now+duration_ms;
}
void kcx_auto_pause(kcx_auto_t *a)
{
    if(a)a->paused=true;
}
void kcx_auto_resume(kcx_auto_t *a,uint32_t now)
{
    if(!a)return;
    a->enabled=true;
    a->paused=false;
    a->next_attempt_ms=now+KCX_AUTO_RESUME_MS;
}
void kcx_auto_set_enabled(kcx_auto_t *a,bool enabled,uint32_t now)
{
    if(!a)return;
    a->enabled=enabled;
    if(enabled){
        a->paused=false;
        a->next_attempt_ms=now+KCX_AUTO_RESUME_MS;
    }
}
void kcx_auto_mark_attempt(kcx_auto_t *a,uint32_t now)
{
    if(!a)return;
    ++a->attempts;
    a->next_attempt_ms=now+KCX_AUTO_RETRY_MS;
}
bool kcx_auto_should_retry(kcx_auto_t *a,uint32_t now,
                           bool last_mac_known,bool kcx_ready,bool status_known,
                           bool connected,bool mode_known,bool transmitter,
                           bool manual_busy,bool scanning)
{
    if(!a || !a->enabled || a->paused || connected || !last_mac_known ||
       !kcx_ready || !status_known || (mode_known && !transmitter) ||
       manual_busy || scanning || !reached(now,a->next_attempt_ms))
        return false;
    kcx_auto_mark_attempt(a,now);
    return true;
}
