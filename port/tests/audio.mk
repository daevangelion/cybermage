CC ?= cc
B ?= build/audio
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
AUDIO_SRC := $(filter-out src/audio/game.c src/audio/movie.c,$(wildcard src/audio/*.c))
GAME_AUDIO_SRC := src/audio/game.c src/audio/movie.c src/rsc.c src/world/math.c src/platform_sdl.c
MEDIA_HEADERS := $(wildcard include/*.h src/audio/*.h)

.PHONY: all check check-game
all: $(B)/libcm_media.so

$(B)/test_sound: tests/test_sound.c $(AUDIO_SRC) $(GAME_AUDIO_SRC) $(MEDIA_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell pkg-config --cflags sdl2) $(LDFLAGS) -o $@ $(filter %.c,$^) $(shell pkg-config --libs sdl2) -lm

$(B)/test_audio: tests/test_audio.c $(AUDIO_SRC) $(MEDIA_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(filter %.c,$^)

$(B)/libcm_media.so: tests/media_error.c $(AUDIO_SRC) $(MEDIA_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIC -shared -o $@ $(filter %.c,$^)

check: $(B)/test_audio $(B)/libcm_media.so
	$(B)/test_audio
	$(abspath ../.venv)/bin/python tests/test_media.py $(abspath $(B)/libcm_media.so)

check-game: $(B)/test_sound
	$(abspath ../.venv)/bin/python ../tools/sound_oracle.py --fixture $(abspath $(B)/test_sound)
