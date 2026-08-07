/* THE LEGACY DRIVER FOR DIAGNOSTIC ROMS (sndmix/sndtest/sndlab, 07.08.2026).
   The game builds sounds.c with SOUNDS_Z80_SEQ=1 (default): Bgm_Update is
   then only the tick byte for the Z80 sequencer, and the whole BgmVoice
   machinery never runs. The mixer and test ROMs need exactly that
   machinery. This wrapper pins the switch to 0 and reuses sounds.c
   unchanged - their makefiles link sounds_alt.rel INSTEAD OF sounds.rel,
   so there is exactly one copy of every symbol in each ROM and no
   compiler -D flag is needed (cc900 support for it is unverified). */
#define SOUNDS_Z80_SEQ 0
#include "sounds.c"
