# Port Calder

An open-world California road trip. The whole state is scaled down to about 8 by 9 km: the Pacific coast from the Oregon line to Mexico, the Sierra Nevada, the Central Valley, the deserts, and 19 real cities joined by real highways. It has traffic, pedestrians, police, a day/night cycle, weather and radio. Steal any car, drive the Pacific Coast Highway, check into hotels, eat your way through the restaurants, take courier jobs and taxi fares, race, hit the stunt ramps and hunt for lost tapes.

Place names are real. Every business, hotel and restaurant is invented.

Native Linux, C11 + SDL2 + OpenGL 3.3. There are no asset files: the city, the cars, the people, the sounds and five radio stations are all generated when the game starts (a few seconds).

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

## The map

- **Cities and neighbourhoods:**
  - Los Angeles: Santa Monica (with a pier and Ferris wheel), Venice, Beverly Hills, Hollywood, Koreatown, Downtown LA, Silver Lake, Pasadena, Boyle Heights and the Long Beach port.
  - San Francisco: the Presidio, Richmond, Golden Gate Park, the Haight, the Marina, Fisherman's Wharf, North Beach, Chinatown, the Financial District, SoMa and the Mission.
  - San Diego, Oakland, Sacramento, Fresno, Bakersfield, Santa Barbara, San Luis Obispo, Monterey, Santa Cruz, Palm Springs, Redding, Eureka, Napa, South Lake Tahoe, Barstow, Furnace Creek and Yosemite Village.
- **Highways:** Interstate 5, US 101, the Pacific Coast Highway through Big Sur, Interstate 80 over the mountains to Tahoe, Interstates 10 and 15 into the desert, and Highways 99, 58, 41 and more. The Golden Gate and Bay bridges span the bay. Roadside stops along the way include the Grapevine, Kettleman City, Baker, Big Sur and Mount Shasta.
- **Landscape:**
  - Mountains: the Sierra Nevada with snowy peaks, Mount Shasta, and Yosemite Valley with its granite dome and waterfall.
  - Water and desert: Lake Tahoe, the Salton Sea, the salt flats and dunes of Death Valley, and Joshua Tree.
  - Farmland and coast: Central Valley fields and orchards, Napa vineyards, redwood forests and golden coastal hills.
- **Buildings by region:**
  - Cities: glass towers downtown, Spanish stucco with red tile roofs in the south, painted Victorian rows in San Francisco and Eureka, and beach houses and mansions.
  - Desert and mountains: adobe in the desert and log cabins in the mountains.
  - Elsewhere: farms with barns and silos, a wind farm in the San Gorgonio Pass and oil pumpjacks near Bakersfield.
- **Traffic:**
  - Cars follow lanes and use turn signals.
  - In the cities they obey traffic lights.
  - On the highways they drive two lanes each way at highway speed.
- **Pedestrians:** they walk the sidewalks, wait for the walk signal, flee from danger and get knocked over (no gore).
- **Time and weather:** a full day lasts 16 minutes. At night, street lamps, windows, neon and headlights light the cities, and rain fronts come and go.

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
| Hotel or motel (H) | 24 h | a night's sleep until 08:00 (saves the game), room service, minibar |
| Restaurant (R) | 11-23 | menus by cuisine: tacos and burritos, fish tacos and chowder, dumplings, pizza, tri-tip and steak, burgers, Korean BBQ, brisket, cafes, a vineyard tasting menu |

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

- **Courier jobs** (yellow J): drive into the marker in any vehicle, then deliver the package across town or to a nearby city before the timer runs out.
- **Taxi fares:** get in a taxi and press T. Pick up the waving fare, drop them off at the marker, and keep your streak going for bigger tips.
- **Street races** (white X): five routes with gold, silver and bronze times (downtown LA, San Francisco, San Diego, the Big Sur coast and a desert highway sprint). Your best times are saved.
- **Stunt jumps:** twelve ramps in the deserts, on the beaches and in the hills. Long air time pays even without a ramp.
- **Lost tapes:** 30 glowing cassettes are hidden in cities and out in the wilds, all within reach of a road. Find them all for a bonus.
- **Wanted level:** carjacking, hitting people, ramming police and wrecking cars raise your stars.
  - Police cars chase and ram you, and officers chase you on foot.
  - At three stars come roadblocks; at four, a helicopter with a searchlight.
  - Break line of sight to make the stars fade. A respray shop (orange R) clears them for $100.
  - If you stop with police next to you, you're **busted**. Lose all your health and you're **wasted**. Either way you lose 10% of your cash.
- **Safehouses** (green S) in Los Angeles, San Francisco, San Diego and Sacramento: walk in to save your progress and heal. Hotels also save, and hospitals patch you up.

The pause screen shows completion, stats and settings (mouse sensitivity, field of view, invert Y). The save file is `~/.local/share/doomedl/port-calder/save.txt`.

## Self-test

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./port-calder --selftest /tmp/shots
```

This runs headless. It compiles the shaders, runs the audio self-test, checks that the world is valid (every city connected by road, lanes clear, no road under water, every point of interest and business entrance reachable), soaks the city traffic for four simulated minutes and checks highway traffic flows. An autopilot then drives from Los Angeles to Santa Barbara, does a courier job, a race and a stunt jump, gets the police to bust it and blows up a car. The shop tests order at a restaurant, sleep at a hotel, refuel and buy a car at the dealer. Screenshots of places across the state are written to the directory. It never writes a save file.

## Source

| File | Contents |
|---|---|
| `src/gfx.c` | math, GLSL (PBR, procedural sky/clouds, city ground and ocean shaders), terrain/road/facade shading, tiled instanced renderer, bloom/tonemap, HUD primitives |
| `src/map.c` | California: coastline, bays and lakes, height-map terrain and biomes, cities, the highway graph, the road mesh, regions |
| `src/city.c` | buildings by regional style, shops/hotels/restaurants, roadside stops, landmarks, countryside, collision queries, the map picture |
| `src/models.c` | vehicle and character models |
| `src/actors.c` | particles, car physics, traffic and police AI, pedestrians, breakable street lamps |
| `src/game.c` | player, wanted level, missions and activities, HUD, minimap and map, save/load |
| `src/main.c` | scene assembly, camera, input, main loop, self-test |
| `src/audio.c` | software mixer, synthesized effects, engine/siren/ambience layers, five radio stations |
