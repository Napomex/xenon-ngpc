/**********************************************************************
 * XENON 2 - NGPC SOUND TEST ROM
 *
 * Standalone test cartridge for the Megablast music converted from the
 * DOS original (XENON2.EXE PC-speaker driver) into the NGPC "Sound
 * Creator" pre-baked format (see tools/music/, Musik/xenon_songs.c).
 *
 *   A       -> play the Megablast main theme (song 0)
 *   B       -> play the short jingle (song 1)
 *   OPTION  -> stop
 *
 * carthdr.h MUST be included first: its consts form the cartridge header
 * and this .c must be the FIRST object in the link (f_header is empty, so
 * the first f_const lands at 0x200000 = the cartridge header).
 **********************************************************************/
#include "ngpc.h"
#include "carthdr.h"
#include "library.h"
#include "sounds.h"
/* provide the default NOTE_TABLE symbol sounds.c links against (test ROM only) */
#define XENON_SONGS_PROVIDE_DEFAULT_NOTE_TABLE
#include "Musik/xenon_songs.c"   /* self-contained song data (like Musik/Titel.c) */

static u8 g_pad, g_pad_prev, g_pad_pressed;

static void play_theme(void) {
    Bgm_SetNoteTable(X_THEME_NOTE_TABLE);
    Bgm_StartLoop4Ex(X_THEME_CH0, 0, X_THEME_CH1, 0,
                     X_THEME_CH2, 0, X_THEME_CHN, 0);
}

static void play_jingle(void) {
    Bgm_SetNoteTable(X_JINGLE_NOTE_TABLE);
    Bgm_StartLoop4Ex(X_JINGLE_CH0, 0, X_JINGLE_CH1, 0,
                     X_JINGLE_CH2, 0, X_JINGLE_CHN, 0);
}

void main(void) {
    InitNGPC();

    SetBackgroundColour(RGB(0, 0, 0));
    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);
    SysSetSystemFont();
    /* white text on black for the font palette on both planes */
    SetPalette(SCR_1_PLANE, 0, RGB(0,0,0), RGB(15,15,15), RGB(15,15,15), RGB(15,15,15));

    PrintString(SCR_1_PLANE, 0, 1, 2, "XENON 2  NGPC SOUND TEST");
    PrintString(SCR_1_PLANE, 0, 1, 5, "A     THEME (MEGABLAST)");
    PrintString(SCR_1_PLANE, 0, 1, 6, "B     JINGLE");
    PrintString(SCR_1_PLANE, 0, 1, 7, "OPT   STOP");
    PrintString(SCR_1_PLANE, 0, 1, 10, "NOW:");
    PrintString(SCR_1_PLANE, 0, 1, 12, "VBL:");

    Sounds_Init();
    play_theme();
    PrintString(SCR_1_PLANE, 0, 6, 10, "THEME  ");

    for (;;) {
        /* one call per VBlank (~60 Hz); driver self-paces via VBCounter */
        { u8 v = VBCounter; while (VBCounter == v) ; }

        g_pad_prev    = g_pad;
        g_pad         = JOYPAD;
        g_pad_pressed = g_pad & (u8)(~g_pad_prev);

        if (g_pad_pressed & J_A) {
            play_theme();
            PrintString(SCR_1_PLANE, 0, 6, 10, "THEME  ");
        }
        if (g_pad_pressed & J_B) {
            play_jingle();
            PrintString(SCR_1_PLANE, 0, 6, 10, "JINGLE ");
        }
        if (g_pad_pressed & J_OPTION) {
            Bgm_Stop();
            PrintString(SCR_1_PLANE, 0, 6, 10, "STOPPED");
        }

        Sounds_Update();

        /* liveness indicator */
        PrintHex(SCR_1_PLANE, 0, 6, 12, (u16)VBCounter, 2);
    }
}
