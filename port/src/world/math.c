#include "world.h"
#include <limits.h>
#include <math.h>

static int32_t signed32(uint32_t v)
{
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v);
}
static int32_t sub32(int32_t a, int32_t b)
{
    return signed32((uint32_t)a - (uint32_t)b);
}
static int32_t mul16(int32_t a, int32_t b)
{
    return signed32((uint32_t)((uint64_t)((int64_t)a * b) >> 16));
}
static int32_t chop(double v)
{
    if (!isfinite(v) || v < -2147483648.0 || v >= 2147483648.0) return INT32_MIN;
    return (int32_t)v;
}

/* CM_HI.EXE MakeTables 0x58F88, constants at 0x91D34..0x91D64.
 * The original uses x87, including signed round-to-nearest table generation;
 * angle and distance conversion use __CHP (truncate, not floor).
 * [rust: native/image1/project.rs] supplies corroborating leaf semantics. */
void world_trig_init(world_trig *t)
{
    unsigned i;
    for (i = 0; i < CM_TRIG_SIZE; ++i) {
        double angle = (double)i * 6.283185307179586 * 0.00048828125;
        double s = sin(angle) * 65536.0;
        double c = cos(angle) * 65536.0;
        double tangent = tan(((double)i + 0.5) * 1.570796326794897 * 0.00048828125);
        t->tan[i] = chop(tangent * 65536.0 + 0.5);
        t->sin[i] = chop(s + (s > 0 ? 0.5 : s < 0 ? -0.5 : 0.0));
        t->cos[i] = chop(c + (c > 0 ? 0.5 : c < 0 ? -0.5 : 0.0));
    }
}

uint16_t cm_atan64k(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t dx = sub32(x2, x1), dy = sub32(y2, y1);
    if (!dx && !dy) return 0;
    return (uint16_t)chop(atan2((double)dy, (double)dx) * 10430.37835047045);
}

int32_t cm_distance(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    double dx = sub32(x2, x1), dy = sub32(y2, y1);
    return chop(sqrt(dx * dx + dy * dy));
}

void cm_fixed_rotate(const world_trig *t, int32_t *x, int32_t *y,
                     int32_t cx, int32_t cy, uint16_t angle)
{
    int32_t dx = sub32(*x, cx), dy = sub32(*y, cy);
    int32_t co = t->cos[angle >> 5], si = t->sin[angle >> 5];
    *x = signed32((uint32_t)cx + (uint32_t)mul16(co, dx) - (uint32_t)mul16(si, dy));
    *y = signed32((uint32_t)cy + (uint32_t)mul16(si, dx) + (uint32_t)mul16(co, dy));
}

/* RANDOM_SEED 0x68ABF only replaces the middle word with BIOS tick low16.
 * Fresh EXE words are {3000,1,29999} at0x9747F,0x97481,0x97483. */
void world_rng_seed(world_rng *r, uint16_t tick) { r->word[1] = tick; }

int32_t world_random(world_rng *r)
{
    static const int divisor[3] = {177,176,178};
    static const int multiplier[3] = {171,172,170};
    static const int quotient[3] = {2,35,63};
    static const unsigned modulus[3] = {30269,30307,30323};
    unsigned i, value = 0;
    for (i = 0; i < 3; ++i) {
        int seed = r->word[i] <= INT16_MAX ? (int)r->word[i] : (int)r->word[i] - 65536;
        uint16_t bits = (uint16_t)(multiplier[i] * (seed % divisor[i]) - quotient[i] * (seed / divisor[i]));
        int next = bits <= INT16_MAX ? (int)bits : (int)bits - 65536;
        if (next < 0) bits = (uint16_t)(bits + modulus[i]);
        r->word[i] = bits;
        value += ((unsigned)bits % modulus[i]) * 10000 / modulus[i];
    }
    return (int32_t)(value & 0x7FFFu);
}

int32_t world_rnd(world_rng *r, int32_t bound)
{
    return bound ? world_random(r) % bound : 0;
}
