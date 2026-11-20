#include "image.h"
#include "journal.h"

enum { HELLO = 1, GET_INFO, BEGIN_UPDATE, ERASE_SLOT, WRITE_CHUNK,
       GET_PROGRESS, VERIFY_IMAGE, ACTIVATE_IMAGE, REBOOT, ABORT_UPDATE, GET_LOG };
enum { OK, BAD_ARGUMENT, BAD_STATE, FLASH_ERROR, INVALID_IMAGE, BAD_SEQUENCE };

static BootState boot;
static struct {
    unsigned slot;
    uint32_t size;
    uint32_t written;
    bool erased;
    bool verified;
} transfer = {.slot = SLOT_NONE};
static uint8_t request[522], previous[522];
static uint8_t reply[12];
static size_t previous_size, reply_size;
static uint16_t sequence;

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void write32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static unsigned handle(unsigned command, const uint8_t *data, size_t size)
{
    if (command != BEGIN_UPDATE && command != WRITE_CHUNK && size) return BAD_ARGUMENT;
    switch (command) {
    case HELLO:
        reply[1] = 1;
        reply_size = 2;
        return OK;
    case GET_INFO:
        reply[1] = (uint8_t)boot.confirmed;
        reply[2] = (uint8_t)boot.pending;
        reply[3] = flash_layout_ok();
        write32(reply + 4, HARDWARE_ID);
        reply_size = 8;
        return OK;
    case BEGIN_UPDATE:
        if (size != 5 || data[0] > 1) return BAD_ARGUMENT;
        if (!flash_layout_ok() || data[0] == boot.confirmed || boot.pending != SLOT_NONE) {
            return BAD_STATE;
        }
        uint32_t length = read32(data + 1);
        if (length < HEADER_SIZE + 512 || length > SLOT_SIZE) return BAD_ARGUMENT;
        transfer.slot = data[0];
        transfer.size = length;
        transfer.written = 0;
        transfer.erased = false;
        transfer.verified = false;
        return OK;
    case ERASE_SLOT:
        if (transfer.slot == SLOT_NONE || transfer.erased) return BAD_STATE;
        for (unsigned i = 0; i < 3; ++i) {
            if (!flash_erase(5 + 3 * transfer.slot + i)) return FLASH_ERROR;
        }
        transfer.erased = true;
        return OK;
    case WRITE_CHUNK:
        if (size <= 4 || size > 260) return BAD_ARGUMENT;
        if (!transfer.erased || transfer.verified || read32(data) != transfer.written ||
            size - 4 > transfer.size - transfer.written) return BAD_STATE;
        if (!flash_write(slot_base(transfer.slot) + transfer.written, data + 4, size - 4)) {
            transfer.erased = false;
            return FLASH_ERROR;
        }
        transfer.written += (uint32_t)(size - 4);
        return OK;
    case GET_PROGRESS:
        write32(reply + 1, transfer.written);
        reply_size = 5;
        return OK;
    case VERIFY_IMAGE:
        if (!transfer.erased || transfer.written != transfer.size) return BAD_STATE;
        transfer.verified = REG(slot_base(transfer.slot) + 16) == transfer.size - HEADER_SIZE &&
                            image_valid(transfer.slot);
        return transfer.verified ? OK : INVALID_IMAGE;
    case ACTIVATE_IMAGE: {
        if (!transfer.verified || boot.pending != SLOT_NONE) return BAD_STATE;
        BootState next = boot;
        next.pending = transfer.slot;
        next.attempts = 0;
        if (!journal_save(&next)) return FLASH_ERROR;
        boot = next;
        transfer.slot = SLOT_NONE;
        transfer.erased = false;
        transfer.verified = false;
        return OK;
    }
    case REBOOT:
        return OK;
    case ABORT_UPDATE:
        transfer.slot = SLOT_NONE;
        transfer.erased = false;
        transfer.verified = false;
        transfer.written = 0;
        return OK;
    case GET_LOG:
        reply[1] = (uint8_t)boot.attempts;
        write32(reply + 2, boot.generation);
        reply_size = 6;
        return OK;
    default:
        return BAD_ARGUMENT;
    }
}

static void send_response(unsigned command, uint16_t seq, const uint8_t *data, size_t size)
{
    uint8_t outgoing[24] = {0xa5, 0x5a, 1, (uint8_t)(command | 0x80),
                            (uint8_t)seq, (uint8_t)(seq >> 8), (uint8_t)size, 0};
    for (size_t i = 0; i < size; ++i) outgoing[8 + i] = data[i];
    write32(outgoing + 8 + size, crc32(outgoing + 2, 6 + size));
    uart_write(outgoing, 12 + size);
}

static void process(size_t count)
{
    unsigned command = request[1];
    uint16_t seq = (uint16_t)(request[2] | request[3] << 8);
    if (request[0] != 1 || command >= 128) return;
    if (previous_size && seq == sequence && previous_size == count &&
        same(previous, request, count)) {
        send_response(command, seq, reply, reply_size);
        return;
    }
    bool hello = command == HELLO && count == 10;
    if (!hello && (!previous_size || seq != (uint16_t)(sequence + 1))) {
        uint8_t status = BAD_SEQUENCE;
        send_response(command, seq, &status, 1);
        return;
    }
    reply_size = 1;
    reply[0] = (uint8_t)handle(command, request + 6, count - 10);
    sequence = seq;
    previous_size = count;
    for (size_t i = 0; i < count; ++i) previous[i] = request[i];
    send_response(command, seq, reply, reply_size);
    if (command == REBOOT && reply[0] == OK) {
        uart_flush();
        reset();
    }
}

static void recover(void)
{
    size_t count = 0, needed = 0;
    unsigned sync = 0;
    /* DWT CYCCNT times out a truncated frame after 100 ms of silence. */
    REG(0xe000edfc) |= 1u << 24;
    REG(0xe0001fb0) = 0xc5acce55u;
    REG(0xe0001000) |= 1;
    uint32_t last = REG(0xe0001004);
    for (;;) {
        uint32_t now = REG(0xe0001004);
        if ((uint32_t)(now - last) > 1600000u) {
            count = 0;
            sync = 0;
        }
        uint8_t byte;
        if (!uart_read(&byte)) continue;
        last = now;
        if (sync != 2) {
            sync = sync == 1 && byte == 0x5a ? 2 : (byte == 0xa5 ? 1 : 0);
            continue;
        }
        request[count++] = byte;
        if (count == 6) {
            needed = 10u + request[4] + ((size_t)request[5] << 8);
            if (needed > sizeof(request)) {
                sync = 0;
                count = 0;
            }
        }
        if (count >= 10 && count == needed) {
            if (crc32(request, count - 4) == read32(request + count - 4)) process(count);
            sync = 0;
            count = 0;
        }
    }
}

int main(void)
{
    board_init();
    if (flash_layout_ok()) {
        boot = journal_load();
        if (!recovery_button()) {
            if (boot.pending < 2) {
                if (boot.attempts < 3 && image_valid(boot.pending)) {
                    ++boot.attempts;
                    if (journal_save(&boot)) {
                        watchdog_start();
                        jump(slot_base(boot.pending) + HEADER_SIZE);
                    }
                }
                boot.pending = SLOT_NONE;
                boot.attempts = 0;
                if (!journal_save(&boot)) {
                    boot = journal_load();
                    recover();
                }
            }
            if (image_valid(boot.confirmed)) jump(slot_base(boot.confirmed) + HEADER_SIZE);
        }
    } else {
        boot.confirmed = SLOT_NONE;
        boot.pending = SLOT_NONE;
    }
    recover();
    return 0;
}
