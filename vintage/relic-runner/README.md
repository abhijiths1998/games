# Relic Runner

An EGA-style platformer in the spirit of early-90s DOS side-scrollers. Rook Rivet must raid six sun vaults: grab each vault's **Sunstone** to unseal the door, then run for the exit. Collect gems along the way, stomp spiders and bats, shoot fire-spitting totems, and strap on a jetpack for the big crossings.

Everything is in the vintage style: a 320×200 canvas in the 16-colour EGA palette, half-screen "jump" scrolling, a one-voice PC-speaker soundtrack, DOS dialog boxes, a 40-column font and a high-score table with initials. Press F2 to turn the CRT scanlines on or off.

## Build and play

    make            # needs a C compiler and the SDL2 development package
    make run        # or ./relic-runner --level 3 to jump ahead

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | Arrows / WASD | Left stick / d-pad |
| Jump (hold for higher) | Space / Up / Z | A |
| Fire pistol | X / Ctrl / J | X / B / RB |
| Jetpack on/off | C / Alt / K | Y / LB |
| Fly (jetpack on) | Arrows | Stick |
| Pause | P | |
| Quit to title | Esc | Back |
| CRT filter / sound / fullscreen | F2 / M / F11 | |

## Items

| | Points / effect |
|---|---|
| Diamond, ruby, crown, orb | 100, 250, 500, 1000 |
| Sunstone | 2000, opens the vault door |
| ? block (hit from below) | 100 |
| Jetpack | flight fuel; touch it again to refill |
| Pistol | one shot on screen at a time |
| Heart | extra life (also every 20,000 points) |

Collect every gem in a vault for a 5,000 point bonus.

## Self-test

    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./relic-runner --selftest /tmp/shots

This runs a solver that drives the game's real physics code through every level, checking that each Sunstone, door and gem can be reached. It also checks that the jetpack levels can't be finished without the jetpack, and writes screenshots of every screen.

High scores are saved in `~/.local/share/doomedl/relic-runner/scores.txt`.
