# Orbit Golf

Gravity mini-golf in space. Sling the ball around planets, dodge black holes, bounce off asteroid belts, hop through wormholes and time your shots against moving moons. Twelve hand-built holes, a scorecard after each, and your best round is saved.

Native Linux, written in C with SDL2 only. Graphics, font and sound are all generated in code; there are no asset files.

## Build and play

    make            # needs gcc/clang + the SDL2 dev package (Debian/Ubuntu: libsdl2-dev, Fedora: SDL2-devel, Arch: sdl2)
    ./orbit-golf

Optional: `make install` puts the binary in `~/.local/bin` and adds a launcher to your app menu (`make uninstall` removes both).

## Controls

| Action | Mouse | Keyboard | Gamepad |
|---|---|---|---|
| Aim and power | Press, drag back, release (the further you drag, the harder the shot) | Left/Right or A/D to aim (Shift = fine), hold Space to charge | Stick / d-pad to aim, hold A to charge |
| Shoot | Release the button | Release Space | Release A |
| Cancel a shot | Right click | Esc | B |
| Pause | | Esc / P | Start |
| Fullscreen | | F11 | |
| Menus | Click | Space / Enter | A / Start |

## Rules

- Get the ball into the glowing cup by rolling in slower than a fast pass. The cup tugs the ball gently when you are close.
- Planets pull on the ball and are solid. Asteroid belts bounce it hard. Wormholes come in pairs. Moons orbit on a clock, so timing matters.
- Black holes swallow the ball: +1 stroke and the ball returns to where you last shot from.
- The dotted preview shows the first moment of your shot's path, including bends and bounces.
- After 10 strokes the ball is picked up and the hole scores 10.
- Best total over all 12 holes is saved in `~/.local/share/doomedl/orbit-golf/best` (the SDL pref path).

## Self-test

    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./orbit-golf --selftest /some/dir

An autopilot (brute-force shot search with a little human error) plays a full round headlessly and writes `title.bmp`, `aim.bmp`, `play.bmp`, `play2.bmp`, `card.bmp` and `over.bmp` to that directory. It never writes a save file.
