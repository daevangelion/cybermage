#ifndef CM_EVENTS_H
#define CM_EVENTS_H

#include "sim.h"
#include "ui.h"

#define EVENTS_ITEM_MAX 40
#define EVENTS_SELECTOR_MAX 20
#define EVENTS_EVENT_MAX 512
#define EVENTS_SWITCH_MAX 128
#define EVENTS_AUTO_MAX 10
#define EVENTS_ARMOR_MAX 6

struct ai_state;

typedef struct {
    char name[20]; /* Xox+130D64, original 40-byte inventory record. */
    int16_t type, object;
    int32_t icon, quantity;
    uint8_t kind, subtype;
    uint8_t flags, variant;
    int32_t unk_0024;
} events_item;

typedef struct {
    int32_t value[16]; /* Xox+139D90, event runtime words, explicit save codec. */
    uint8_t state; /* +139390 */
    uint8_t type, enabled, once; /* +139190/+139590/+139790 */
    uint16_t next;
} events_event;

typedef struct {
    int32_t tile;
    uint16_t textures[2];
    int16_t fxo, key, link;
    uint8_t enabled, state;
} events_switch;

typedef struct {
    char name[20];
    uint8_t kind, subtype, icon, flags;
    int32_t unk_0018, scon, unk_0020, spell, value;
    uint8_t dice_count, dice_sides;
    int16_t dice_add;
    uint8_t extra[12];
} events_definition;

typedef struct {
    int32_t kind, screen, arg, switch_index;
} events_shop_request;

typedef struct {
    int32_t pending, scene, x, y, z;
    uint16_t heading;
    int32_t auto_mode;
} events_scene_request;

enum { EVENTS_SELECT_WEAPON=1, EVENTS_SELECT_POWER, EVENTS_SELECT_KEY };
/* Counts/indices at +144814/+144818, +14491C/+144920, +144974/+144978.
 * Native array layout is not the original stride. The +14497C shortcut map
 * and the temporary item/order lists are shared by all three selectors. */
typedef struct { unsigned count, index; } events_selector;

typedef struct events_state {
    struct ai_state *actors;
    events_item items[EVENTS_ITEM_MAX];
    events_event events[EVENTS_EVENT_MAX];
    events_switch switches[EVENTS_SWITCH_MAX];
    events_definition definitions[256];
    unsigned definition_count;
    unsigned event_count, switch_count, scene;
    uint32_t event_heads[64]; /* LoadEVENTS reads 256 bytes, 0x27381. */
    int16_t armor[EVENTS_ARMOR_MAX], max_armor[EVENTS_ARMOR_MAX];
    int32_t energy, max_energy, cash, weapon, hh_weapon, spell;
    int32_t item_spell;
    unsigned weapon_slot;
    uint32_t hh_dice;
    int32_t PCBet, PCBetPick, PCBetOdds, JackPot, SlotWaitTime, SlotVisitTime;
    int16_t SAVEStatus, SAVELife, SAVELifeMax;
    int32_t unk_1443FC, unk_144400;
    int16_t ammo[12], max_ammo[12];
    uint8_t spells[8];
    uint8_t speechLog[63];
    int32_t selector_items[EVENTS_SELECTOR_MAX], selector_order[EVENTS_SELECTOR_MAX];
    int32_t selector_keys[EVENTS_SELECTOR_MAX];
    events_selector selectors[3];
    unsigned selector_kind, selector_request, selector_confirm, selector_joy_latch;
    int32_t text_trigger, text_object, text_message;
    int text_skip_pending;
    uint32_t text_serial;
    int32_t text_switch1, text_switch0, text_event;
    int32_t recorder_requested, item_up_type, item_up_icon;
    events_shop_request shop_requested;
    events_scene_request scene_requested;
    uint32_t item_up_time;
    int32_t ambient_slot, ambient_sample;
    uint32_t ambient_until;
    int32_t video_list[9], video_frames[5], video_fxo[5];
    int32_t video_time[20], video_text[8], video_seq[70], video_flick[8];
    int32_t video_chain, ending_requested;
    char coderStr[24];
    int32_t coderEvent[3], coderSlot;
    int32_t autoDestruct, autoLFlag, autoMSlot, autoMSound, pc_dead_man;
    uint32_t autoDTime, autoDMsgTimer;
    uint32_t DrakMangTime;
    int32_t unk_144B58;
    uint32_t unk_144B5C;
    int32_t waterNdx[12], waterFlag, miscFlag, candleflicker;
    uint32_t waterTimer, miscTimer;
    uint16_t prop_base_texture[2048];
    int tables_loaded;
    int32_t tank_flag, vehicle_person, vehicle_type;
    unsigned foot_object, foot_person;
    int16_t auto_slot[EVENTS_AUTO_MAX], auto_ptr[EVENTS_AUTO_MAX];
    uint32_t ticks;
} events_state;

