#include "audio.h"

/* CM_HI.EXE OpenGLOBALFILES 0x219B6, PlaySpeechMessage 0x1786C. */
#define SPEECH_SCENES 12
#define SPEECH_HEADER (4 + SPEECH_SCENES * 4)
#define SPEECH_MESSAGE_SIZE 6
#define SPEECH_SEGMENT_SIZE 10

int audio_speech_entry(const uint8_t *data, uint32_t len, unsigned scene,
                       unsigned message, unsigned segment, const uint8_t **out,
                       uint32_t *length, int *link)
{
    uint32_t directory, groups, first, end, at, segments, offset;
    unsigned count, raw_link;
    const uint8_t *d, *s;
    if (!data || len < SPEECH_HEADER + SPEECH_MESSAGE_SIZE || scene >= SPEECH_SCENES ||
        !message || !segment || !out || !length || !link)
        return cm_fail("SPEECH.ENG", 0, "invalid speech request/header");
    directory = cm_u32(data);
    if (directory > len - SPEECH_HEADER || directory < SPEECH_MESSAGE_SIZE)
        return cm_fail("SPEECH.ENG", 0, "speech directory outside file");
    d = data + SPEECH_HEADER;
    groups = cm_u32(d);
    if (!groups || groups > directory || groups % SPEECH_MESSAGE_SIZE)
        return cm_fail("SPEECH.ENG", SPEECH_HEADER, "invalid speech message table");
    groups /= SPEECH_MESSAGE_SIZE;
    first = cm_u32(data + 4 + scene * 4);
    end = scene + 1 < SPEECH_SCENES ? cm_u32(data + 8 + scene * 4) : groups;
    if (first > end || end > groups || message > end - first)
        return cm_fail("SPEECH.ENG", 4 + scene * 4, "speech message outside scene");
    at = (first + message - 1) * SPEECH_MESSAGE_SIZE;
    segments = cm_u32(d + at); count = cm_u16(d + at + 4);
    if (segments < groups * SPEECH_MESSAGE_SIZE || segments > directory ||
        count > (directory - segments) / SPEECH_SEGMENT_SIZE || segment > count)
        return cm_fail("SPEECH.ENG", SPEECH_HEADER + at, "speech segment outside directory");
    at = segments + (segment - 1) * SPEECH_SEGMENT_SIZE;
    s = d + at; offset = cm_u32(s); *length = cm_u32(s + 4);
    raw_link = cm_u16(s + 8); *link = raw_link < 32768 ? (int)raw_link : (int)raw_link - 65536;
    if (offset < SPEECH_HEADER + directory)
        return cm_fail("SPEECH.ENG", SPEECH_HEADER + at, "speech PCM overlaps directory");
    if (*length > 0x20000)
        return cm_fail("SPEECH.ENG", SPEECH_HEADER + at + 4, "speech segment exceeds original buffer");
    return audio_speech_span(data, len, offset, *length, out);
}
