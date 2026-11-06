#ifndef CM_RSC_H
#define CM_RSC_H

#include "platform.h"

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t capacity;
    uint32_t count;
    const char *path;
    cm_file file;
} rsc;

int rsc_open(rsc *r, const char *path);
/* Borrowed bytes: remain valid until caller releases its buffer. */
int rsc_parse(rsc *r, const uint8_t *data, uint32_t size, const char *path);
int rsc_entry(rsc *r, unsigned idx, const uint8_t **p, uint32_t *len);
void rsc_close(rsc *r);

#endif
