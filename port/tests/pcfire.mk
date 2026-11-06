.PHONY: pc-fire-check
pc-fire-check: $(B)/test_pc_fire
	$(B)/test_pc_fire ../orig/retail/CM_HI.EXE
	$(abspath ../.venv/bin/python) ../tools/pc_fire_original.py $(B)/test_pc_fire

$(B)/test_pc_fire: tests/test_pc_fire.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
