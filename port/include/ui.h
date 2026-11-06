#ifndef CM_UI_H
#define CM_UI_H

#include "gfx.h"
#include "rsc.h"
#include "world.h"

#define UI_STRING_COUNT 212
#define UI_FONT_COUNT 7
#define UI_SAVE_SLOTS 10
#define UI_NAME_BYTES 24
#define UI_CONSOLE_BITMAPS 13
#define UI_GLOVE_COUNT 16
#define UI_HAND_FRAMES 18
#define UI_SHOP_ITEMS 40
#define UI_BITMAPS_MAX 24
#define UI_CONFIG_BYTES 172
#define UI_CONFIG_KEYS 15

/* CMDATA.CNF, ReadConfigFile020931..020C05 / WriteConfigFile020C18.
 * options[] below is the original GUI's separate LoadOptVals projection. */
typedef struct {
    int32_t in_device, difficulty, brightness, contrast, text_delay;
    int32_t midi_volume, digital_volume, speech_volume;
    int32_t joystick[4];
    uint32_t key[UI_CONFIG_KEYS], mask[UI_CONFIG_KEYS];
} ui_config;

typedef enum {
    UI_GAME, UI_TITLE, UI_OPTIONS, UI_PAUSE, UI_SAVE, UI_LOAD, UI_HELP,
    UI_RECORDER, UI_DEATH
} ui_screen;
typedef enum {
    UI_NONE, UI_NEW_GAME, UI_TRAINING, UI_RESUME, UI_SAVE_GAME,
    UI_LOAD_GAME, UI_QUIT, UI_CREDITS, UI_SPEECH_REPLAY, UI_ERROR=-1
} ui_action;

typedef struct {
    const uint8_t *pixels;
    uint16_t width, height;
} ui_bitmap;

typedef struct {
    ui_screen screen, return_screen;
    unsigned selected, slot;
    int naming;
    unsigned mouse_buttons;
    uint32_t ticks;
    uint32_t blip_time;
    unsigned hp_count;
    uint32_t blink_timer;
    int blink;
    int options[10];
    ui_config config;
    int applied_volumes[3], volumes_applied;
    unsigned con_size, vdu;
    int32_t view_info[8][4], ACarX[11], ACarY[11];
    uint8_t TankBar[128], slot_symbols[28];
    const uint8_t *backscreen[3];
    ui_bitmap acar[UI_BITMAPS_MAX];
    unsigned acar_count;
    const char *strings[UI_STRING_COUNT];
    cm_font fonts[UI_FONT_COUNT];
    const uint8_t *title, *options_screen, *help_screen, *recorder_screen;
    const uint8_t *shades, *bright;
    ui_bitmap console[UI_CONSOLE_BITMAPS];
    ui_bitmap scon[33];
    unsigned scon_count;
    char names[UI_SAVE_SLOTS][UI_NAME_BYTES];
    int16_t auto_ptr[UI_SAVE_SLOTS], auto_slot[UI_SAVE_SLOTS];
    int32_t restart_game, restart_auto;
    int32_t Glv_xOff[UI_GLOVE_COUNT][UI_HAND_FRAMES];
    int32_t Glv_yOff[UI_GLOVE_COUNT][UI_HAND_FRAMES];
    int32_t Glv_xPosOff[UI_GLOVE_COUNT][UI_HAND_FRAMES];
    int32_t Glv_xFrame[UI_GLOVE_COUNT][22];
    int32_t Swd_xFrame[4][UI_HAND_FRAMES];
    int32_t SwordX[4][UI_HAND_FRAMES];
    int16_t MuzzleX[UI_GLOVE_COUNT][9][3], MuzzleY[UI_GLOVE_COUNT][9][3];
    int32_t CenterAimX[UI_GLOVE_COUNT], CenterAimY[UI_GLOVE_COUNT];
    int32_t spellPower[16], spellScon[16];
    int32_t GlowSet[16];
    uint8_t SpellGlow[72];
    int32_t flashcolor[7];
    int hand_tables_loaded;
    uint32_t text_revision, text_deadline;
    unsigned text_line, text_add;
    uint8_t speechLog[63];
    uint8_t recorder_map[63];
    unsigned recorder_count;
    const uint8_t *shop_background[4];
    ui_bitmap shop_gfx[4][UI_BITMAPS_MAX];
    unsigned shop_gfx_count[4];
    int32_t shop_stock[3][16];
    int32_t shop_kind, shop_switch, shop_arg, shop_screen;
    unsigned shop_selected, shop_first, shop_count;
    int shop_selling;
    int32_t shop_items[UI_SHOP_ITEMS], shop_slots[UI_SHOP_ITEMS];
    int32_t coder_x[12], coder_y[12];
    char shop_code[5];
    unsigned shop_button;
    int32_t bet_amount, bet_payout;
    int slot_spinning, slot_result;
    unsigned slot_reel[3], slot_steps[3], slot_message;
    uint32_t gambling_next;
    int32_t arena_x[14], arena_y[14], slot_x[24], slot_y[24];
    int shop_success;
    uint32_t shop_button_until;
} cm_ui;

