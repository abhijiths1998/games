GAMES := bramble-bound orbit-golf prism-smash lumen-drift vintage/relic-runner vintage/ember-isle vintage/nova-lancer vintage/deep-delve

all:
	@for g in $(GAMES); do $(MAKE) -C $$g || exit 1; done

clean:
	@for g in $(GAMES); do $(MAKE) -C $$g clean || exit 1; done

.PHONY: all clean
