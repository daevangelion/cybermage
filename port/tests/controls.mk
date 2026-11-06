include Makefile

.PHONY: controls-check
controls-check: $(B)/test_controls $(B)/test_controls_platform $(B)/test_controls_touch $(B)/test_controls_sim $(B)/test_controls_map
	$(B)/test_controls
	$(B)/test_controls_platform
	$(B)/test_controls_touch
	$(B)/test_controls_sim
	$(B)/test_controls_map

$(B)/test_controls_touch: tests/test_controls_touch.c src/controls/actions.c src/controls/bindings.c src/platform_sdl.c $(AUDIO_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_controls_map: tests/test_controls_map.c tests/media_error.c src/controls/map.c src/controls/actions.c src/controls/bindings.c src/gfx.c
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm
