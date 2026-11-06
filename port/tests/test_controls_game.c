#include "platform.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int scripted_poll(cm_input *input, unsigned frame);
#define main cybermage_main
#define platform_poll scripted_poll
#define snd_close controls_game_checks
#include "../src/main.c"
#undef main
#undef platform_poll
#undef snd_close

void snd_close(void);
static SDL_JoystickID pad_id;
static int pad_index = -1;
static int moved, looked, jumped, fired;
static int32_t origin_x, origin_y;
static uint16_t origin_heading;

static void key(Uint32 type, SDL_Scancode scancode)
{
    SDL_Event event;
    memset(&event, 0, sizeof event);
    event.type = type;
    event.key.keysym.scancode = scancode;
    assert(SDL_PushEvent(&event) == 1);
}

static void axis(unsigned which, int value)
{
    SDL_Event event;
    memset(&event, 0, sizeof event);
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.which = pad_id;
    event.caxis.axis = (Uint8)which;
    event.caxis.value = (Sint16)value;
    assert(SDL_PushEvent(&event) == 1);
}

static int scripted_poll(cm_input *input, unsigned frame)
{
    if (frame == 0) key(SDL_KEYDOWN, SDL_SCANCODE_RETURN);
    if (frame == 1) key(SDL_KEYUP, SDL_SCANCODE_RETURN);
    if (frame == 2) {
        SDL_Event event;
        origin_x = game.objects[game.player_object].x;
        origin_y = game.objects[game.player_object].y;
        origin_heading = game.objects[game.player_object].heading;
        pad_index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 21, 0);
        assert(pad_index >= 0);
        pad_id = SDL_JoystickGetDeviceInstanceID(pad_index);
        key(SDL_KEYDOWN, SDL_SCANCODE_T);
        memset(&event, 0, sizeof event);
        event.type = SDL_MOUSEMOTION;
        event.motion.xrel = 3;
        event.motion.yrel = 2;
        assert(SDL_PushEvent(&event) == 1);
    }
    if (frame == 3) {
        assert(simulation.movement_request > 0); /* T came from --user/controls.cfg. */
        looked = game.objects[game.player_object].heading != origin_heading && game.camera.pitch != 0;
        axis(SDL_CONTROLLER_AXIS_LEFTX, 16384);
    }
    if (frame == 4) {
        SDL_Event event;
        memset(&event, 0, sizeof event);
        event.type = SDL_CONTROLLERBUTTONDOWN;
        event.cbutton.which = pad_id;
        event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
        assert(SDL_PushEvent(&event) == 1);
    }
    if (frame == 5) {
        jumped = simulation.unk_144b1c > 0;
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
    }
    /* Semi-automatic spells clear fire on the following held frame. */
    if (frame >= 6 && actors.pc.fire) fired = 1;
    if (frame == 10) {
        moved = game.objects[game.player_object].x != origin_x ||
                game.objects[game.player_object].y != origin_y;
        key(SDL_KEYUP, SDL_SCANCODE_T);
        assert(!SDL_JoystickDetachVirtual(pad_index));
        pad_index = -1;
    }
    return platform_poll(input, frame);
}

void controls_game_checks(void)
{
    fprintf(stderr, "modern gameplay: moved=%d looked=%d jumped=%d fired=%d mode=%" PRId32 "\n",
            moved, looked, jumped, fired, simulation.pc_mode);
    assert(moved && looked && jumped && fired);
    snd_close();
}

int main(int argc, char **argv)
{
    static const uint8_t config[] = "foot.forward = key:20\n";
    char config_path[1024];
    int n;
    const char *args[] = {"cybermage", "--headless", "--controls", "modern",
        "--data", "../orig/retail", "--user", NULL, "--frames", "12", "--bios-seed", "44"};
    assert(argc == 2);
    assert(!platform_directory(argv[1]));
    n = snprintf(config_path, sizeof config_path, "%s/controls.cfg", argv[1]);
    assert(n > 0 && (size_t)n < sizeof config_path);
    assert(!platform_file_write(config_path, config, sizeof config - 1));
    args[7] = argv[1];
    assert(!cybermage_main((int)(sizeof args / sizeof args[0]), (char **)args));
    puts("controls modern game: actual new-game movement/look/jump/fire through SDL events PASS");
    return 0;
}
