CC ?= cc
B ?= build/audio
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
AUDIO_SRC := $(wildcard src/audio/*.c)

.PHONY: all check
all: $(B)/libcm_media.so

$(B)/test_audio: tests/test_audio.c $(AUDIO_SRC)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(B)/libcm_media.so: tests/media_error.c $(AUDIO_SRC)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIC -shared -o $@ $^

check: $(B)/test_audio $(B)/libcm_media.so
	$(B)/test_audio
	$(abspath ../.venv)/bin/python tests/test_media.py $(abspath $(B)/libcm_media.so)
