CC ?= cc
JAVA ?= /usr/lib/jvm/java-21-openjdk/bin/java
JAVAC ?= /usr/lib/jvm/java-21-openjdk/bin/javac
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

check: $(B)/test_core $(B)/test_platform $(B)/test_platform_lifecycle $(B)/test_touch $(B)/test_sim $(B)/test_replay $(B)/test_exe $(B)/test_sim_loop $(B)/test_sim_collision $(B)/test_sim_swim $(B)/test_sim_swim_ubsan $(B)/test_sim_swim_original $(B)/test_sim_fire $(B)/test_sim_fire_ubsan $(B)/test_attack_ignore
	$(B)/test_core
	$(B)/test_platform $(B)/platform-keys.txt $(B)/platform-dump
	$(B)/test_platform_lifecycle
	$(B)/test_touch
	$(B)/test_sim
	$(B)/test_replay
	$(B)/test_exe
	$(B)/test_sim_loop $(B)/loop-user
	$(B)/test_sim_collision
	$(B)/test_sim_swim ../orig/retail
	$(B)/test_sim_swim_ubsan ../orig/retail
	$(abspath ../.venv/bin/python) ../tools/sim_swim_original.py $(B)/test_sim_swim_original
	$(abspath ../.venv/bin/python) ../tools/sim_swim_original.py $(B)/test_sim_swim_original 7
	$(abspath ../.venv/bin/python) tests/test_swim_trace.py
	$(abspath ../.venv/bin/python) tests/test_cutovers.py
	$(abspath ../.venv/bin/python) tests/test_deviations.py
	$(MAKE) -f tests/controls.mk B=$(B) controls-check
	$(MAKE) -f tests/sim_phases.mk B=$(B) sim-phases-check
	$(abspath ../.venv/bin/python) ../tools/sim_fire_original.py $(B)/test_sim_fire
	$(B)/test_sim_fire_ubsan 0 0 0
	$(abspath ../.venv/bin/python) ../tools/attack_ignore_original.py $(B)/test_attack_ignore
	$(abspath ../.venv/bin/python) ../tools/test_rsc.py
	$(MAKE) -f tests/audio.mk B=$(B)/audio check
	$(MAKE) -f tests/world.mk B=$(B)/world check
	$(JAVA) -cp $(B)/java com.jshield.cybermage.TestDataImport

$(B)/test_core: tests/test_core.c $(CORE_SOURCES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_platform: tests/test_platform.c src/platform_sdl.c $(AUDIO_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_touch: tests/test_touch.c src/platform_sdl.c $(AUDIO_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_platform_lifecycle: tests/test_platform_lifecycle.c src/platform_sdl.c $(AUDIO_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(AUDIO_CORE) $(LDLIBS)

$(B)/test_sim: tests/test_sim.c tests/media_error.c src/sim/input.c src/sim/flat.c src/sim/fire_flat.c src/sim/save.c
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) $(LDFLAGS) -o $@ $^

$(B)/test_exe: tests/test_exe.c tests/media_error.c src/exe.c
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) $(LDFLAGS) -o $@ $^

$(B)/test_attack_ignore: tests/test_attack_ignore.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_replay: tests/test_replay.c tests/media_error.c src/sim/replay.c
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections $(LDFLAGS) -o $@ $^ -lm

$(B)/test_sim_loop: tests/test_sim_loop.c src/main.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(filter %.o,$^) $(LDLIBS)

$(B)/test_sim_collision: tests/test_sim_collision.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_sim_swim: tests/test_sim_swim.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_sim_swim_ubsan: tests/test_sim_swim.c src/sim/frame.c $(filter-out $(B)/main.o $(B)/sim/frame.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -fsanitize=undefined -fno-sanitize-recover=undefined $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_sim_swim_original: tests/test_sim_swim_original.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_sim_fire: tests/test_sim_fire.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_sim_fire_ubsan: tests/test_sim_fire.c tests/media_error.c src/sim/fire.c src/sim/fire_flat.c src/sim/slide.c src/world/math.c src/world/sight.c
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) -O1 -fsanitize=undefined -fno-sanitize-recover=undefined -ffunction-sections -fdata-sections -Wl,--gc-sections $(LDFLAGS) -o $@ $^ -lm

$(B)/test_controls: tests/test_controls.c tests/media_error.c $(CONTROLS_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_controls_platform: tests/test_controls_platform.c src/platform_sdl.c $(AUDIO_CORE) $(CONTROLS_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_controls_sim: tests/test_controls_sim.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

include tests/pcfire.mk
check: pc-fire-check

check: $(B)/java/com/jshield/cybermage/TestDataImport.class

$(B)/java/com/jshield/cybermage/TestDataImport.class: tests/TestDataImport.java android/app/src/main/java/com/jshield/cybermage/DataImport.java
	@mkdir -p $(B)/java
	$(JAVAC) -d $(B)/java $^

-include $(OBJECTS:.o=.d)
