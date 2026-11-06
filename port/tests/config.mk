include Makefile

.PHONY: config-check
config-check: $(B)/test_ui_config
	$(B)/test_ui_config ../orig/retail/CMDATA.CNF $(B)/config-user

$(B)/test_ui_config: tests/test_ui_config.c src/main.c $(filter-out $(B)/main.o,$(OBJECTS))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_ui_config.c $(filter-out $(B)/main.o,$(OBJECTS)) $(LDLIBS)
