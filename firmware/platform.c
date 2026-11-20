#include "platform.h"

#define RCC 0x40023800u
#define FLASH 0x40023c00u
#define USART 0x40004800u
#define GPIOD 0x40020c00u
#define GPIOC 0x40020800u
#define IWDG 0x40003000u
#define FLASH_SR REG(FLASH + 12)
#define FLASH_CR REG(FLASH + 16)
#define FLASH_ERRORS 0xf2u
#define FLASH_BUSY (1u << 16)

bool flash_layout_ok(void)
{
    return (REG(FLASH + 20) & (1u << 29)) != 0;
}

static bool unlock(void)
{
    if (FLASH_CR & (1u << 31)) {
        REG(FLASH + 4) = 0x45670123u;
        REG(FLASH + 4) = 0xcdef89abu;
    }
    FLASH_SR = FLASH_ERRORS | 1u;
    return (FLASH_CR & (1u << 31)) == 0;
}

static bool wait_flash(void)
{
    /* In single-bank mode the CPU can stall fetching instructions during BSY. */
    for (uint32_t remaining = 16000000; remaining; --remaining) {
        if (!(FLASH_SR & FLASH_BUSY)) {
            return (FLASH_SR & FLASH_ERRORS) == 0;
        }
    }
    return false;
}

bool flash_erase(unsigned sector)
{
    if (sector < 4 || sector > 11 || !flash_layout_ok() || !unlock()) {
        return false;
    }
    FLASH_CR = (1u << 1) | (sector << 3) | (2u << 8);
    FLASH_CR |= 1u << 16;
    bool ok = wait_flash();
    FLASH_CR = 1u << 31;
    return ok;
}

bool flash_write(uint32_t address, const void *data, size_t size)
{
    if (address < 0x08020000u || address >= 0x08200000u ||
        size > 0x08200000u - address || !flash_layout_ok() || !unlock()) {
        return false;
    }
    const uint8_t *source = data;
    bool ok = true;
    FLASH_CR = 1u; /* Byte programming allows an unaligned final chunk. */
    for (size_t i = 0; i < size && ok; ++i) {
        volatile uint8_t *target = (volatile uint8_t *)(uintptr_t)(address + i);
        if (source[i] != 0xff) {
            *target = source[i];
            ok = wait_flash();
        }
        ok = ok && *target == source[i];
    }
    FLASH_CR = 1u << 31;
    return ok;
}

void board_init(void)
{
    /* Reset clock tree: HSI at 16 MHz, APB1 undivided. Caches stay off. */
    REG(RCC + 0x30) |= (1u << 2) | (1u << 3);
    REG(RCC + 0x40) |= 1u << 18;
    (void)REG(RCC + 0x40);
    REG(GPIOC + 0x00) &= ~(3u << 26);
    REG(GPIOC + 0x0c) = (REG(GPIOC + 0x0c) & ~(3u << 26)) | (2u << 26);
    REG(GPIOD + 0x00) = (REG(GPIOD + 0x00) & ~(15u << 16)) | (10u << 16);
    REG(GPIOD + 0x24) = (REG(GPIOD + 0x24) & ~255u) | 0x77u;
    REG(USART + 12) = 139;
    REG(USART) = 13;
}

bool recovery_button(void) { return (REG(GPIOC + 0x10) & (1u << 13)) != 0; }

bool uart_read(uint8_t *byte)
{
    if (!(REG(USART + 0x1c) & (1u << 5))) {
        REG(USART + 0x20) = 15; /* Clear line/overrun flags. */
        return false;
    }
    *byte = (uint8_t)REG(USART + 0x24);
    return true;
}

void uart_write(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        while (!(REG(USART + 0x1c) & (1u << 7))) {}
        REG(USART + 0x28) = bytes[i];
    }
}

void uart_flush(void) { while (!(REG(USART + 0x1c) & (1u << 6))) {} }
void watchdog_feed(void) { REG(IWDG) = 0xaaaau; }

void watchdog_start(void)
{
    REG(IWDG) = 0xccccu;
    REG(IWDG) = 0x5555u;
    REG(IWDG + 4) = 6u;
    REG(IWDG + 8) = 1500u;
    while (REG(IWDG + 12) & 3u) {}
    watchdog_feed();
}

_Noreturn void reset(void)
{
    REG(0xe000ed0c) = 0x05fa0004u;
    __asm volatile("dsb");
    for (;;) {}
}

_Noreturn void jump(uint32_t vectors)
{
    __asm volatile("cpsid i");
    REG(USART) = 0;
    REG(0xe000e010) = 0;
    REG(0xe000ed04) = (1u << 25) | (1u << 27);
    for (unsigned bank = 0; bank < 4; ++bank) {
        REG(0xe000e180u + 4 * bank) = UINT32_MAX;
        REG(0xe000e280u + 4 * bank) = UINT32_MAX;
    }
    REG(0xe000ed08) = vectors;
    uint32_t stack = REG(vectors);
    uint32_t entry = REG(vectors + 4);
    __asm volatile("dsb\n isb\n msr msp, %0\n bx %1" :: "r"(stack), "r"(entry) : "memory");
    __builtin_unreachable();
}
