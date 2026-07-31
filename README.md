# XENON — Neo Geo Pocket Color

A vertically scrolling shoot 'em up in the style of *Xenon 2: Megablast*,
written in C89 for the TLCS-900H CPU of the Neo Geo Pocket Color and built
with the cc900 toolchain.

Graphics: NAPOMEX · Music programming: NAPOMEX · FX: NAPOMEX

## What is in this repository

Source code and the built ROM. Level data, sprites and all gameplay tables
are produced by an external editor (NGPC Tile Analyzer) and exported as
`PNG/Map/map.h` — nothing in this project is hand-pixelled or hand-coded as
level data.

| Path | Contents |
|---|---|
| `xenon.c` | the game — main loop, rendering, all subsystems |
| `library.c` / `library.h` | NGPC framework (sprites, tilemaps, palettes) |
| `sounds.c` / `sounds.h` | T6W28 sound driver |
| `Musik/*.c` | baked note tables for the soundtrack |
| `PNG/Map/map.h` | exported level: terrain, sprites, paths, spawns, weapons |
| `spr_sections.h` | generated per-section sprite pool tables |
| `ngpc.h`, `carthdr.h`, `dmaprog.asm` | hardware definitions and DMA program |
| `makefile`, `ngpc.lcf` | build and linker configuration |
| `xenon.ngp` | the built ROM |

## Building

Requires the Toshiba cc900 toolchain (`cc900`, `tulink`, `tuconv`) and
`s242ngp` on the PATH.

```
make
```

produces `xenon.ngp`.

## Implementation notes

- **Rendering**: the ship is a 7-cell metasprite using OAM chaining, so
  movement costs a single OAM write. All other sprites draw from a shared
  dynamic OAM pool (slots 16–63).
- **Scrolling**: toroidal 32-row ring streaming across both scroll planes,
  with a raster split for the HUD bar driven by micro-DMA.
- **VRAM**: the 512 tile slots are fully allocated. Both terrain and sprite
  tiles are streamed per level section rather than held resident, using a
  progressive slot assignment that keeps offsets stable across section
  boundaries.
- **Timing**: the frame rate is switchable between 30 and 20 fps — a fixed
  cap of two or three VBlanks per frame. Values derived from the original are
  converted with px ÷ 2 and ticks × 1.648.

## Frame rate

The compute time of a frame sits just above two VBlanks, so at a 30 fps cap a
frame keeps tipping between two and three. Because every movement value is
per frame, that swings the world speed by 50 %. A fixed three-VBlank budget
turns it into a constant pace at the cost of a third of the frames.

Both modes are meant to run the world at the same speed **in pixels per
second**. Every existing constant stays calibrated for 30 fps and the 20 fps
mode derives from it at runtime — speeds × 3/2, durations × 2/3 — so at 30 fps
each macro returns its input unchanged and that mode is provably identical to
before the conversion.

Three switches at the top of `xenon.c` control it:

| Switch | Default | Effect |
|---|---|---|
| `FPS_MODE_DEFAULT` | 20 | frame rate at cold start, 30 or 20 |
| `FPS_SWITCH_ENABLE` | 1 | OPTION on the title screen switches between them (+1.7 % frame load). At 0 the rate becomes a compile-time constant, the compiler folds every macro away and switchability costs nothing |
| `FPS_EXACT_HALVES` | 0 | 0 rounds ×1.5 of odd values to the nearest integer; 1 alternates between the two neighbours so the average is exact, at about 1.1 % |

Measured: 86 922 cycles per frame at a fixed 30 fps, 99 638 at a fixed 20 fps
— 14.6 % more per frame, but 23.6 % less per second, since there are two
thirds as many frames.

## A note on language

Source comments were originally written in German and have been translated to
English. They carry the reasoning behind a lot of hardware-specific decisions
and measurements — where a value looks arbitrary, the comment usually explains
which measurement produced it, and several record approaches that were tried
and measured to be useless, so they do not get tried again.
