#include "tgv.h"
#include <string.h>

static int bad(const tgv *m, uint32_t at, const char *why)
{
    return cm_fail(m->file, at, why);
}

static int unpack(const tgv *m, const uint8_t *s, uint32_t n,
                  uint8_t *out, uint32_t want, uint32_t at)
{
    uint32_t p, d = 0, declared;
    if (n < 5 || (s[0] & 0xfe) != 0x10 || s[1] != 0xfb)
        return bad(m, at, "invalid RefPack header");
    p = s[0] & 1 ? 5 : 2;
    if (p + 3 > n) return bad(m, at, "truncated RefPack size");
    declared = (uint32_t)s[p] << 16 | (uint32_t)s[p + 1] << 8 | s[p + 2];
    p += 3;
    if (declared != want) return bad(m, at, "RefPack size differs from frame dimensions");
    while (d < want) {
        unsigned c, literal, run = 0, distance = 0;
        if (p == n) return bad(m, at + p, "truncated RefPack control");
        c = s[p++]; literal = c & 3;
        if (c < 0x80) {
            if (p == n) return bad(m, at + p, "truncated short backreference");
            distance = ((c & 0x60) << 3) + s[p++] + 1;
            run = ((c & 0x1c) >> 2) + 3;
        } else if (c < 0xc0) {
            if (n - p < 2) return bad(m, at + p, "truncated medium backreference");
            literal = s[p] >> 6;
            distance = ((s[p] & 0x3f) << 8) + s[p + 1] + 1;
            run = (c & 0x3f) + 4; p += 2;
        } else if (c < 0xe0) {
            if (n - p < 3) return bad(m, at + p, "truncated long backreference");
            distance = ((c & 0x10) << 12) + ((unsigned)s[p] << 8) + s[p + 1] + 1;
            run = ((c & 0x0c) << 6) + s[p + 2] + 5; p += 3;
        } else if (c < 0xfc) literal = ((c & 0x1f) + 1) * 4;
        if (literal > n - p || literal > want - d)
            return bad(m, at + p, "RefPack literal outside frame");
        memcpy(out + d, s + p, literal); d += literal; p += literal;
        if (run > want - d || (run && distance > d))
            return bad(m, at + p, "RefPack backreference outside frame");
        while (run--) { out[d] = out[d - distance]; ++d; }
        if (c >= 0xfc && d != want)
            return bad(m, at + p, "early RefPack terminator");
    }
    return 0;
}

/* Random-access LSB-first fields avoid allocating motion/codebook tables. */
static uint32_t bits(const uint8_t *p, uint32_t bit, unsigned n)
{
    uint32_t value = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        value |= (uint32_t)((p[(bit + i) / 8] >> ((bit + i) & 7)) & 1) << i;
    return value;
}

static int inter(tgv *m, const uint8_t *p, uint32_t n, uint32_t at)
{
    unsigned nmv, nraw, npacked, vb, x, y, k;
    uint32_t raw_at, packed_at, indices, needed;
    uint8_t *out;
    const uint8_t *prev;
    if (!m->width || n < 12) return bad(m, at, "inter frame without key frame");
    nmv = cm_u16(p); nraw = cm_u16(p + 2); npacked = cm_u16(p + 4); vb = cm_u16(p + 6);
    if (!vb || vb > 25) return bad(m, at + 6, "invalid block index width");
    raw_at = 12 + ((nmv * 20u + 31) & ~31u) / 8;
    packed_at = raw_at + nraw * 16u;
    indices = npacked * 64u;
    needed = indices + (m->width / 4) * (m->height / 4) * vb;
    if (packed_at > n || (needed + 7) / 8 > n - packed_at)
        return bad(m, at, "truncated TGV codebooks or indices");
    prev = m->pixels[m->current];
    out = m->pixels[m->current ^ 1];
    for (y = 0; y < m->height; y += 4) for (x = 0; x < m->width; x += 4) {
        uint32_t v = bits(p + packed_at, indices, vb);
        indices += vb;
        if (v < nmv) {
            int dx = (int)bits(p + 12, v * 20, 10);
            int dy = (int)bits(p + 12, v * 20 + 10, 10);
            if (dx & 512) dx -= 1024;
            if (dy & 512) dy -= 1024;
            dx += (int)x; dy += (int)y;
            if (dx < 0 || dy < 0 || (unsigned)dx + 4 > m->width || (unsigned)dy + 4 > m->height)
                return bad(m, at, "motion vector outside preceding frame");
            for (k = 0; k < 4; ++k)
                memcpy(out + (y + k) * m->width + x, prev + ((unsigned)dy + k) * m->width + (unsigned)dx, 4);
        } else {
            v -= nmv;
            if (v >= nraw + npacked) return bad(m, at, "block index outside codebooks");
            for (k = 0; k < 16; ++k) {
                uint8_t colour;
                if (v < nraw) colour = p[raw_at + v * 16 + k];
                else {
                    uint32_t b = (v - nraw) * 64;
                    unsigned c = bits(p + packed_at, b + 32 + (15 - k) * 2, 2);
                    colour = (uint8_t)bits(p + packed_at, b + c * 8, 8);
                }
                out[(y + k / 4) * m->width + x + k % 4] = colour;
            }
        }
    }
    m->current ^= 1;
    return 0;
}