typedef struct {
    int hp, max_hp, energy, max_energy, ammo, cash;
    int armor[6], max_armor[6];
    unsigned hp_count;
    int status, weapon, selector_active;
} ui_hud;

/* Resource storage remains caller-owned and valid through the UI lifetime. */
int ui_init(cm_ui *ui, rsc *hipak, const cm_file *strings);
int ui_bitmap_bank(rsc *hipak, unsigned entry, ui_bitmap *bitmaps,
                    unsigned capacity, unsigned *count);
int ui_load_hand_tables(cm_ui *ui, const char *asset_dir);
void ui_open(cm_ui *ui, ui_screen screen);
void ui_speech_recorder(cm_ui *ui, const uint8_t speech_log[63]);
ui_action ui_input(cm_ui *ui, const cm_input *input, uint32_t ticks);
int ui_draw(cm_ui *ui, uint8_t pixels[CM_WIDTH * CM_HEIGHT]);
int ui_text_mode(uint8_t *dst, const cm_font *font, int x, int y,
                  const char *text, uint8_t fg, uint8_t outline,
                  uint8_t shadow, int mode);
int ui_draw_scon_bitmap(const cm_ui *ui, uint8_t *dst, int x, int y,
                        unsigned icon, unsigned shade);
int ui_draw_prop_icon(const world *w, uint8_t *dst, int x, int y,
                       unsigned resource);
int ui_draw_shop_object(const world *w, uint8_t *dst, int x, int y,
                         unsigned resource);
struct events_state;
int ui_gambling_open(cm_ui *ui, struct events_state *e, world *w);
int ui_gambling_step(cm_ui *ui, struct events_state *e, world *w,
                       const cm_input *input);
int ui_gambling_draw(cm_ui *ui, const struct events_state *e, uint8_t *dst);
int ui_shop_step(cm_ui *ui, struct events_state *events, world *w,
                   const cm_input *input, uint32_t ticks);
int ui_shop_draw(cm_ui *ui, const struct events_state *events, const world *w,
                   uint8_t *dst);
int ui_draw_selector(const cm_ui *ui, const world *w,
                      const struct events_state *events, uint8_t *dst);
int ui_render_bells_and_whistles(cm_ui *ui, uint8_t *pixels,
                                const ui_hud *hud, const cm_draw_clock *clock);
/* Only when StatusMonitor actually runs; shared with pixel-free verification. */
int ui_status_visible(const cm_ui *ui,const ui_hud *hud);
int ui_status_advance(cm_ui *ui, const cm_draw_clock *clock);
void ui_draw_monitor(uint8_t *dst, int x, int y, const ui_bitmap *bitmap);
typedef struct {
    world_span pixels;
    int half_width, half_height;
    int view_x, view_y, view_width, view_height;
    int x_off, y_off, x_pos_off, aim_x, aim_y;
    int sword, tank, muzzle;
} ui_hand;
typedef struct { int x, y, width, height; } ui_view;
int ui_apply_settings(cm_ui *ui, uint8_t palette[768], const uint8_t base_rgb[768]);
void ui_config_defaults(ui_config *config);
int ui_config_decode(ui_config *config,const uint8_t *data,uint32_t size,const char *path);
int ui_config_encode(const ui_config *config,uint8_t *data,uint32_t size,const char *path);
int ui_config_load(ui_config *config,const char *path);
int ui_config_save(const ui_config *config,const char *path);
void ui_options_from_config(cm_ui *ui);
void ui_options_to_config(cm_ui *ui);
int ui_game_view(const cm_ui *ui, int mode, int vehicle_type, ui_view *view);
int ui_draw_backdrop(const cm_ui *ui, uint8_t *dst, int mode,
                       int vehicle_type, ui_view view);
