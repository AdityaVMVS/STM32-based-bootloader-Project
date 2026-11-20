#include "journal.h"

#define COMMITTED 0x4a4e4c31u
static const uint32_t base[2] = {0x08020000, 0x081c0000};
static const uint32_t limit[2] = {0x08040000, 0x08200000};
static const unsigned sector[2] = {4, 11};

typedef struct {
    BootState latest;
    unsigned latest_bank;
    uint32_t free[2];
} Scan;

static bool valid_slot(uint32_t slot)
{
    return slot < 2 || slot == SLOT_NONE;
}

static Scan scan(void)
{
    Scan result = {.latest = {.confirmed = SLOT_NONE, .pending = SLOT_NONE}};
    for (unsigned bank = 0; bank < 2; ++bank) {
        for (uint32_t addr = base[bank]; addr <= limit[bank] - sizeof(BootState);
             addr += sizeof(BootState)) {
            const BootState *record = (const BootState *)(uintptr_t)addr;
            if (record->commit == COMMITTED &&
                record->generation > result.latest.generation &&
                valid_slot(record->confirmed) && valid_slot(record->pending) &&
                crc32(record, offsetof(BootState, checksum)) == record->checksum) {
                result.latest = *record;
                result.latest_bank = bank;
            }
            const uint8_t *bytes = (const uint8_t *)record;
            size_t i = 0;
            while (i < sizeof(*record) && bytes[i] == 0xff) ++i;
            if (i != sizeof(*record)) {
                result.free[bank] = 0;
            } else if (!result.free[bank]) {
                result.free[bank] = addr;
            }
        }
    }
    return result;
}

BootState journal_load(void) { return scan().latest; }

bool journal_save(BootState *state)
{
    Scan current = scan();
    if (current.latest.generation == UINT32_MAX) return false;
    unsigned bank = current.latest_bank;
    uint32_t address = current.free[bank];
    if (!address) {
        bank ^= 1u;
        if (!flash_erase(sector[bank])) return false;
        address = base[bank];
    }
    state->generation = current.latest.generation + 1;
    state->checksum = crc32(state, offsetof(BootState, checksum));
    state->commit = COMMITTED;
    return flash_write(address, state, offsetof(BootState, commit)) &&
           flash_write(address + offsetof(BootState, commit),
                       &state->commit, sizeof(state->commit));
}
