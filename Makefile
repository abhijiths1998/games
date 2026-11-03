GAMES := bramble-bound orbit-golf prism-smash lumen-drift

all:
	@for g in $(GAMES); do $(MAKE) -C $$g || exit 1; done

clean:
	@for g in $(GAMES); do $(MAKE) -C $$g clean || exit 1; done

.PHONY: all clean
