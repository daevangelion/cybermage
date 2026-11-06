CC ?= cc
B ?= build/Events
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror -O2 -g
LDLIBS += $(shell pkg-config --libs sdl2) -lm
SOURCES := $(filter-out src/main.c,$(wildcard src/*.c src/*/*.c))

.PHONY: check
check: $(B)/test_events
	$(B)/test_events

$(B)/test_events: tests/test_events.c $(SOURCES) $(wildcard include/*.h)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_events.c $(SOURCES) $(LDLIBS)
