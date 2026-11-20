#include "image.h"

typedef struct {
    uint32_t magic;
    uint16_t format;
    uint16_t header_size;
    uint32_t version;
    uint32_t hardware;
    uint32_t length;
    uint32_t vector_offset;
    uint8_t digest[32];
    uint32_t checksum;
    uint8_t reserved[452];
} ImageHeader;

_Static_assert(sizeof(ImageHeader) == HEADER_SIZE, "image format size");

bool image_valid(unsigned slot)
{
    if (slot >= 2) return false;
    uint32_t start = slot_base(slot) + HEADER_SIZE;
    const ImageHeader *header = (const ImageHeader *)(uintptr_t)slot_base(slot);
    if (header->magic != 0x46574d47u || header->format != 1 ||
        header->header_size != HEADER_SIZE || header->hardware != HARDWARE_ID ||
        header->vector_offset != HEADER_SIZE || header->length < 512 ||
        header->length > SLOT_SIZE - HEADER_SIZE ||
        crc32(header, offsetof(ImageHeader, checksum)) != header->checksum) {
        return false;
    }
    uint32_t stack = REG(start);
    uint32_t entry = REG(start + 4);
    if (stack <= 0x20000000u || stack > 0x20080000u || (stack & 7) ||
        !(entry & 1) || (entry & ~1u) < start + 512 ||
        (entry & ~1u) >= start + header->length) {
        return false;
    }
    uint8_t digest[32];
    sha256((const void *)(uintptr_t)start, header->length, digest);
    return same(digest, header->digest, sizeof(digest));
}