int ui_draw_vehicle(const cm_ui *ui, const world *w, uint8_t *dst,
                      int vehicle_type, int speed, int altitude);
int ui_text_message(cm_ui *ui, const world *w, uint8_t *dst,
                    const char *const *lines, unsigned count,
                    uint32_t revision, int object, ui_view view);

int ui_draw_xscale_aim(uint8_t *dst, const ui_hand *hand,
                       const uint8_t *glow);
int ui_draw_player_hand(const cm_ui *ui, world *w, uint8_t *dst,
                        int mode, int weapon, unsigned frame,
                        ui_view view);
struct ai_state;
/* after_hand0: transient tint;1: damage flash. State advances in AI, not here. */
int ui_draw_pc_tint(const cm_ui *ui, uint8_t *dst, const struct ai_state *a,
                      ui_view view, int after_hand);
int ui_draw_vehicle_muzzle(const cm_ui *ui, const struct ai_state *a, uint8_t *dst);
int ui_draw_live_hand(const cm_ui *ui, world *w, uint8_t *dst,
                       int mode, const struct events_state *events,
                       const struct ai_state *actors, ui_view view);
int ui_draw_menu_cursor(const cm_ui *ui, world *w, uint8_t *dst,
                         int aim_x, int aim_y, unsigned frame);
unsigned ui_cursor_pose(int aim_x, int aim_y, int pressed);
/* 1 sampled a completed melt column,0 none,-1 malformed column state. */
int ui_pc_death(uint8_t *dst, const uint8_t *background,
                unsigned columns[CM_WIDTH], world_rng *rng);
int ui_game_names_read(cm_ui *ui, const uint8_t *data, uint32_t size);
int ui_game_names_write(const cm_ui *ui, uint8_t data[288]);

#define UI_SAVE_PREFIX 216861u
#define UI_PERSON_BYTES 192u
#define UI_SAVE_MAX (UI_SAVE_PREFIX + CM_OBJECT_MAX * UI_PERSON_BYTES)

/* Unidentified saved fields retain their original Xox/global names. */
typedef struct {
    uint16_t unk_130D50[870];
    uint32_t unk_6D4[29]; /* file0x6D4: discontiguous Xox scalar fields */
    uint32_t PCBet, PCBetPick, PCBetOdds, JackPot;
    uint32_t unk_BDEB8, unk_5DFA0;
    uint8_t speechLog[63], coderStr[24];
    uint32_t unk_7B7[9];
    uint32_t unk_142BFC[1536];
    uint32_t unk_1443FC, unk_144400;
    uint32_t textMsgNum, textMsgScen, textCount, textLine, textAdd, textEnd;
    uint32_t textSwitch1, textSwitch0, textEvent, textTrigger, textObj;
    uint32_t autoDestruct, autoDTime, gemCountDown, gemCountDownTimer;
    uint32_t unk_135ECC[144], unk_13610C[70];
    int16_t light_param[32]; /* Xox+A48F4 */
    uint16_t light_type[32]; /* Xox+A4934 */
    uint16_t unk_139390[256], unk_139590[256];
    uint32_t unk_139D90[8192];
    uint8_t aperture_enabled[96]; /* Xox+142390 */
    uint8_t unk_142B78[128], unk_1428F8[256];
    uint32_t FireCheck, FireTilePanel, FireTileArt, FireDamage;
    int loaded;
} ui_save_state;

int ui_save_scene(const uint8_t *data, uint32_t size, unsigned *scene);
void ui_save_init(ui_save_state *save, const world *w);
struct events_state;
int ui_hud_from_world(ui_hud *hud, const world *w,
                      const struct events_state *events, unsigned phase,
                      int hand_mode);
int ui_load_game(ui_save_state *save, world *w, const uint8_t *data,
                 uint32_t size, const char *asset_dir);
int ui_save_overlay(ui_save_state *save, world *w, const uint8_t *data,
                      uint32_t size);
int ui_save_game(ui_save_state *save, const world *w, uint8_t *data,
                 uint32_t capacity, uint32_t *size);

#endif