int tgv_open(tgv *m, const uint8_t *data, uint32_t len, const char *file)
{
    if (!m || !data || !file || len < 8) return cm_fail(file ? file : "TGV", 0, "empty TGV movie");
    memset(m, 0, sizeof *m);
    m->data = data; m->len = len; m->file = file;
    return 0;
}

const uint8_t *tgv_pixels(const tgv *m) { return m->pixels[m->current]; }

int tgv_next(tgv *m)
{
    while (m->pos < m->len) {
        const uint8_t *tag = m->data + m->pos, *p;
        uint32_t n, at = m->pos + 8;
        if (m->len - m->pos < 8) return bad(m, m->pos, "truncated TGV chunk header");
        n = cm_u32(tag + 4);
        if (n < 8 || n > m->len - m->pos) return bad(m, m->pos + 4, "TGV chunk outside file");
        p = tag + 8; m->pos += n; n -= 8;
        if (!memcmp(tag, "kVGT", 4)) {
            unsigned w, h, colours;
            uint32_t start;
            if (n < 12) return bad(m, at, "truncated key frame header");
            w = cm_u16(p); h = cm_u16(p + 2); colours = cm_u16(p + 6);
            if (!w || !h || w > TGV_WIDTH_MAX || h > TGV_HEIGHT_MAX || (w & 3) || (h & 3) || colours > 256)
                return bad(m, at, "unsupported TGV dimensions or palette");
            start = 12 + colours * 3;
            if (start > n) return bad(m, at, "truncated TGV palette");
            if (unpack(m, p + start, n - start, m->pixels[m->current ^ 1], w * h, at + start)) return -1;
            m->width = w; m->height = h; m->current ^= 1;
            memcpy(m->palette, p + 12, colours * 3);
            return TGV_FRAME;
        }
        if (!memcmp(tag, "fVGT", 4)) {
            if (inter(m, p, n, at)) return -1;
            return TGV_FRAME;
        }
        if (!memcmp(tag, "1SNh", 4)) {
            unsigned rate, channels;
            if (n < 32 || memcmp(p, "EACS", 4)) return bad(m, at, "invalid EACS header");
            rate = cm_u32(p + 4); channels = p[9];
            if (rate < 4000 || rate > 192000 || p[8] != 1 ||
                (channels != 1 && channels != 2) || p[10] != 0)
                return bad(m, at + 8, "unsupported EACS audio format");
            m->audio_rate = rate; m->audio_channels = channels;
            /* Some EACS headers append initial PCM after the 32-byte header. */
            p += 32; n -= 32;
            if (!n) continue;
        } else if (!memcmp(tag, "1SNd", 4)) {
            if (!m->audio_rate) return bad(m, at, "PCM chunk without EACS header");
        } else if (!memcmp(tag, "1SNe", 4)) continue;
        else return bad(m, at - 8, "unknown TGV chunk tag");
        if (n % m->audio_channels) return bad(m, at, "partial EACS sample frame");
        m->audio = p; m->audio_len = n;
        return TGV_AUDIO;
    }
    return TGV_END;
}
