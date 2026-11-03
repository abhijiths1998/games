# Prism Smash

A neon brick-breaker. Bounce the ball off your paddle, smash glowing bricks, catch falling power-ups and set off chain reactions. Hit bricks without touching the paddle to build a combo multiplier (up to x8), and note that the combo resets whenever the ball touches the paddle.

Native Linux, written in C with SDL2 only. Graphics, font and sound are all generated in code; there are no asset files.

## Build and play

    make            # needs gcc/clang, pkg-config and the SDL2 dev package (e.g. `sdl2` on Arch, `libsdl2-dev` on Debian/Ubuntu)
    ./prism-smash

Optional: `make install` puts the binary in `~/.local/bin` and adds an app-menu launcher (`make uninstall` removes both).

## Controls

| Action | Mouse | Keyboard | Gamepad |
|---|---|---|---|
| Move paddle | Move mouse | A/D or arrows | Left stick / d-pad |
| Launch ball / fire laser / release caught ball | Left click (hold to rapid-fire) | Space or Enter | A / RB |
| Pause | | Esc / P (Q while paused: quit to title) | Start |
| Fullscreen | | F11 | |

The game also pauses when the window loses focus.

## Bricks

- Plain bricks: one hit.
- Silver (2 hits) and gold (3 hits) bricks show cracks as they weaken.
- Explosive bricks (orange, X) detonate their neighbours in a chain reaction.
- Steel bricks cannot be destroyed (ignore them; they are not needed to clear a level).
- Prism bricks (rainbow) split the ball into coloured copies.

## Power-ups (falling capsules)

M multiball, W wide paddle, L laser, C catch (sticky paddle), T slow-mo, + extra life, and the rare bad one, - shrink.

## Notes

- 12 hand-designed levels; after the last one the game loops, faster.
- Ball angle depends on where it strikes the paddle, and speed ramps up gently as you play.
- Your high score is saved in `~/.local/share/doomedl/prism-smash/best`.

## Self-test

    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./prism-smash --selftest /some/dir

An autopilot plays every level headlessly, then a full game, and writes `title.bmp`, `play.bmp` and `over.bmp` to that directory. It never touches your saved score.
