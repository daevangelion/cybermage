include Makefile

.PHONY: sim-phases-check
sim-phases-check: $(B)/test_sim_phases
	$(B)/test_sim_phases ../orig/retail cap
	$(B)/test_sim_phases ../orig/retail hit
	$(B)/test_sim_phases ../orig/retail vehicle

$(B)/test_sim_phases: tests/test_sim_phases.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
