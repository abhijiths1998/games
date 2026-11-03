# Port Calder

An open-city driving sandbox. Port Calder is a procedurally built island city of 576 blocks (about 1.5 km across, nearly 2 km including beaches and docks) with traffic, pedestrians, police, a day/night cycle, weather and radio. Steal any car, take courier jobs and taxi fares, race through the streets, hit the stunt ramps and hunt for lost tapes.

Native Linux, C11 + SDL2 + OpenGL 3.3. There are no asset files: the city, the cars, the people, the sounds and five radio stations are all generated when the game starts (about 2 s).

## Build and run

```sh
make run          # build and play
make install      # optional: installs to ~/.local (binary + desktop entry)
```

Needs SDL2 and OpenGL development files and a GPU with OpenGL 3.3.

## Controls

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Walk / drive | WASD or arrows | left stick, RT/LT for throttle/brake |
| Camera | mouse, wheel to zoom, C to cycle distance | right stick |
| Enter / exit / carjack | F or Enter | Y |
| Handbrake / jump | Space | A |
| Sprint | Shift | B |
| Horn | H | X |
| Radio station | Q / E | LB / RB |
| Taxi duty (in a taxi) | T | D-pad up |
| Map | Tab | Back |
| Pause, settings, stats | Esc | Start |
| Fullscreen / mute | F11 / M | |

## The city

- **Districts:**
  - Calder Center: glass towers and the Calder Needle observation tower.
  - Midtown, with the Calder Bowl stadium.
  - Old Quarter.
  - Maple Heights: houses and gardens.
  - Calder Park, with its lake.
  - Ironside Docks: cranes, container piers and a cargo ship.
  - Eastgate.
  - Seabright, plus the Sunset Strand beach with its pier, Ferris wheel and lighthouse.
- **Traffic:** cars follow lanes and obey traffic lights. They honk when you block them and panic when you hit them.
- **Pedestrians:** they walk the sidewalks, cross streets, flee from danger and get knocked over (no gore).
- **Time and weather:** a full day lasts 16 minutes. At night, street lamps, windows, neon and headlights light the city, and rain fronts come and go.

## Shops

Shops show on the map with a letter. Walk up to the door, or drive into the bay for gas and the auto shop, and press F. Most shops keep real opening hours, and none will serve you while the police are after you.

| Shop | Hours | What you can buy |
|---|---|---|
| Gas station (G) | 24 h | fuel at $1.75/L, a quick patch-up; snacks at the kiosk on foot |
| 24/7 store (M) | 24 h | water, sandwiches, energy drinks (unlimited sprint), first aid kits, lottery scratch cards |
| Diner (D) | 06-23 | coffee, milkshakes, burger meals, full breakfast |
| Clothing (C) | 09-21 | seven outfits: buy once, change any time |
| Car dealer (V) | 09-20 | buy your own car: sedan, pickup, van, taxi, muscle car or coupe |
| Auto shop (A) | 08-20 | repairs, paint, and upgrades for your own car (engine stages 1-3, brakes, tires, armor) |

Your own car is marked on the map. If it is wrecked or sinks, it gets towed back to the safehouse.

## Real-life details

- **Fuel:** cars burn fuel as you drive. When the tank runs dry the engine dies, so watch the fuel gauge.
- **Stamina:** sprinting tires you out, and an energy drink or coffee keeps you going.
- **Traffic laws:** patrol cars are part of normal traffic. Speeding past one or running a red light in front of one earns a wanted star.
- **Traffic:** cars use turn signals and stop for pedestrians.
- **Pedestrians:** people wait at the kerb for the walk signal before crossing.
- **Streets:** benches, bins, hydrants, planters with trees, bus shelters, mailboxes and street-level business signs (cafes, pharmacies, barbers, banks and more).
- **Road surfaces:** wheel-worn lanes, cracks, patches and manholes.

## Things to do

- **Courier jobs** (yellow J): drive into the marker in any vehicle, then deliver the package across town before the timer runs out.
- **Taxi fares:** get in a taxi and press T. Pick up the waving fare, drop them off at the marker, and keep your streak going for bigger tips.
- **Street races** (white X): four checkpoint routes with gold, silver and bronze times. Your best times are saved.
- **Stunt jumps:** ten ramps on the beach, in the park, at the docks and around the edge of town. Long air time pays even without a ramp.
- **Lost tapes:** 30 glowing cassettes are hidden in alleys, yards, beaches and piers. Find them all for a bonus.
- **Wanted level:** carjacking, hitting people, ramming police and wrecking cars raise your stars.
  - Police cars chase and ram you, and officers chase you on foot.
  - At three stars come roadblocks; at four, a helicopter with a searchlight.
  - Break line of sight to make the stars fade. A respray shop (orange R) clears them for $100.
  - If you stop with police next to you, you're **busted**. Lose all your health and you're **wasted**. Either way you lose 10% of your cash.
- **Safehouse** (green S): walk in to save your progress and heal. Hospitals (red H) also patch you up.

The pause screen shows city completion, stats and settings (mouse sensitivity, field of view, invert Y). The save file is `~/.local/share/doomedl/port-calder/save.txt`.

## Self-test

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./port-calder --selftest /tmp/shots
```

This runs headless. It compiles the shaders, runs the audio self-test, checks that the world is valid (lanes clear, points of interest reachable, race routes), and soaks the traffic for four simulated minutes. An autopilot then does a courier job, a race and a stunt jump, gets the police to bust it and blows up a car. The shop tests buy food, change clothes, run a tank dry and refuel, buy a car at the dealer, fit an engine upgrade and check that a patrol car notices speeding. Screenshots are written to the directory. It never writes a save file.

## Source

| File | Contents |
|---|---|
| `src/gfx.c` | math, GLSL (PBR, procedural sky/clouds, city ground and ocean shaders), instanced renderer, bloom/tonemap, HUD primitives |
| `src/world.c` | terrain, districts, buildings and landmarks, collision grid, road graph |
| `src/actors.c` | particles, car physics, traffic and police AI, pedestrians, breakable street lamps |
| `src/game.c` | player, wanted level, missions and activities, HUD, minimap and map, save/load |
| `src/main.c` | scene assembly, camera, input, main loop, self-test |
| `src/audio.c` | software mixer, synthesized effects, engine/siren/ambience layers, five radio stations |
