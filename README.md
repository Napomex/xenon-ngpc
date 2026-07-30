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
- **Timing**: a fixed 30 fps cap (two VBlanks minimum per frame). Values
  derived from the original are converted with px ÷ 2 and ticks × 1.648.

## A note on language

The in-source comments are in German — they carry the reasoning behind a lot
of hardware-specific decisions and measurements, and translating them would
risk losing that detail. The repository-facing documentation is in English.
