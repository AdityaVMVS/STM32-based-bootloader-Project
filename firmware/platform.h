#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define SLOT_SIZE 0xc0000u
#define HEADER_SIZE 512u
#define SLOT_NONE 255u
#define SLOT_A 0x08040000u
#define SLOT_B 0x08100000u
#define HARDWARE_ID 0x767u

static inline uint32_t slot_base(unsigned slot) { return slot == 0 ? SLOT_A : SLOT_B; }

uint32_t crc32(const void *data, size_t length);
void sha256(const void *data, size_t length, uint8_t digest[32]);
bool same(const void *a, const void *b, size_t size);
bool flash_layout_ok(void);
bool flash_erase(unsigned sector);
bool flash_write(uint32_t address, const void *data, size_t size);
void board_init(void);
bool recovery_button(void);
bool uart_read(uint8_t *byte);
void uart_write(const void *data, size_t size);
void uart_flush(void);
void watchdog_start(void);
void watchdog_feed(void);
_Noreturn void reset(void);
_Noreturn void jump(uint32_t vectors);
#endif
