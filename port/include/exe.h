#ifndef CM_EXE_H
#define CM_EXE_H

#include "platform.h"

typedef struct {
    const uint8_t *data;
    const char *path;
    uint32_t size, base, virtual_size;
    uint32_t page_size, page_map, first_page, page_count, total_pages, data_pages;
} cm_exe;

/* Borrow the supplied LE executable. Numeric data only: no relocated pointers. */
int cm_exe_init(cm_exe *e, const cm_file *file, const char *path);
int cm_exe_table(const cm_exe *e, uint32_t link, int32_t *dst, unsigned count);
int cm_exe_bytes(const cm_exe *e, uint32_t link, uint8_t *dst, uint32_t size);

#endif
