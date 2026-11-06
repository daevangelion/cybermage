#include "controls.h"

int controls_load(cm_controls *c, const char *path)
{
    cm_file file = {0};
    int exists = platform_file_exists(path), result;
    if (exists < 0) return -1;
    if (!exists) return 0;
    if (platform_file_read(&file, path)) return -1;
    result = controls_parse(c, file.data, file.size);
    platform_file_release(&file);
    return result;
}
