#pragma once

#include <stddef.h>

typedef struct {
    const char* name;
    void*       ptr;
    int         is_input;
} nwocg_ExtPort;
