# Games

Four small native Linux games written in C11 with SDL2. Everything (graphics, fonts, music and sound effects) is generated in code, so there are no asset files.

| Game | What it is |
|---|---|
| [Bramble Bound](bramble-bound/) | Side-scrolling platformer. Guide Bramble the fox through four worlds (meadow, sunset cliffs, glowing cave, frozen night) with parallax backgrounds, dynamic lighting, weather and a chiptune per world. |
| [Orbit Golf](orbit-golf/) | Gravity mini-golf in space. Slingshot the ball around planets, black holes, wormholes and moving moons across 12 holes. |
| [Prism Smash](prism-smash/) | Neon brick-breaker with exploding chain bricks, ball-splitting prisms, 7 power-ups and 12 levels. |
| [Lumen Drift](lumen-drift/) | Survive in a dark cavern as a fading light, collecting sparks while shadows hunt you. |

## Build

You need a C compiler, `make`, `pkg-config` and the SDL2 development package (`sdl2` on Arch, `libsdl2-dev` on Debian/Ubuntu, `SDL2-devel` on Fedora).

    make                 # build every game
    cd bramble-bound && make run

Each game folder has its own README with controls. `make install` inside a game folder adds it to your app menu (`make uninstall` removes it).
