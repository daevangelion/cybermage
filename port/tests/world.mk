CC ?= cc
B ?= build/world
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
LDLIBS += $(shell pkg-config --libs sdl2) -lm
WORLD_SOURCES := $(wildcard src/world/*.c)
SOURCES := tests/test_world.c $(WORLD_SOURCES) src/rsc.c src/platform_sdl.c $(wildcard src/audio/*.c)
.PHONY: check
check: $(B)/test_world
	$(B)/test_world
	$(B)/test_world scenes ../orig/retail
	../.venv/bin/python ../tools/world_original.py $(B)/test_world
$(B)/test_world: $(SOURCES) include/world.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)
