include Makefile

.PHONY: controls-check
controls-check: $(B)/test_controls $(B)/test_controls_platform $(B)/test_controls_touch $(B)/test_controls_sim
	$(B)/test_controls
	$(B)/test_controls_platform
	$(B)/test_controls_touch
	$(B)/test_controls_sim

$(B)/test_controls: tests/test_controls.c tests/media_error.c src/controls/actions.c src/controls/bindings.c
	@mkdir -p $(@D)
	$(CC) -Iinclude $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

$(B)/test_controls_platform: tests/test_controls_platform.c src/controls/actions.c src/controls/bindings.c src/platform_sdl.c $(AUDIO_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_controls_touch: tests/test_controls_touch.c src/controls/actions.c src/controls/bindings.c src/platform_sdl.c $(AUDIO_CORE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_controls_sim: tests/test_controls_sim.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
