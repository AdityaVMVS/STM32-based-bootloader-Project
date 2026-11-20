/* Run the target's journal, image validator and hash against emulated NOR flash. */
#include "image.h"
#include "journal.h"

#include <assert.h>
#include <string.h>
#include <sys/mman.h>

#define JOURNAL_A 0x08020000u
#define JOURNAL_B 0x081c0000u
#define FLASH_END 0x08200000u

static uint8_t *at(uint32_t address) { return (uint8_t *)(uintptr_t)address; }
static size_t write_budget = SIZE_MAX;
static unsigned erased;

bool flash_write(uint32_t address, const void *data, size_t size)
{
    const uint8_t *source = data;
    uint8_t *target = at(address);
    for (size_t i = 0; i < size; ++i) {
        if (!write_budget) return false;
        if (write_budget != SIZE_MAX) --write_budget;
        assert((target[i] & source[i]) == source[i]);
        target[i] = source[i];
    }
    return true;
}

bool flash_erase(unsigned sector)
{
    assert(sector == 4 || sector == 11);
    erased = sector;
    memset(at(sector == 4 ? JOURNAL_A : JOURNAL_B), 0xff,
           sector == 4 ? SLOT_A - JOURNAL_A : FLASH_END - JOURNAL_B);
    return true;
}

static void test_hashes(void)
{
    static const uint8_t expected[] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    uint8_t digest[32];
    sha256("abc", 3, digest);
    assert(same(digest, expected, sizeof(expected)));
    assert(crc32("123456789", 9) == 0xcbf43926u);
}

static void test_journal(void)
{
    BootState state = journal_load();
    assert(state.confirmed == SLOT_NONE && state.pending == SLOT_NONE);
    state.confirmed = 0;
    assert(journal_save(&state));

    state.pending = 1;
    write_budget = offsetof(BootState, commit) + 2;
    assert(!journal_save(&state));
    assert(journal_load().pending == SLOT_NONE);
    write_budget = SIZE_MAX;
    assert(journal_save(&state));
    assert(journal_load().pending == 1);

    at(JOURNAL_A + 2 * sizeof(BootState) + offsetof(BootState, checksum))[0] ^= 1;
    assert(journal_load().confirmed == 0);
    assert(journal_load().pending == SLOT_NONE);

    memset(at(JOURNAL_A + 3 * sizeof(BootState)), 0, SLOT_A - JOURNAL_A - 3 * sizeof(BootState));
    state = journal_load();
    state.pending = 1;
    write_budget = offsetof(BootState, commit);
    assert(!journal_save(&state));
    assert(erased == 11 && journal_load().pending == SLOT_NONE);
    write_budget = SIZE_MAX;
    assert(journal_save(&state));
    assert(journal_load().pending == 1);
}

static void test_image(void)
{
    uint8_t *body = at(SLOT_A + HEADER_SIZE);
    memset(body, 0, 520);
    uint32_t vectors[] = {0x20020000, SLOT_A + HEADER_SIZE + 513};
    memcpy(body, vectors, sizeof(vectors));
    uint32_t fields[] = {0x46574d47, (HEADER_SIZE << 16) | 1, 1, HARDWARE_ID, 520, HEADER_SIZE};
    memcpy(at(SLOT_A), fields, sizeof(fields));
    sha256(body, 520, at(SLOT_A + 24));
    uint32_t checksum = crc32(at(SLOT_A), 56);
    memcpy(at(SLOT_A + 56), &checksum, sizeof(checksum));
    assert(image_valid(0));
    memcpy(at(SLOT_B), at(SLOT_A), HEADER_SIZE + 520);
    assert(!image_valid(1));
    body[519] ^= 1;
    assert(!image_valid(0));
}

int main(void)
{
    size_t length = FLASH_END - JOURNAL_A;
    void *flash = mmap(at(JOURNAL_A), length, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(flash == at(JOURNAL_A));
    memset(flash, 0xff, length);
    test_hashes();
    test_journal();
    test_image();
    assert(munmap(flash, length) == 0);
}
