include Makefile

.PHONY: check-projectiles
check-projectiles: $(B)/test_projectiles
	$(B)/test_projectiles

$(B)/test_projectiles: tests/test_projectiles.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
