CC ?= cc
B ?= build/render-test
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
LDLIBS += $(shell pkg-config --libs sdl2) -lm
SOURCES := $(wildcard src/render/*.c src/vis/*.c src/target/*.c) src/audio/mixer.c src/audio/music.c src/audio/synth.c src/audio/opl3.c src/world/math.c src/rsc.c src/platform_sdl.c
SCENE_SOURCES := $(filter-out src/main.c,$(wildcard src/*.c src/*/*.c))
HEADERS := $(wildcard include/*.h src/render/*.h src/vis/*.h)

.PHONY: check scene
check: $(B)/test_render
	$(B)/test_render
$(B)/test_render: tests/test_render.c $(SOURCES) $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_render.c $(SOURCES) $(LDLIBS)
scene: $(B)/render_scene
$(B)/render_scene: tests/render_scene.c $(SCENE_SOURCES) $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/render_scene.c $(SCENE_SOURCES) $(LDLIBS)
