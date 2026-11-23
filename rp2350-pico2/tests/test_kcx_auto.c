/* Deterministic host-side tests for KCX v1.7 reconnection policy. */
#include "../src/kcx_auto.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    kcx_auto_t a;
    kcx_auto_init(&a,0u);
    assert(a.enabled && !a.paused);
    /* Unknown previous device -> don't force a generic pairing. */
    assert(!kcx_auto_should_retry(&a,60000,false,true,true,false,true,true,false,false));
    assert(!kcx_auto_should_retry(&a,60000,true,false,true,false,true,true,false,false));
    assert(!kcx_auto_should_retry(&a,60000,true,true,false,false,true,true,false,false));
    assert(!kcx_auto_should_retry(&a,59999,true,true,true,false,true,true,false,false));
    assert(kcx_auto_should_retry(&a,60000,true,true,true,false,true,true,false,false));
    assert(a.attempts==1u);
    assert(!kcx_auto_should_retry(&a,149999,true,true,true,false,true,true,false,false));
    assert(kcx_auto_should_retry(&a,150000,true,true,true,false,true,true,false,false));
    assert(a.attempts==2u);
    /* Connected device is never reset. */
    kcx_auto_on_link(&a,150100,true);
    assert(!kcx_auto_should_retry(&a,500000,true,true,true,true,true,true,false,false));
    kcx_auto_on_link(&a,500000,false);
    assert(!kcx_auto_should_retry(&a,559999,true,true,true,false,true,true,false,false));
    assert(!kcx_auto_should_retry(&a,560000,true,true,true,false,true,false,false,false));
    assert(!kcx_auto_should_retry(&a,560000,true,true,true,false,true,true,true,false));
    assert(!kcx_auto_should_retry(&a,560000,true,true,true,false,true,true,false,true));
    assert(kcx_auto_should_retry(&a,560000,true,true,true,false,true,true,false,false));
    kcx_auto_pause(&a);
    assert(!kcx_auto_should_retry(&a,1000000,true,true,true,false,true,true,false,false));
    kcx_auto_resume(&a,1000000);
    assert(!kcx_auto_should_retry(&a,1009999,true,true,true,false,true,true,false,false));
    assert(kcx_auto_should_retry(&a,1010000,true,true,true,false,true,true,false,false));
    kcx_auto_set_enabled(&a,false,2000000);
    assert(!kcx_auto_should_retry(&a,2200000,true,true,true,false,true,true,false,false));
    kcx_auto_set_enabled(&a,true,2200000);
    assert(kcx_auto_should_retry(&a,2210000,true,true,true,false,true,true,false,false));
    kcx_auto_on_kcx_boot(&a,2210000);
    assert(!kcx_auto_should_retry(&a,2269999,true,true,true,false,true,true,false,false));
    assert(kcx_auto_should_retry(&a,2270000,true,true,true,false,true,true,false,false));
    kcx_auto_init(&a,0xffffff00u);
    assert(!kcx_auto_should_retry(&a,0x0000e95fu,true,true,true,false,true,true,false,false));
    assert(kcx_auto_should_retry(&a,0x0000e960u,true,true,true,false,true,true,false,false));
    puts("KCX v1.7 Pico 2 auto reconnect tests: PASS");
    return 0;
}
