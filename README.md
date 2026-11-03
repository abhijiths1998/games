# Games

Small native Linux games written in C11 with SDL2. Everything (graphics, fonts, music and sound effects) is generated in code, so there are no asset files.

| Game | What it is |
|---|---|
| [Port Calder](port-calder/) | Open-world California road trip in OpenGL 3.3: the whole state scaled down, with 19 real cities, Interstate 5, US 101, the Pacific Coast Highway, the Golden Gate Bridge, mountains and deserts. It has traffic, police chases, hotels, restaurants, shops, fuel, day/night, rain and radio. Needs a GPU. |
| [Hardlight](hardlight/) | Modern arena shooter in OpenGL 3.3: PBR lighting, shadows, HDR bloom, waves of drones, crawlers and sentinels. Needs a GPU. |
| [Bramble Bound](bramble-bound/) | Side-scrolling platformer. Guide Bramble the fox through four worlds (meadow, sunset cliffs, glowing cave, frozen night) with parallax backgrounds, dynamic lighting, weather and a chiptune per world. |
| [Orbit Golf](orbit-golf/) | Gravity mini-golf in space. Slingshot the ball around planets, black holes, wormholes and moving moons across 12 holes. |
| [Prism Smash](prism-smash/) | Neon brick-breaker with exploding chain bricks, ball-splitting prisms, 7 power-ups and 12 levels. |
| [Lumen Drift](lumen-drift/) | Survive in a dark cavern as a fading light, collecting sparks while shadows hunt you. |

### Vintage collection

Original games in the style of old DOS titles, sharing one retro toolkit. See [vintage/](vintage/).

| Game | What it is |
|---|---|
| [Relic Runner](vintage/relic-runner/) | EGA platformer: grab each vault's Sunstone, reach the door. Jetpack, pistol, PC-speaker sound, CRT scanlines. |
| [Ember Isle](vintage/ember-isle/) | Open-world 3D island in early-90s software-rendered polygons. Gather embers, light seven beacons, rekindle the lighthouse as the sun sets. |
| [Nova Lancer](vintage/nova-lancer/) | 4-colour CGA vertical shoot-'em-up: 4 stages, multi-phase bosses, three weapons, smart bombs. |
| [Deep Delve](vintage/deep-delve/) | DOS text-mode roguelike: 10 generated depths, unidentified potions and scrolls, a boss guarding the Hollow Star. |

## Build

You need a C compiler, `make`, `pkg-config` and the SDL2 development package (`sdl2` on Arch, `libsdl2-dev` on Debian/Ubuntu, `SDL2-devel` on Fedora). Hardlight and Port Calder also need the OpenGL development files (`mesa`, `libgl1-mesa-dev` or `mesa-libGL-devel`).

    make                 # build every game
    cd bramble-bound && make run
    cd vintage/relic-runner && make run

Each game folder has its own README with controls. `make install` inside a game folder adds it to your app menu (`make uninstall` removes it).
