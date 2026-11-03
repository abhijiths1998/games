# NOVA LANCER

An original vertical shoot-'em-up in the style of late-80s CGA PC games, written in C11 with SDL2.
Everything on screen uses only the four CGA palette-1 high-intensity colours: black, light cyan,
light magenta and white. Built on the shared `../common/retro.h` toolkit (320x200 canvas, CRT filter,
one-voice PC-speaker sound).

## Build and run

    make
    ./nova-lancer            # normal game
    ./nova-lancer --stage 3  # start on stage 3 (debug)

## Controls

| Action        | Keys                          | Gamepad              |
|---------------|-------------------------------|----------------------|
| Move          | Arrow keys / WASD             | Left stick / D-pad   |
| Fire (hold)   | Space / Z / X / J / Ctrl      | A / X / B / R-shoulder |
| Smart bomb    | B or C                        | Y / L-shoulder       |
| Pause         | P (also on focus loss)        | Start                |
| Quit to title | Esc (then Y / N)              |                      |
| Mute          | M                             |                      |
| CRT / fullscreen | F2 / F11                   |                      |
| Title pages   | Left / Right, Space or Enter starts |                |

## How to play

Fly through four stages (Outer Reach, Cinder Belt, Veil World, Iron Citadel). Each ends in a
multi-phase boss with a health bar. Destroy enemy formations quickly to build a kill chain; the
score multiplier climbs to x8 and drains if you stop killing. Collect capsules:

- P: gun level up (levels 1-4). Dying costs one level.
- S: Spread shot. L: Lance (piercing bolts). H: Seeker (homing missiles). Picking the same type again levels it up.
- B: extra smart bomb (max 5). A bomb wipes enemy bullets, damages everything and makes you briefly invulnerable.
- 1: extra ship. Extra ships are also awarded at 30000 and 80000 points, then every 100000.

You respawn with a short invulnerability window. The tiny white dot on your ship is the hitbox.

## Self-test

    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./nova-lancer --selftest DIR

Runs an autopilot bot through all four stages using the real game code, prints one result line per
stage, and writes BMP screenshots (title pages, stage card, gameplay and boss shots for every stage,
bomb, pause, quit prompt, stage clear, game over, initials entry, victory, each weapon) into `DIR`
(the directory must exist). It exits 0 when the bot clears every stage, and never touches the score file.

## Save file

High scores are stored in `~/.local/share/doomedl/nova-lancer/scores.txt`.