int events_init(events_state *e, world *w);
int events_load_tables(events_state *e, const char *asset_dir);
int events_step(events_state *e, sim_state *s, struct ai_state *a, world *w);
/* HandleNextFrame01C1CB..01C23F, after missile/soul updates, before input. */
int events_frame_begin(events_state *e, sim_state *s, struct ai_state *a, world *w);
/* HandleKeyboardEvents: after key motion sampling, before ProcessInputEvents. */
int events_keyboard(events_state *e, sim_state *s, struct ai_state *a, world *w);
int events_selector_needs_input(const events_state *e);
void events_reset_selectors(events_state *e);
/* Input: -1 error, 0 proceed to world. Selectors do not stop MoveWorld. */
int events_input(events_state *e, sim_state *s, struct ai_state *a, world *w);
int events_move_world(events_state *e, sim_state *s, struct ai_state *a, world *w);
void events_scene_cosmetics(events_state *e, const world *w);
int events_cosmetics(events_state *e, sim_state *s, struct ai_state *a, world *w);
int events_countdown(events_state *e, sim_state *s, struct ai_state *a, world *w);
int events_switch_search(events_state *e, sim_state *s, struct ai_state *a, world *w);
int events_key_search(events_state *e, sim_state *s, world *w, unsigned key);
int events_toggle_switch(events_state *e, world *w, unsigned index);
int events_use_item(events_state *e, sim_state *s, world *w, unsigned slot);
void events_fly_landing(sim_state *s, const world_object *p);
int events_coll_item(events_state *e, world *w, unsigned item, unsigned source);
int events_set_tank_mode(events_state *e, sim_state *s, world *w, int enter);
int events_clear_vehicle(sim_state *s, world *w, unsigned object);
int events_make_selector(events_state *e, unsigned kind);
int events_process_selector(events_state *e, sim_state *s, world *w);
int events_find_auto_slot(const events_state *e);
int events_inventory_add(events_state *e, unsigned type, int32_t amount);
int events_give_key(events_state *e, unsigned type, int32_t subtype);
int events_inventory_remove(events_state *e, unsigned slot, int32_t amount);
int events_buy_item(events_state *e, world *w, unsigned type);
void events_cash_add(events_state *e, int32_t delta);
int events_inventory_sell(events_state *e, unsigned slot, int32_t amount);
int events_arena_start(events_state *e, world *w);
int events_save_import(events_state *e, const ui_save_state *save);
int events_save_export(const events_state *e, ui_save_state *save);
int events_rescale_prop(world *w, unsigned placement, int32_t percent);
void events_zflat_drop(world *w, unsigned object, int32_t previous_z);
void events_zflat_raise(world *w, unsigned object, int32_t previous_z);
int events_ycrush_crash(sim_state *s, struct ai_state *a, world *w,
                        unsigned object, unsigned other);
int events_zwall_dcrash(sim_state *s, struct ai_state *a, world *w, unsigned object);
int events_zflat_dcrash(sim_state *s, struct ai_state *a, world *w, unsigned object);
int events_zflat_ucrash(sim_state *s, struct ai_state *a, world *w,
                        unsigned object, int32_t previous_z);

#endif
