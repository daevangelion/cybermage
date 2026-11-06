#include "exe.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}

int main(void)
{
    uint8_t bytes[512] = {0}, output[12];
    cm_file f = {bytes, sizeof bytes};
    cm_exe e;
    int32_t values[2];
    unsigned i;
    bytes[0] = 'M'; bytes[1] = 'Z'; put(bytes + 60, 64);
    bytes[64] = 'L'; bytes[65] = 'E';
    put(bytes + 64 + 20, 3); put(bytes + 64 + 40, 8);
    put(bytes + 64 + 64, 172); put(bytes + 64 + 68, 2);
    put(bytes + 64 + 72, 220); put(bytes + 64 + 128, 400);
    put(bytes + 260, 16); put(bytes + 264, 0x90000);
    put(bytes + 272, 2); put(bytes + 276, 2);
    bytes[286] = 1; bytes[290] = 2; bytes[294] = 3;
    for (i = 0; i < 16; ++i) bytes[408+i] = (uint8_t)i;
    assert(cm_exe_init(&e, &f, "fixture") == 0);
    assert(cm_exe_table(&e, 0x90004, values, 2) == 0);
    assert(values[0] == 0x07060504 && values[1] == 0x0b0a0908);
    assert(cm_exe_table(&e, 0x90006, values, 2) == 0);
    assert(values[0] == 0x09080706 && values[1] == 0x0d0c0b0a);
    assert(cm_exe_bytes(&e, 0x90003, output, 12) == 0);
    for (i = 0; i < 12; ++i) assert(output[i] == i + 3);
    put(bytes + 408, UINT32_MAX);
    assert(cm_exe_table(&e, 0x90000, values, 1) == 0 && values[0] == -1);
    assert(cm_exe_table(&e, 0x8ffff, values, 1) == -1);
    assert(cm_exe_table(&e, 0x9000e, values, 1) == -1);
    assert(cm_exe_table(&e, 0x90000, values, UINT32_MAX) == -1);
    bytes[290] = 0;
    assert(cm_exe_table(&e, 0x90000, values, 1) == -1);
    bytes[290] = 2; bytes[291] = 1;
    assert(cm_exe_table(&e, 0x90000, values, 1) == -1);
    bytes[291] = 0; put(bytes + 272, 0);
    assert(cm_exe_init(&e, &f, "fixture") == -1);
    f.size = 80;
    assert(cm_exe_init(&e, &f, "fixture") == -1);
    puts("exe: numeric and raw tables, cross-page words, signed values and malformed bounds passed");
    return 0;
}
