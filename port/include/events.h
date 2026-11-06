#ifndef CM_EVENTS_H
#define CM_EVENTS_H

#include "sim.h"

#define EVENTS_ITEM_MAX 40
#define EVENTS_SELECTOR_MAX 20
#define EVENTS_EVENT_MAX 512
#define EVENTS_SWITCH_MAX 128
#define EVENTS_AUTO_MAX 10
#define EVENTS_ARMOR_MAX 6

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

typedef struct events_state {
    events_item items[EVENTS_ITEM_MAX];
    events_event events[EVENTS_EVENT_MAX];
    events_switch switches[EVENTS_SWITCH_MAX];
    events_definition definitions[256];
    unsigned definition_count;
    unsigned event_count, switch_count;
    uint32_t event_heads[64]; /* LoadEVENTS reads 256 bytes, 0x27381. */
    int16_t armor[EVENTS_ARMOR_MAX], max_armor[EVENTS_ARMOR_MAX];
    int32_t energy, max_energy, cash, weapon, hh_weapon, spell;
    int32_t unk_1443FC, unk_144400;
    int16_t ammo[12], max_ammo[12];
    uint8_t spells[8];
    uint8_t speechLog[63];
    int32_t selector_items[EVENTS_SELECTOR_MAX], selector_order[EVENTS_SELECTOR_MAX];
    int32_t selector_keys[EVENTS_SELECTOR_MAX];
    unsigned selector_count, selector_index, selector_kind;
    int32_t text_trigger, text_object, text_message;
    int32_t text_switch1, text_switch0, text_event;
    int32_t recorder_requested, item_up_type, item_up_icon;
    uint32_t item_up_time;
    int32_t tank_flag, vehicle_person, vehicle_type, previous_mode;
    unsigned foot_object, foot_person;
    int16_t auto_slot[EVENTS_AUTO_MAX], auto_ptr[EVENTS_AUTO_MAX];
    uint32_t ticks;
} events_state;

struct ai_state;
int events_init(events_state *e, world *w);
int events_step(events_state *e, sim_state *s, struct ai_state *a, world *w);
int events_switch_search(events_state *e, sim_state *s, struct ai_state *a, world *w);
int events_key_search(events_state *e, sim_state *s, world *w, unsigned key);
int events_use_item(events_state *e, sim_state *s, world *w, unsigned slot);
int events_coll_item(events_state *e, world *w, unsigned item, unsigned source);
int events_set_tank_mode(events_state *e, sim_state *s, world *w, int enter);
int events_clear_vehicle(sim_state *s, world *w, unsigned object);
int events_make_selector(events_state *e, unsigned kind);
int events_process_selector(events_state *e, sim_state *s, world *w);
int events_find_auto_slot(const events_state *e);

#endif
