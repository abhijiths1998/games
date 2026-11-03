# Ember Isle

A small open-world 3D game in the style of early-90s software-rendered polygon games. The keeper of Ember Isle is gone and its seven beacons have gone dark. Roam the island, gather glowing embers (each beacon needs three to burn), light every beacon, then return to the lighthouse and rekindle its lamp. The sun sets as you play, so you may finish under the stars.

It's all drawn by a hand-written CPU renderer at 320×200: flat-shaded polygons, a z-buffer, fog and ordered dithering into a 256-colour palette, with no GPU 3D and no asset files. The island has hills, beaches, forests, a village, standing stones, ruins, sheep that scatter when you approach, circling gulls, drifting clouds and a full day-to-night sky. Sound is a one-voice PC speaker.

## Build and play

    make            # needs a C compiler and the SDL2 development package
    make run

## Controls

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Walk | W S / Up Down | Left stick |
| Turn | A D / Left Right, or mouse (click to capture) | Right stick |
| Step sideways | Q E | Left stick |
| Run | Shift | B / RT / stick click |
| Jump | Space | A |
| Look up / down | PgUp PgDn, or mouse | Right stick |
| Map | Tab or C | Y |
| Pause / quit to title | P / Esc (Esc first releases the mouse) | |
| CRT filter / sound / fullscreen | F2 / M / F11 | |

The compass at the top marks unlit beacons in cyan and lit ones in yellow. Arrows at its edges point to beacons behind you. Once all seven burn, the lighthouse flashes on the compass.

Score: 100 per ember, 1000 per beacon, 5000 for the lighthouse, plus a time bonus. Best scores are saved in `~/.local/share/doomedl/ember-isle/scores.txt`.

## Self-test

    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./ember-isle --selftest /tmp/shots

This checks the generated world (land, beacon spacing, ember placement), then lets a bot play the game with the real movement code: it gathers embers, lights all seven beacons and reaches the lighthouse. It also times the renderer and writes screenshots of every screen and time of day.
