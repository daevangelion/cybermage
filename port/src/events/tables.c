#include "events.h"
#include "exe.h"
#include <string.h>

int events_load_tables(events_state *e, const char *asset_dir)
{
    cm_file file={0};
    cm_exe exe;
    char path[CM_WORLD_PATH_MAX];
    size_t n;
    int result;
    if(!e || !asset_dir) return cm_fail("events/tables",0,"missing asset directory");
    n=strlen(asset_dir);
    if(n>sizeof(path)-sizeof("/CM_HI.EXE")) return cm_fail("events/tables",0,"asset path too long");
    memcpy(path,asset_dir,n);
    if(n && path[n-1]!='/') path[n++]='/';
    memcpy(path+n,"CM_HI.EXE",sizeof("CM_HI.EXE"));
    if(platform_file_read(&file,path)) return -1;
    result=cm_exe_init(&exe,&file,path);
    if(!result) result=cm_exe_table(&exe,0x92710,e->video_list,9) ||
        cm_exe_table(&exe,0x92734,e->video_frames,5) ||
        cm_exe_table(&exe,0x92748,e->video_fxo,5) ||
        cm_exe_table(&exe,0x9275C,e->video_time,20) ||
        cm_exe_table(&exe,0x927AC,e->video_text,8) ||
        cm_exe_table(&exe,0x927CC,e->video_seq,70);
    platform_file_release(&file);
    if(result) return -1;
    e->tables_loaded=1;
    return 0;
}
