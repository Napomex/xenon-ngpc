/**********************************************************************
 * XENON 2 - NGPC SOUND TEST ROM v2  (A/B comparison)
 *   A       -> MIDI multi-track arrangement (megablast.mid -> 4 voices)
 *   B       -> DOS 2-voice original (from XENON2.EXE)
 *   OPTION  -> stop
 **********************************************************************/
#include "ngpc.h"
#include "carthdr.h"
#include "library.h"
#include "sounds.h"
/* MIDI arrangement provides the default NOTE_TABLE symbol sounds.c links to */
#define XENON_SONGS_PROVIDE_DEFAULT_NOTE_TABLE
#include "Musik/xenon_songs2.c"    /* X2_THEME_* (MIDI, 4 voices) + NOTE_TABLE */
#undef XENON_SONGS_PROVIDE_DEFAULT_NOTE_TABLE   /* only one default NOTE_TABLE */
#include "Musik/xenon_songs.c"     /* X_THEME_* / X_JINGLE_* (DOS 2-voice)     */

static u8 g_pad, g_pad_prev, g_pad_pressed;

static void play_midi(void) {
    Bgm_SetNoteTable(X2_THEME_NOTE_TABLE);
    Bgm_StartLoop4Ex(X2_THEME_CH0, 0, X2_THEME_CH1, 0,
                     X2_THEME_CH2, 0, X2_THEME_CHN, 0);
}
static void play_dos(void) {
    Bgm_SetNoteTable(X_THEME_NOTE_TABLE);
    Bgm_StartLoop4Ex(X_THEME_CH0, 0, X_THEME_CH1, 0,
                     X_THEME_CH2, 0, X_THEME_CHN, 0);
}

void main(void) {
    InitNGPC();
    SetBackgroundColour(RGB(0,0,0));
    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);
    SysSetSystemFont();
    SetPalette(SCR_1_PLANE, 0, RGB(0,0,0), RGB(15,15,15), RGB(15,15,15), RGB(15,15,15));

    PrintString(SCR_1_PLANE, 0, 1, 2, "XENON 2 SOUND TEST V2");
    PrintString(SCR_1_PLANE, 0, 1, 5, "A   MIDI 4-VOICE");
    PrintString(SCR_1_PLANE, 0, 1, 6, "B   DOS 2-VOICE");
    PrintString(SCR_1_PLANE, 0, 1, 7, "OPT STOP");
    PrintString(SCR_1_PLANE, 0, 1, 10, "NOW:");
    PrintString(SCR_1_PLANE, 0, 1, 12, "VBL:");

    Sounds_Init();
    play_midi();
    PrintString(SCR_1_PLANE, 0, 6, 10, "MIDI  ");

    for (;;) {
        { u8 v = VBCounter; while (VBCounter == v) ; }
        g_pad_prev = g_pad; g_pad = JOYPAD;
        g_pad_pressed = g_pad & (u8)(~g_pad_prev);
        if (g_pad_pressed & J_A)      { play_midi(); PrintString(SCR_1_PLANE,0,6,10,"MIDI  "); }
        if (g_pad_pressed & J_B)      { play_dos();  PrintString(SCR_1_PLANE,0,6,10,"DOS   "); }
        if (g_pad_pressed & J_OPTION) { Bgm_Stop();  PrintString(SCR_1_PLANE,0,6,10,"STOP  "); }
        Sounds_Update();
        PrintHex(SCR_1_PLANE, 0, 6, 12, (u16)VBCounter, 2);
    }
}
