#include "combat_internal.h"
#include "audio.h"

static int32_t multiply(int32_t a, int32_t b) { return ai_scale32(a,b,1); }
static int32_t absolute(int32_t n) { return n < 0 ? ai_sub32(0,n) : n; }
static int32_t sine(const world *w, uint16_t angle, int32_t distance)
{
    return ai_fixed32(w->trig.sin[angle >> 5],distance);
}
static int32_t cosine(const world *w, uint16_t angle, int32_t distance)
{
    return ai_fixed32(w->trig.cos[angle >> 5],distance);
}
static void forward(const world *w, world_object *o, const world_object *pc, uint16_t angle, int32_t distance)
{
    o->x = ai_add32(pc->x,cosine(w,angle,distance));
    o->y = ai_add32(pc->y,sine(w,angle,distance));
}
static int32_t muzzle_z(const ai_pc_aim *q, const sim_state *s, const world *w, int32_t y)
{
    const world_object *pc = &w->objects[w->player_object];
    int32_t z = ai_add32(ai_add32(pc->floor_z,pc->z),q->pistol_udoffset);
    z = ai_add32(z,multiply(ai_sub32(y,q->pistol_boffset),q->pistol_ud));
    z = ai_add32(z,s->unk_144b34);
    return ai_sub32(z,multiply(w->camera.pitch / 2,64));
}
static void gun_position(const ai_pc_aim *q, const sim_state *s, const world *w,
                          world_object *o, int32_t x, int32_t y)
{
    const world_object *pc = &w->objects[w->player_object];
    int32_t lateral = ai_add32(multiply(ai_sub32(x,q->pistol_xoffset),q->pistol_lr),q->pistol_lroffset);
    uint16_t side = (uint16_t)(pc->heading - 0x4000);
    forward(w,o,pc,pc->heading,0x2800);
    o->x = ai_add32(o->x,cosine(w,side,lateral)); o->y = ai_add32(o->y,sine(w,side,lateral));
    o->floor_z = muzzle_z(q,s,w,y); o->z = 0;
}
static int target_height(const world *w, int target, int quarter_extra, int32_t *height)
{
    const world_object *t, *pc = &w->objects[w->player_object];
    int person;
    if (target < 0 || (unsigned)target >= w->object_count)
        return cm_fail("PCLaunchMissile",(uint32_t)target,"invalid auto target");
    t = &w->objects[target]; person = t->person;
    if (person < 0 || (unsigned)person >= w->person_count)
        return cm_fail("PCLaunchMissile",(uint32_t)target,"target has no PERSON");
    *height = ai_add32(t->z,quarter_extra ? t->z / 4 : 0);
    if (t->floor_z > pc->floor_z) *height = ai_add32(*height,t->height / 2);
    if (w->persons[person].water) *height /= 2;
    *height = ai_add32(*height,t->floor_z);
    return 0;
}
static void aim_feedback(ai_pc_aim *q, int32_t y)
{
    q->aim_y = y;
    if (q->aim_y < q->w_max_botaimy) q->aim_y = q->w_max_botaimy;
    else if (q->aim_y > q->w_max_topaimy) q->aim_y = q->w_max_topaimy;
    if (q->aim_y < q->w_botaimy) q->weapon_aim_step = ai_add32(q->weapon_aim_step,3);
    else if (q->aim_y > q->w_topaimy) q->weapon_aim_step = ai_sub32(q->weapon_aim_step,3);
}
static void grenade_vertical(ai_missile *m, int32_t delta)
{
    int32_t range = ai_sub32(0x24000,multiply(absolute(delta),1024));
    m->vertical = ai_scale32(absolute(multiply(delta,2048)),0x4000,range);
    if (m->vertical < 64) m->vertical = 64;
    m->ticks = ai_sub32(range,0x8000) / 0x4000;
    if (delta < 0) { m->ticks = ai_sub32(m->ticks,absolute(delta / 2)); if (m->ticks < 0) m->ticks = 0; }
}

/* CM_HI.EXE PCLaunchMissile02F1C4..03092C. The longs after type are
 * screen aim X/Y, not damage or object target; type4 alone reuses X as an
 * object index and the final short as its reverse heading. */
