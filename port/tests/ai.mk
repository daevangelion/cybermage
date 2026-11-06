CC ?= cc
B ?= build/AI
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror -O2
SOURCES := $(filter-out src/main.c,$(wildcard src/*.c src/*/*.c))
OBJECTS := $(patsubst src/%.c,$(B)/%.o,$(SOURCES))
LDLIBS += $(shell pkg-config --libs sdl2) -lm

.PHONY: check
check: $(B)/test_ai
	$(B)/test_ai ../orig/retail/SCENE.RSC ../orig/retail
	$(abspath ../.venv/bin/python) tests/test_target_trace.py

$(B)/test_ai: tests/test_ai.c $(OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/ai_trace: ../tools/ai_trace.c $(OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wl,--wrap=ai_monster_blocked -o $@ $^ $(LDLIBS)

$(B)/%.o: src/%.c $(wildcard include/*.h)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(OBJECTS:.o=.d)
