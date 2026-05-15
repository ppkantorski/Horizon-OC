#pragma once
#include <switch.h>

#define R_UNLESS(rc)        \
    do {                    \
        if (R_FAILED(rc)) { \
            return;         \
        }                   \
    } while (0)

/* TODO: Add more Result macros. */