int ai_pc_launch_missile(ai_state *a, sim_state *s, world *w, unsigned type,
                         int32_t aim_x, int32_t aim_y, int16_t aim)
{
    ai_pc_aim *q = &a->pc_aim;
    const world_object *pc;
    world_object *o;
    ai_missile *m;
    const ai_missile_data *d;
    unsigned slot;
    int target;
    int32_t pitch_scale, pitch = w->camera.pitch, height, distance;
    if (!a->tables_loaded || type >= AI_MISSILE_TYPES || w->player_object >= w->object_count)
        return cm_fail("PCLaunchMissile",type,"invalid missile/player/table state");
    for (slot = 0; slot < AI_MISSILE_MAX && a->missiles[slot].active > 0; ++slot) {}
    if (slot == AI_MISSILE_MAX) return 0;
    m = &a->missiles[slot]; d = &a->missile_types[type];
    if (m->object < 0 || (unsigned)m->object >= w->object_count)
        return cm_fail("PCLaunchMissile",slot,"uninitialized missile object");
    o = &w->objects[m->object]; pc = &w->objects[w->player_object];
    m->target = 0; m->source = (int32_t)w->player_object; m->frame = 0;
    m->sound = -1; m->variant = 0; m->speed = d->speed;
    pitch_scale = multiply(m->speed / 2048,13);
    if (type == 4) {
        const world_object *t;
        int person;
        int32_t top;
        if (aim_x < 0 || (unsigned)aim_x >= w->object_count)
            return cm_fail("PCLaunchMissile",(uint32_t)aim_x,"invalid attached projectile target");
        t = &w->objects[aim_x]; person = t->person;
        if (person < 0 || (unsigned)person >= w->person_count)
            return cm_fail("PCLaunchMissile",(uint32_t)aim_x,"attached target has no PERSON");
        if (ai_set_missile(w,(unsigned)m->object,6)) return -1;
        m->vertical = 4; m->target = aim_x; m->ticks = 0x1000;
        o->heading = (uint16_t)(aim - 0x8000); forward(w,o,t,o->heading,0x1000);
        o->floor_z = ai_add32(ai_add32(ai_add32(pc->floor_z,pc->z),pc->height / 2),s->unk_144b34);
        o->floor_z = ai_sub32(o->floor_z,multiply(pitch / 2,64));
        top = ai_add32(t->floor_z,multiply(t->height,w->persons[person].water ? 1 : 2));
        if (o->floor_z > top) o->floor_z = top;
        else if (o->floor_z < t->floor_z) o->floor_z = t->floor_z;
        o->z = 0;
    } else if (type == 6) {
        snd_play(15,32767,32768);
        if (ai_set_missile(w,(unsigned)m->object,8)) return -1;
        if (!m->speed) return cm_fail("PCLaunchMissile",type,"zero tank projectile speed");
        o->heading = (uint16_t)(pc->heading + world_rnd(&w->rng,0x400) - 0x200);
        forward(w,o,pc,pc->heading,0x3000);
        o->floor_z = ai_add32(ai_add32(ai_add32(pc->floor_z,pc->z),q->tank_udoffset),multiply(aim_y,q->tank_ud)); o->z = 0;
        m->ticks = 0x40000 / m->speed + 1;
        m->vertical = multiply(ai_sub32(aim_y,q->tank_falloffset),q->tank_fall);
        if (q->auto_enable && q->tank_auto) {
            q->tank_aim_step = 1; target = ai_set_auto_target(a,w);
            if (target < 0) {
                q->tank_aim_y = 19;
                if (aim_y != 19) {
                    o->floor_z = ai_add32(ai_add32(ai_add32(pc->floor_z,pc->z),q->tank_udoffset),multiply(19,q->tank_ud));
                    m->vertical = multiply(ai_sub32(19,q->tank_falloffset),q->tank_fall);
                }
            } else {
                if (target_height(w,target,0,&height)) return -1;
                distance = cm_distance(o->x,o->y,w->objects[target].x,w->objects[target].y);
                m->vertical = ai_scale32(ai_sub32(o->floor_z,height),m->speed,distance);
                q->reset_auto_aim_time = a->timer_ticks + 60;
                if (!q->tank_fall) return cm_fail("PCLaunchMissile",type,"zero tank vertical trajectory scale");
                q->tank_aim_y = ai_add32(m->vertical / q->tank_fall,q->tank_falloffset);
                if (q->tank_aim_y < 19) q->tank_aim_y = 19;
                else if (q->tank_aim_y > 63) q->tank_aim_y = 63;
            }
            if (q->tank_aim_y < 32) q->tank_aim_step = ai_add32(q->tank_aim_step,1);
            else if (q->tank_aim_y > 47) q->tank_aim_step = ai_sub32(q->tank_aim_step,1);
        }
    } else {
        unsigned prop = type == 1 ? 3u : type == 38 ? 4u : (unsigned)d->prop;
        if (type != 1 && type != 38 && d->sound) snd_play((unsigned)d->sound,32767,32768);
        if (ai_set_missile(w,(unsigned)m->object,prop)) return -1;
        o->heading = (uint16_t)(pc->heading + q->pistol_angoffset - multiply(ai_sub32(aim_x,q->pistol_xoffset),q->pistol_angle));
        if (type != 1 && type != 38) {
            if (type == 14) { m->frame = (int32_t)(slot % 3); o->texture = (uint16_t)(o->texture + m->frame * 16); }
            else if (type == 15 || type == 20 || type == 24 || type == 39 || type == 50) {
                m->frame = (int32_t)(slot % (type == 50 ? 8 : 4)); o->texture = (uint16_t)(o->texture + m->frame);
            }
            if (type == 26 || type == 27) o->flags |= 0x8000;
            if (aim && world_rnd(&w->rng,2)) o->heading = (uint16_t)(o->heading + (world_rnd(&w->rng,5) - 2) * 128);
        }
        gun_position(q,s,w,o,aim_x,aim_y);
        if (type == 1) {
            int32_t delta = ai_sub32(aim_y,q->grenade_aim_center);
            if (pitch < 0) delta = ai_sub32(delta,pitch / 6);
            else if (pitch > 0) delta = ai_sub32(delta,pitch / 12);
            grenade_vertical(m,delta);
        } else {
            if (!m->speed) return cm_fail("PCLaunchMissile",type,"zero projectile speed");
            m->ticks = type == 38 ? 20 : 0x40000 / m->speed + 1;
            m->vertical = multiply(ai_sub32(aim_y,q->pistol_falloffset),q->pistol_fall);
        }
        if (q->auto_enable) {
            target = ai_set_auto_target(a,w);
            if (target < 0) {
                if (ai_reset_aim(a,s)) return -1;
                if (q->aim_y != aim_y) {
                    o->floor_z = muzzle_z(q,s,w,q->aim_y);
                    if (type == 1) {
                        int32_t delta = ai_sub32(q->aim_y,q->grenade_aim_center);
                        if (pitch < 0) delta = ai_sub32(delta,pitch / 6);
                        else if (pitch > 0) delta = ai_sub32(delta,pitch / 12);
                        grenade_vertical(m,delta);
                    } else m->vertical = multiply(ai_sub32(q->aim_y,q->pistol_falloffset),q->pistol_fall);
                }
                if (type != 1) m->vertical = ai_add32(m->vertical,multiply(pitch / 2,type==38?-pitch_scale:pitch_scale));
            } else {
                if (type == 1) {
                    const world_object *t = &w->objects[target];
                    int32_t range = ai_add32(cm_distance(o->x,o->y,t->x,t->y),0x4000);
                    int person = t->person;
                    if (person < 0 || (unsigned)person >= w->person_count) return cm_fail("PCLaunchMissile",(uint32_t)target,"invalid grenade target");
                    if (range > 0x24000) range = 0x24000;
                    else if (range < 0x8000) range = 0x8000;
                    if (t->floor_z > pc->floor_z) {
                        int32_t h = w->persons[person].water ? t->height / 2 : t->height;
                        m->vertical = absolute(ai_scale32(ai_sub32(ai_add32(t->floor_z,h),o->floor_z),0x4000,range-0x4000));
                        m->ticks = (range - 0x8000) / 0x4000;
                    } else { m->vertical = multiply(pc->height,2) >> ((unsigned)(range / 0x4000) & 31); m->ticks = 0; }
                    if (m->vertical < 64) m->vertical = 64;
                    aim_feedback(q,aim_y);
                } else {
                    int32_t feedback;
                    if (target_height(w,target,type!=38,&height)) return -1;
                    distance = cm_distance(o->x,o->y,w->objects[target].x,w->objects[target].y);
                    m->vertical = ai_scale32(type==38?ai_sub32(height,o->floor_z):ai_sub32(o->floor_z,height),m->speed,distance);
                    if (!q->pistol_fall) return cm_fail("PCLaunchMissile",type,"zero vertical trajectory scale");
                    if (type == 38) feedback = ai_add32(m->vertical,multiply(pitch / 2,pitch_scale));
                    else {
                        feedback = m->speed > distance ? ai_scale32(ai_sub32(o->floor_z,height),distance,m->speed) : m->vertical;
                        feedback = ai_sub32(feedback,multiply(pitch / 2,pitch_scale));
                    }
                    aim_feedback(q,ai_add32(feedback / q->pistol_fall,q->pistol_falloffset));
                }
                q->reset_auto_aim_time = a->timer_ticks + 60;
            }
        }
    }
    m->active = 1; m->type = (int32_t)type;
    if (type == 40 || type == 18 || type == 43 || type == 2 || type == 49) o->active = 0;
    else { o->active = 1; world_omap_insert(w,(unsigned)m->object); }
    return 1;
}
