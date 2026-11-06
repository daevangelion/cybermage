CC ?= cc
B ?= build/UI
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
LDLIBS += $(shell pkg-config --libs sdl2) -lm
UI_SOURCES := $(filter-out src/main.c,$(wildcard src/*.c src/*/*.c))

.PHONY: check
$(B)/test_ui: tests/test_ui.c $(UI_SOURCES) $(wildcard include/*.h)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_ui.c $(UI_SOURCES) $(LDLIBS)

check: $(B)/test_ui
	cd .. && port/$(B)/test_ui out/port/ui
	$(MAKE) -f tests/config.mk B=$(B) config-check
