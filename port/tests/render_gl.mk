CC ?= cc
B ?= build/GL
CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2)
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
LDLIBS += $(shell pkg-config --libs sdl2) -lm
SOURCES := $(filter-out src/main.c,$(wildcard src/*.c src/*/*.c))
OBJECTS := $(patsubst src/%.c,$(B)/%.o,$(SOURCES))
HEADERS := $(wildcard include/*.h src/render_gl/*.h)

.PHONY: scene scene-es check check-gl check-gl-es
scene: $(B)/render_gl_scene
scene-es: $(B)/render_gl_scene_es
$(B)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<
$(B)/render_gl_scene: tests/render_gl_scene.c $(OBJECTS) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/render_gl_scene.c $(OBJECTS) $(LDLIBS)
$(B)/platform_sdl_es.o: src/platform_sdl.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCM_OPENGL_ES -c -o $@ $<
$(B)/render_gl_scene_es: tests/render_gl_scene.c $(filter-out $(B)/platform_sdl.o,$(OBJECTS)) $(B)/platform_sdl_es.o $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/render_gl_scene.c $(filter-out $(B)/platform_sdl.o,$(OBJECTS)) $(B)/platform_sdl_es.o $(LDLIBS)
$(B)/test_render_gl_runtime: tests/test_render_gl_runtime.c $(OBJECTS) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_render_gl_runtime.c $(OBJECTS) $(LDLIBS)
$(B)/test_render_gl_runtime_es: tests/test_render_gl_runtime.c $(filter-out $(B)/platform_sdl.o,$(OBJECTS)) $(B)/platform_sdl_es.o $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_render_gl_runtime.c $(filter-out $(B)/platform_sdl.o,$(OBJECTS)) $(B)/platform_sdl_es.o $(LDLIBS)
check-gl: $(B)/test_render_gl_runtime
	$(B)/test_render_gl_runtime
check-gl-es: $(B)/test_render_gl_runtime_es
	$(B)/test_render_gl_runtime_es
check: $(B)/test_render_gl
	$(B)/test_render_gl
$(B)/test_render_gl: tests/test_render_gl.c src/gfx.c src/render_gl/mesh.c src/render_gl/texture.c tests/media_error.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) $(LDFLAGS) -o $@ tests/test_render_gl.c src/gfx.c src/render_gl/mesh.c src/render_gl/texture.c tests/media_error.c
-include $(OBJECTS:.o=.d)
