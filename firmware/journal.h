#ifndef JOURNAL_H
#define JOURNAL_H
#include "platform.h"

typedef struct {
    uint32_t generation;
    uint32_t confirmed;
    uint32_t pending;
    uint32_t attempts;
    uint32_t checksum;
    uint32_t commit;
} BootState;

BootState journal_load(void);
bool journal_save(BootState *state);
#endif
