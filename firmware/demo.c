#include "journal.h"

#ifndef APP_SLOT
#error Set APP_SLOT to 0 or 1
#endif

#ifndef DEMO_FAIL
#define DEMO_FAIL 0
#endif
#define GPIOB 0x40020400u

int main(void)
{
    const unsigned pin = APP_SLOT == 0 ? 0 : 14;
    REG(0x40023830u) |= 1u << 1;
    (void)REG(0x40023830u);
    REG(GPIOB) = (REG(GPIOB) & ~(3u << (2 * pin))) | (1u << (2 * pin));
    REG(GPIOB + 0x18) = 1u << pin;
    BootState state = journal_load();
    if (state.pending == APP_SLOT) {
#if DEMO_FAIL
        for (;;) {} /* Trial watchdog triggers the next boot attempt. */
#else
        state.confirmed = APP_SLOT;
        state.pending = SLOT_NONE;
        state.attempts = 0;
        if (!journal_save(&state)) reset();
#endif
    }
    for (;;) watchdog_feed();
}
