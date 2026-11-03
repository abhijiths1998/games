# Deep Delve

An EGA text-mode dungeon crawl in the spirit of late-80s DOS roguelikes. Descend ten procedurally generated floors (rooms-and-corridors and cave levels), survive the things that live there, and on the last floor defeat or slip past the **Hollow King** to take the **Hollow Star**. Death is permanent.

The whole game is a 40x25 colour text screen (8x8 cells on the shared 320x200 canvas, 16 EGA colours), a one-voice PC-speaker soundtrack and a high-score table with initials. Press F2 to toggle the CRT filter.

## Build and play

    make            # needs a C compiler and the SDL2 development package
    make run

## How to play

- Bump into a monster to attack it. Sleeping or stunned foes take double damage.
- Throw knives or zap wands with **T**: pick the item, aim the cursor (arrows, Tab cycles targets), Enter fires. Thrown knives can be picked up again.
- Potions, scrolls and wands are **unidentified until used**, and their looks/titles are reshuffled every game. Some are harmful (venom, torpor, summoning, blaze). Scroll of lore identifies something in your pack.
- Wield weapons and wear armour by selecting them in the pack. Enchant scrolls improve what you hold.
- You get hungry: eat before you starve. Resting (R) is interrupted by enemies, and new wanderers keep arriving.
- Level up by killing things. Walking onto gold collects it; other items need **G**.
- Score = gold + 100 per depth reached + 10 per kill (+2000 for taking the Hollow Star).

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move / attack | Arrows, WASD, numpad | Left stick / d-pad |
| Diagonals | Numpad 7 9 1 3, or Y U B N | Stick diagonals |
| Wait a turn | `.`, 5, Space | |
| Rest until healed | R | |
| Pick up | G or `,` | A (context: stairs, pick up, wait) |
| Descend stairs | `>` (Shift+`.`) or Enter on stairs | A on stairs |
| Inventory | I | Y / LB |
| Use / eat / equip | Letter in the pack, or Enter on the cursor | A |
| Throw / zap | T | X / B / RB |
| Message log | L | |
| Quit to title prompt | Esc | Back / Start |
| Mute / CRT / fullscreen | M / F2 / F11 | |

In targeting: arrows move the cursor, Tab (or X on pad) cycles monsters, Enter (A) fires, Esc (B/Y) cancels.
Title screen: Enter starts, Left/Right browse pages, F1 help, H high scores.

## Self-test

    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./deep-delve --selftest /tmp/shots

Runs headless: generates floors 1-10 for 520 seeds and checks connectivity, stairs and spawn positions; plays several seeds with an autopilot bot through the real turn code (set `DD_BOTS=n` for more); and writes BMP screenshots of every screen. It never writes the score file. Exit code 0 means success.

## High scores

Saved to `~/.local/share/doomedl/deep-delve/scores.txt` (only after you enter initials in a real run).
