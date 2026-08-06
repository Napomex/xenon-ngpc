/**********************************************************************
 * XENON 2 - Z80-SEQUENCER BRING-UP ROM (06.08.2026)
 *
 * Laedt den von tools/z80_seq.py erzeugten Sequencer-Blob (Treiber +
 * Notentabelle + gepackte Streams) in das Z80-RAM, startet den Z80 und
 * tut danach NUR noch das, was das Spiel spaeter auch tun soll:
 *   - je Frame den VBCounter in das TICK-Byte schreiben (die 60-Hz-Uhr)
 *   - auf Tastendruck ein Kommando setzen
 * Die gesamte Musikarbeit passiert auf dem Z80.
 *
 *   A       -> Theme (Loop, 4 Spuren)
 *   B       -> Jingle (einmal)
 *   OPTION  -> Stop
 *
 * carthdr.h MUSS zuerst kommen (Cartridge-Header, siehe sndtest.c).
 **********************************************************************/
#include "ngpc.h"
#include "carthdr.h"
#include "library.h"

typedef unsigned char  u8;
typedef unsigned short u16;
#include "sounds_z80seq.h"

#define Z80_TICK  (*(volatile u8 *)Z80SEQ_TICK)
#define Z80_CMD   (*(volatile u8 *)Z80SEQ_CMD)
#define Z80_MATTN (*(volatile u8 *)Z80SEQ_MATTN)

static u8 g_pad, g_pad_prev, g_pad_pressed;

void main(void) {
    u8 *ram;
    u16 i;
    u8 letzt;

    InitNGPC();
    SetBackgroundColour(RGB(0, 0, 0));
    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);
    SysSetSystemFont();
    SetPalette(SCR_1_PLANE, 0, RGB(0,0,0), RGB(15,15,15), RGB(15,15,15), RGB(15,15,15));
    PrintString(SCR_1_PLANE, 0, 1, 2, "Z80 SEQUENCER TEST");
    PrintString(SCR_1_PLANE, 0, 1, 5, "A     THEME (LOOP)");
    PrintString(SCR_1_PLANE, 0, 1, 6, "B     JINGLE");
    PrintString(SCR_1_PLANE, 0, 1, 7, "OPT   STOP");

    /* Z80 anhalten, Blob kopieren, starten - exakt die Sounds_Init-Folge. */
    SOUNDCPU_CTRL = 0xAAAA;
    ram = (u8 *)0x7000;
    for (i = 0; i < sizeof(s_z80seq); i++) {
        ram[i] = s_z80seq[i];
    }
    SOUNDCPU_CTRL = 0x5555;

    Z80_MATTN = 2;              /* MUSIC_ATTN wie im Spiel */
    Z80_TICK  = VBCounter;

    /* kurz ankommen lassen, dann das Theme starten */
    for (i = 0; i < 30; i++) {
        letzt = VBCounter;
        while (VBCounter == letzt) { }
        Z80_TICK = VBCounter;
    }
    Z80_CMD = 1;

    for (;;) {
        letzt = VBCounter;
        while (VBCounter == letzt) { }
        Z80_TICK = VBCounter;   /* die ganze Restarbeit der Haupt-CPU */

        g_pad_prev    = g_pad;
        g_pad         = JOYPAD;
        g_pad_pressed = g_pad & (u8)(~g_pad_prev);
        if (g_pad_pressed & J_A)      Z80_CMD = 1;
        if (g_pad_pressed & J_B)      Z80_CMD = 2;
        if (g_pad_pressed & J_OPTION) Z80_CMD = 3;
    }
}
