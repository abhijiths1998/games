# Lumen Drift

You are a small light drifting through a dark cavern. Your light is always fading. Gather blue sparks to refuel and chain them quickly for up to ×5 score. Shadows hunt you in the dark, but they slow down inside your light. A flare banishes every shadow within your light, at a cost of 14 light.

Native Linux, written in C with SDL2 only. Graphics, lighting, font and sound are all generated in code; there are no asset files.

## Build and play

    make            # needs gcc/clang + SDL2 dev headers (pkg-config sdl2)
    ./lumen-drift

Optional: `make install` puts the binary in `~/.local/bin` and adds a launcher to your app menu (`make uninstall` removes both).

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | WASD / arrows | Left stick / d-pad |
| Flare | Space (or J/Z) | A / RB |
| Dash (invulnerable) | Shift (or K/X) | B / X / LB |
| Pause | Esc / P | Start |
| Fullscreen | F11 | |

## Tips

- Sparks glow in the dark, so you can always see where to go. Shadows show only their eyes.
- Golden-eyed shadows (after about 35 s) pause, flicker, then lunge. Dash through them or flare.
- Each banished shadow returns 5 light, so a flare that catches 3 or more pays for itself.
- Your high score is saved in `~/.local/share/lumen-drift/best`.

## Self-test

    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./lumen-drift --selftest /some/dir

An autopilot plays a full game headlessly and writes `title.bmp`, `play.bmp` and `over.bmp` to that directory.
