#include "cm.h"
static cm_error last_error;
int cm_fail(const char *file, uint32_t offset, const char *message)
{
    last_error.file = file; last_error.offset = offset; last_error.message = message;
    return -1;
}
const cm_error *cm_last_error(void) { return &last_error; }
void cm_media_clear_error(void)
{
    last_error.file = NULL; last_error.offset = 0; last_error.message = NULL;
}
