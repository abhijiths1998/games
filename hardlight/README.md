# Hardlight

A compact arena shooter with a modern renderer. Hold a neon rooftop arena against waves of plasma drones, charging crawlers and shielded sentinels. The waves get bigger and faster; see how long you last.

Everything is generated in code (geometry, materials, sky, sound effects and music), so there are no asset files. It needs a GPU with OpenGL 3.3.

**Renderer:** physically based shading (GGX), shadow-mapped moonlight, up to 32 dynamic point lights (muzzle flashes, plasma bolts, explosions), a procedural night sky with stars and nebula reflected in metal surfaces, 4× MSAA HDR, a bloom chain, ACES tonemapping, chromatic aberration, vignette, film grain, and additive particles and debris.

## Build and play

    make            # needs a C compiler, SDL2 and OpenGL development files
    make run

## Controls

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Move | WASD | Left stick |
| Aim | Mouse | Right stick |
| Fire pulse rifle | Left mouse | Right trigger |
| Arc cannon (explosive, 3 charges) | Right mouse | Left trigger |
| Jump | Space | A |
| Sprint | Shift | Left stick click |
| Dash | Q or Ctrl | B |
| Reload | R | X |
| Pause | Esc or P | Start |
| Fullscreen / mute | F11 / M | |

In the pause menu, `-` and `+` change mouse sensitivity, `,` and `.` change field of view, and `I` inverts the Y axis. Settings and your best score are saved in `~/.local/share/doomedl/hardlight/best.txt`.

**Tips:** your shield recharges after a few seconds out of fire. Destroyed enemies sometimes drop green health orbs. Crawlers can only reach the central platform by its ramps, and sentinels split off drones when half destroyed.

## Self-test

    SDL_VIDEODRIVER=offscreen ./hardlight --selftest /tmp/shots

It compiles every shader, times the GPU, and lets a bot with human-like reaction time and aim play until it dies. In testing it lasted about 6 minutes and reached wave 9 with 60% accuracy. It also checks that a player who stands still gets killed, and writes screenshots. It runs on NVIDIA and on Mesa's software renderer.
