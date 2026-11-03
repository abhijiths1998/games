# Bramble Bound

An original side-scrolling platformer for Linux. Guide **Bramble the fox** across four worlds:
Mossy Meadow, Amber Cliffs, Glimmer Hollow (a glowing cave) and Frostfall Peaks (an aurora-lit night).

Everything is generated in code (C11 + SDL2 only): pixel-art sprites, tiles, multi-layer parallax
backdrops, dynamic lighting, weather, god-rays, aurora, animated water, chiptune music and sound effects.

## Build & run

    sudo pacman -S sdl2        # or: apt install libsdl2-dev / dnf install SDL2-devel
    make run
    make install               # optional: ~/.local/bin + app-menu entry (make uninstall to remove)

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | Arrows / A D | Stick / D-pad |
| Jump (hold for higher) | Space / Z / W / Up | A / B |
| Run | Shift / X | X / Y / right trigger |
| Pause | Esc / P | Start |
| Music on/off | M | |
| Fullscreen | F11 | |

## How to play

- Collect golden seeds; 50 seeds = extra life. Hit star crates from below.
- Stomp beetles and bees. Hedgehogs are spiky: wait until they doze off (Zzz) before stomping.
- Grab the **leaf**: it absorbs one hit, lets you break stone bricks, and lets you **glide** by holding jump.
- Bounce on mushrooms, ride lifts and sliding platforms, light lanterns to set checkpoints.
- Touch the crystal at the end of each world. Faster clears earn a time bonus.

The best score is saved to `~/.local/share/doomedl/bramble-bound/best`.

`./bramble-bound --selftest DIR` plays every world headlessly with an autopilot and writes screenshots.
