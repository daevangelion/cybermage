CC ?= cc
B ?= build/GL
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
LDLIBS += $(shell pkg-config --libs sdl2) -lm
SOURCES := $(wildcard src/render/*.c src/render_gl/*.c src/world/*.c src/controls/*.c) src/exe.c src/gfx.c src/rsc.c src/platform_sdl.c src/audio/mixer.c src/audio/music.c src/audio/synth.c src/audio/opl3.c
OBJECTS := $(patsubst src/%.c,$(B)/%.o,$(SOURCES))
HEADERS := $(wildcard include/*.h src/render_gl/*.h)

.PHONY: scene check
scene: $(B)/render_gl_scene
$(B)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<
$(B)/render_gl_scene: tests/render_gl_scene.c $(OBJECTS) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/render_gl_scene.c $(OBJECTS) $(LDLIBS)
check: $(B)/test_render_gl
	$(B)/test_render_gl
$(B)/test_render_gl: tests/test_render_gl.c src/gfx.c src/render_gl/mesh.c src/render_gl/texture.c tests/media_error.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) $(LDFLAGS) -o $@ tests/test_render_gl.c src/gfx.c src/render_gl/mesh.c src/render_gl/texture.c tests/media_error.c
-include $(OBJECTS:.o=.d)
