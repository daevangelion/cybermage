CC ?= cc
B ?= build
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
LDLIBS += $(shell pkg-config --libs sdl2) -lm
SOURCES := $(wildcard src/*.c src/*/*.c)
OBJECTS := $(patsubst src/%.c,$(B)/%.o,$(SOURCES))
CORE_SOURCES := src/gfx.c src/rsc.c src/platform_sdl.c $(wildcard src/audio/*.c)

.PHONY: all check
all: $(B)/cybermage

$(B)/cybermage: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

check: $(B)/test_core $(B)/test_platform
	$(B)/test_core
	$(B)/test_platform $(B)/platform-keys.txt $(B)/platform-dump
	$(abspath ../.venv/bin/python) ../tools/test_rsc.py
	$(MAKE) -f tests/audio.mk B=$(B)/audio check

$(B)/test_core: tests/test_core.c $(CORE_SOURCES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_platform: tests/test_platform.c src/platform_sdl.c $(wildcard src/audio/*.c)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

-include $(OBJECTS:.o=.d)
