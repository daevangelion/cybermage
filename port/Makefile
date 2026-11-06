CC ?= cc
B ?= build
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
LDLIBS += $(shell pkg-config --libs sdl2)
SOURCES := $(wildcard src/*.c src/*/*.c)
OBJECTS := $(patsubst src/%.c,$(B)/%.o,$(SOURCES))

.PHONY: all check
all: $(B)/cybermage

$(B)/cybermage: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

check: $(B)/test_core
	$(B)/test_core
	../.venv/bin/python ../tools/test_rsc.py

$(B)/test_core: tests/test_core.c src/rsc.c src/gfx.c src/platform_sdl.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

-include $(OBJECTS:.o=.d)
