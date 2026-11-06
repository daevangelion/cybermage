#define main cybermage_main
#define snd_close text_load_checks
#include "../src/main.c"
#undef main
#undef snd_close
#include <assert.h>
#include <inttypes.h>

void snd_close(void);
static const char *test_user;

static int matrix(void)
{
    static const char *const lines[] = {"Test"};
    rsc images = {0};
    cm_file strings_file = {0};
    uint32_t ticks, deadline, line, add, end;
    ui_view view = {0, 0, CM_WIDTH, CM_HEIGHT};
    unsigned rows = 0;
    int scanned;
    assert(!platform_init(1));
    assert(!rsc_open(&images, "../orig/retail/HIPAK.RSC"));
    assert(!platform_file_read(&strings_file, "../orig/retail/STRINGS.MSG"));
    assert(!ui_init(&ui, &images, &strings_file));
    save.textTrigger = 1;
    while ((scanned = scanf("%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
                            " %" SCNu32, &ticks, &deadline, &line, &add, &end)) == 5) {
        int result;
        save.textCount = deadline; save.textLine = line;
        save.textAdd = add; save.textEnd = end;
        assert(!ui_text_restore(&ui, &save, 1));
        ui.ticks = ticks;
        result = ui_text_message(&ui, &game, pixels, lines, 1, 1, 0, view);
        assert(result >= 0);
        printf("%d %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
            result, ui.text_deadline, ui.text_line, ui.text_add, ui.text_end);
        ++rows;
    }
    assert(scanned == EOF && rows);
    save.textEnd = 2;
    assert(ui_text_restore(&ui, &save, 99) == -1 && ui.text_revision == 1);
    rsc_close(&images); platform_file_release(&strings_file);
    platform_shutdown();
    return 0;
}

/* The same completed caption saved in the genuine SOUTH checkpoint: its
 * absolute timer remains in the future after loading at a lower game clock. */
void text_load_checks(void)
{
    uint32_t size;
    const snd_dialogue *text;
    ui_view view = {0, 0, CM_WIDTH, CM_HEIGHT};
    assert(game.scene == 11);
    assert(!encode_save(&size));
    save.textMsgNum = 9; save.textMsgScen = 11;
    save.textTrigger = 2; save.textObj = 1417;
    save.textCount = 2539; save.textLine = 1;
    save.textAdd = 1; save.textEnd = 1;
    save.textSwitch0 = save.textSwitch1 = save.textEvent = 0;
    assert(!ui_save_game(&save, &game, save_data, sizeof save_data, &size));
    assert(!make_path(save_path, sizeof save_path, test_user, "SAVE000.DAT"));
    assert(!platform_file_write(save_path, save_data, size));
    ui.text_deadline = ui.text_line = ui.text_add = 0;
    events.text_skip_pending = 1;
    assert(!load_game("../orig/retail", test_user, 0, 1021));
    assert(ui.text_deadline == 2539 && ui.text_line == 1 && ui.text_add == 1);
    assert(ui.text_end == 1);
    assert(events.text_trigger == 1 && !events.text_skip_pending);
    text = snd_text();
    assert(text->active && events.text_serial == text->revision);
    ui.ticks = 1021;
    assert(!ui_text_message(&ui, &game, pixels, text->lines, text->line_count,
                            text->revision, text->object, view));
    assert(events.text_trigger == 1);
    ui.ticks = 2539;
    assert(!ui_text_message(&ui, &game, pixels, text->lines, text->line_count,
                            text->revision, text->object, view));
    assert(!encode_save(&size));
    assert(save.textMsgScen == 11 && save.textCount == 2539);
    assert(save.textLine == 1 && save.textAdd == 1 && save.textEnd == 1);
    ui.ticks = 2540;
    assert(ui_text_message(&ui, &game, pixels, text->lines, text->line_count,
                           text->revision, text->object, view) == 1);
    snd_message_stop();
    assert(!events_text_finish(&events, &game));
    assert(!events.text_trigger);
    assert(!encode_save(&size));
    assert(!save.textCount && save.textLine == 2 && save.textEnd == 1);
    events.text_trigger = 2; events.text_scene = 7; events.text_serial = 123;
    events.text_skip_pending = 1;
    assert(!events_text_restore(&events, &game));
    assert(!events.text_trigger && !events.text_serial && !events.text_skip_pending);
    snd_close();
}

int main(int argc, char **args)
{
    static const uint8_t keys[] =
        "0 raw 9c\n1 down e050\n1 up e050\n2 down 1c\n2 up 1c\n";
    char path[PATH_BYTES];
    const char *argv[] = {"cybermage", "--headless", "--data", "../orig/retail",
        "--user", NULL, "--keys", path, "--frames", "4", "--bios-seed", "44"};
    if (argc == 2 && !strcmp(args[1], "--matrix")) return matrix();
    assert(argc == 2);
    test_user = args[1]; argv[5] = test_user;
    assert(!make_path(path, sizeof path, test_user, "training.keys"));
    assert(!platform_file_write(path, keys, sizeof keys - 1));
    assert(!cybermage_main((int)(sizeof argv / sizeof argv[0]), (char **)argv));
    puts("text load: saved absolute deadline and completed-page gate retained");
    return 0;
}
