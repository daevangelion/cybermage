#include "platform.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void script(const char *path, const char *data, uint32_t size)
{
    assert(platform_file_write(path, (const uint8_t *)data, size) == 0);
}

int main(int argc, char **argv)
{
    static const char valid[] =
        "0 down e048\n0 down 1e\n0 raw 1e\n1 up e048\n1 up 1e\n";
    static const char *bad[] = {
        "-1 down 1e\n", "42949672960000000000 down 1e\n", "0 down 1x\n",
        "0 raw 100\n", "0 down e080\n", "1 down 1e\n0 up 1e\n"
    };
    cm_input input = {0};
    cm_file file;
    unsigned i;
    char overflow[4096], state_path[1024];
    size_t used = 0;
    assert(argc == 3);
    assert(platform_init(1) == 0);
    assert(platform_user_dir() && *platform_user_dir());
    assert(platform_directory(argv[2]) == 0);
    assert(platform_file_exists(argv[2]) == -1);
    assert(snprintf(state_path, sizeof state_path, "%s/never-created-absent-test.bin", argv[2]) > 0);
    assert(platform_file_exists(state_path) == 0);
    assert(platform_ticks(99) == 99);
    for (i = 1; i <= 8; ++i) {
        assert(!platform_tick_step(i));
        assert(platform_ticks(99) == 99 * i);
    }
    assert(platform_tick_step(0) == -1 && platform_tick_step(9) == -1);
    assert(platform_ticks(99) == 99 * 8);
    assert(!platform_tick_step(1));
    assert(platform_state_dump(argv[2], 42, "state\n", 6) == 0);
    assert(snprintf(state_path, sizeof state_path, "%s/frame000042.state", argv[2]) > 0);
    assert(platform_file_read(&file, state_path) == 0);
    assert(file.size == 6 && !memcmp(file.data, "state\n", 6));
    platform_file_release(&file);
    {
        char path[1024], temporary[1024];
        assert(snprintf(path, sizeof path, "%s/atomic", argv[2]) > 0);
        assert(snprintf(temporary, sizeof temporary, "%s.tmp", path) > 0);
        assert(!remove(temporary) || errno == ENOENT);
        /* A failed temporary open must never truncate the existing save. */
        assert(platform_file_write(path, (const uint8_t *)"old", 3) == 0);
        assert(platform_directory(temporary) == 0);
        assert(platform_file_write(path, (const uint8_t *)"replacement", 11) == -1);
        assert(platform_file_read(&file, path) == 0);
        assert(file.size == 3 && !memcmp(file.data, "old", 3));
        platform_file_release(&file);
        assert(!remove(temporary));
    }
    script(argv[1], valid, sizeof valid - 1);
    assert(platform_file_exists(argv[1]) == 1);
    assert(platform_file_read(&file, argv[1]) == 0);
    assert(file.size == sizeof valid - 1 && !memcmp(file.data, valid, file.size));
    platform_file_release(&file);
    assert(platform_keys(argv[1]) == 0);
    assert(platform_poll(&input, 0) == 0);
    assert(input.raw_count == 4);
    assert(input.raw_scans[0] == 0xe0 && input.raw_scans[1] == 0x48);
    assert(input.raw_scans[2] == 0x1e && input.raw_scans[3] == 0x1e);
    assert(input.held[0x48] && input.pressed[0x1e]);
    assert(platform_poll(&input, 1) == 0);
    assert(input.raw_count == 3);
    assert(input.raw_scans[0] == 0xe0 && input.raw_scans[1] == 0xc8);
    assert(input.raw_scans[2] == 0x9e && input.released[0x1e]);
    for (i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        script(argv[1], bad[i], (uint32_t)strlen(bad[i]));
        assert(platform_keys(argv[1]) == -1);
    }
    script(argv[1], "0 raw f0\n", 9);
    assert(platform_keys(argv[1]) == 0);
    input.held[0x70] = 1;
    assert(platform_poll(&input, 0) == 0);
    assert(input.raw_count == 1 && input.raw_scans[0] == 0xf0);
    assert(input.held[0x70] == 1); /* original ISR treats all >=E0 as prefixes */
    script(argv[1], "0 raw 1e\0garbage", 16);
    assert(platform_keys(argv[1]) == -1);
    for (i = 0; i < CM_RAW_SCANS_MAX + 1; ++i) {
        memcpy(overflow + used, "0 raw 1e\n", 9);
        used += 9;
    }
    script(argv[1], overflow, (uint32_t)used);
    assert(platform_keys(argv[1]) == 0);
    assert(platform_poll(&input, 0) == -1);
    platform_shutdown();
    puts("platform: RWops roundtrip, ordered/repeated/E0 scans, malformed input and overflow passed");
    return 0;
}
