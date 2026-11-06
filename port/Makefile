CC ?= cc
B ?= build
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
LDLIBS += $(shell pkg-config --libs sdl2) -lm
SOURCES := $(wildcard src/*.c src/*/*.c)
OBJECTS := $(patsubst src/%.c,$(B)/%.o,$(SOURCES))
AUDIO_CORE := src/audio/mixer.c src/audio/music.c src/audio/synth.c src/audio/opl3.c
CONTROLS_CORE := src/controls/actions.c src/controls/bindings.c
CORE_SOURCES := src/gfx.c src/rsc.c src/exe.c src/platform_sdl.c $(AUDIO_CORE)

.PHONY: all check
all: $(B)/cybermage

$(B)/cybermage: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

check: $(B)/test_core $(B)/test_platform $(B)/test_touch $(B)/test_sim $(B)/test_exe $(B)/test_sim_loop $(B)/test_sim_fire
	$(B)/test_core
	$(B)/test_platform $(B)/platform-keys.txt $(B)/platform-dump
	$(B)/test_touch
	$(B)/test_sim
	$(B)/test_exe
	$(B)/test_sim_loop $(B)/loop-user
	$(MAKE) -f tests/controls.mk B=$(B) controls-check
	$(abspath ../.venv/bin/python) ../tools/sim_fire_original.py $(B)/test_sim_fire
	$(abspath ../.venv/bin/python) ../tools/test_rsc.py
	$(MAKE) -f tests/audio.mk B=$(B)/audio check
	$(MAKE) -f tests/world.mk B=$(B)/world check

$(B)/test_core: tests/test_core.c $(CORE_SOURCES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_platform: tests/test_platform.c src/platform_sdl.c $(AUDIO_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_touch: tests/test_touch.c src/platform_sdl.c $(AUDIO_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_sim: tests/test_sim.c tests/media_error.c src/sim/input.c src/sim/flat.c src/sim/fire_flat.c src/sim/save.c
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) $(LDFLAGS) -o $@ $^

$(B)/test_exe: tests/test_exe.c tests/media_error.c src/exe.c
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) $(LDFLAGS) -o $@ $^

$(B)/test_sim_loop: tests/test_sim_loop.c src/main.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(filter %.o,$^) $(LDLIBS)

$(B)/test_sim_fire: tests/test_sim_fire.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_controls: tests/test_controls.c tests/media_error.c $(CONTROLS_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_controls_platform: tests/test_controls_platform.c src/platform_sdl.c $(AUDIO_CORE) $(CONTROLS_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_controls_sim: tests/test_controls_sim.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

-include $(OBJECTS:.o=.d)
