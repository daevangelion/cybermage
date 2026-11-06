include Makefile

.PHONY: check-projectiles
check-projectiles: $(B)/test_projectiles
	$(B)/test_projectiles
	$(abspath ../.venv/bin/python) tests/test_projectile_trace.py

.PHONY: check-melee-distance
check-melee-distance: $(B)/test_melee_distance
	$(abspath ../.venv/bin/python) ../tools/melee_distance_original.py $(abspath $(B)/test_melee_distance)

$(B)/test_projectiles: tests/test_projectiles.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/test_melee_distance: tests/test_melee_distance.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wl,--wrap=ai_sight_person -Wl,--wrap=ai_move_object -Wl,--wrap=ai_track_target -Wl,--wrap=ai_update_pc_object -o $@ $^ $(LDLIBS)
