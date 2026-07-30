/**********************************************************************
 * XENON 2 - NGPC SOUND LAB  (hardware mixer / A-B test cartridge)
 *
 * Several music arrangements with a live 4-channel mixer, so you can
 * dial in the best mix on real hardware and report the winner.
 *
 *   UP / DOWN   : select channel (BASS / LEAD / HARM / DRUM)
 *   LEFT/RIGHT  : volume of the selected channel  (0..15, RIGHT = louder)
 *   A           : mute / unmute the selected channel
 *   B           : next version        OPTION : previous version
 *
 * Volume is per channel and live (no restart). Muting = volume off.
 * Versions:
 *   0 DOS 2VOICE  original 2-voice from XENON2.EXE
 *   1 MB GUITAR   megablast.mid  lead=CleanGtr  harm=Vibra
 *   2 MB STRINGS  megablast.mid  lead=SynStr    harm=Vibra
 *   3 MB VIBRA    megablast.mid  lead=Vibra     harm=CleanGtr
 *   4 MB ORGAN    megablast.mid  lead=CleanGtr  harm=Organ
 *   5 XG STEEL    Xenon2 XG.mid  lead=CleanGtr  harm=SteelDrum
 *   6 XG STLEAD   Xenon2 XG.mid  lead=SteelDrum harm=CleanGtr
 *   7 XG ODRIVE   Xenon2 XG.mid  lead=CleanGtr  harm=Overdrive
 * Channels: CH0=BASS CH1=LEAD CH2=HARM CH3=DRUM
 **********************************************************************/
#include "ngpc.h"
#include "carthdr.h"
#include "library.h"
#include "sounds.h"

#define XENON_SONGS_PROVIDE_DEFAULT_NOTE_TABLE
#include "Musik/xenon_songs2.c"      /* X2_THEME_*  + default NOTE_TABLE */
#undef XENON_SONGS_PROVIDE_DEFAULT_NOTE_TABLE
#include "Musik/xenon_songs.c"       /* X_THEME_* (DOS)      */
#include "Musik/xenon_songs2b.c"     /* X2B_THEME_*          */
#include "Musik/xenon_songs2c.c"     /* X2C_THEME_*          */
#include "Musik/xenon_songs2d.c"     /* X2D_THEME_*          */
#include "Musik/xenon_songs_xg.c"    /* XG_THEME_*           */
#include "Musik/xenon_songs_xgb.c"   /* XGB_THEME_*          */
#include "Musik/xenon_songs_xgc.c"   /* XGC_THEME_*          */

typedef struct {
    const char *name;
    const unsigned char *nt;
    const unsigned char *ch[4];
    u8  grp;          /* 0=DOS 1=megablast.mid 2=Xenon2 XG.mid */
    u32 len;          /* loop length in frames (same within a group) */
} Song;

#define GRP_DOS 0u
#define GRP_MB  1u
#define GRP_XG  2u
#define LEN_DOS 9340u
#define LEN_MB  12413u
#define LEN_XG  20068u

static const Song SONGS[] = {
    { "DOS 2VOICE", X_THEME_NOTE_TABLE,   { X_THEME_CH0,  X_THEME_CH1,  X_THEME_CH2,  X_THEME_CHN  }, GRP_DOS, LEN_DOS },
    { "MB GUITAR ", X2_THEME_NOTE_TABLE,  { X2_THEME_CH0, X2_THEME_CH1, X2_THEME_CH2, X2_THEME_CHN }, GRP_MB,  LEN_MB  },
    { "MB STRINGS", X2B_THEME_NOTE_TABLE, { X2B_THEME_CH0,X2B_THEME_CH1,X2B_THEME_CH2,X2B_THEME_CHN}, GRP_MB,  LEN_MB  },
    { "MB VIBRA  ", X2C_THEME_NOTE_TABLE, { X2C_THEME_CH0,X2C_THEME_CH1,X2C_THEME_CH2,X2C_THEME_CHN}, GRP_MB,  LEN_MB  },
    { "MB ORGAN  ", X2D_THEME_NOTE_TABLE, { X2D_THEME_CH0,X2D_THEME_CH1,X2D_THEME_CH2,X2D_THEME_CHN}, GRP_MB,  LEN_MB  },
    { "XG STEEL  ", XG_THEME_NOTE_TABLE,  { XG_THEME_CH0, XG_THEME_CH1, XG_THEME_CH2, XG_THEME_CHN }, GRP_XG,  LEN_XG  },
    { "XG STLEAD ", XGB_THEME_NOTE_TABLE, { XGB_THEME_CH0,XGB_THEME_CH1,XGB_THEME_CH2,XGB_THEME_CHN}, GRP_XG,  LEN_XG  },
    { "XG ODRIVE ", XGC_THEME_NOTE_TABLE, { XGC_THEME_CH0,XGC_THEME_CH1,XGC_THEME_CH2,XGC_THEME_CHN}, GRP_XG,  LEN_XG  },
};
#define NSONGS 8

/* 2D array (not char*[]): cc900 mis-relocates arrays of string-literal pointers */
static const char CHNAMES[4][5] = { "BASS", "LEAD", "HARM", "DRUM" };
/* default attenuation per channel (0=loud..15=silent): bass/lead loud, harm/drums back a bit */
static const u8 DEF_ATTN[4] = { 2u, 2u, 5u, 4u };

static u8 g_pad, g_pad_prev, g_pad_pressed;
static u8 g_ver, g_cursor;
static u8 g_attn[4];    /* live volume per channel */
static u8 g_mute[4];    /* per-channel mute */
static BgmDebug g_dbg;  /* for the play-position readout */

static void start_song(void) {
    const Song *s = &SONGS[g_ver];
    Bgm_SetNoteTable(s->nt);
    Bgm_StartLoop4Ex(s->ch[0], 0, s->ch[1], 0, s->ch[2], 0, s->ch[3], 0);
}
/* switch version: seamless (keep position) within the same MIDI group,
   restart across groups (DOS<->MB<->XG differ in timing). Mix is kept. */
static void goto_version(u8 nv) {
    u8 same = (u8)(SONGS[nv].grp == SONGS[g_ver].grp && SONGS[nv].grp != GRP_DOS);
    const Song *s = &SONGS[nv];
    g_ver = nv;
    Bgm_SetNoteTable(s->nt);
    if (same) Bgm_SwapLoop4(s->ch[0], s->ch[1], s->ch[2], s->ch[3], s->len);
    else      Bgm_StartLoop4Ex(s->ch[0], 0, s->ch[1], 0, s->ch[2], 0, s->ch[3], 0);
}
static void load_defaults(void) {
    u8 i; for (i = 0u; i < 4u; i++) { g_attn[i] = DEF_ATTN[i]; g_mute[i] = 0u; }
}
static void apply_mix(void) {
    u8 i; for (i = 0u; i < 4u; i++) Bgm_SetVoiceAttn(i, g_mute[i] ? 15u : g_attn[i]);
}

static void draw_ui(void) {
    u8 i;
    PrintString(SCR_1_PLANE, 0, 1, 3, "VER:");
    PrintString(SCR_1_PLANE, 0, 6, 3, SONGS[g_ver].name);
    PrintNumber(SCR_1_PLANE, 0, 17, 3, (u16)(g_ver + 1u), 1);
    for (i = 0u; i < 4u; i++) {
        const char *nm = CHNAMES[i];          /* local ptr: cc900 subscript quirk */
        u8 y = (u8)(5 + i);
        PrintString(SCR_1_PLANE, 0, 1, y, (i == g_cursor) ? ">" : " ");
        PrintString(SCR_1_PLANE, 0, 3, y, nm);
        if (g_mute[i]) {
            PrintString(SCR_1_PLANE, 0, 9, y, "MUTE ");
        } else {
            PrintString(SCR_1_PLANE, 0, 9, y, "VOL");
            PrintNumber(SCR_1_PLANE, 0, 13, y, (u16)(15u - g_attn[i]), 2);
        }
    }
}

void main(void) {
    InitNGPC();
    SetBackgroundColour(RGB(0,0,0));
    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);
    SysSetSystemFont();
    SetPalette(SCR_1_PLANE, 0, RGB(0,0,0), RGB(15,15,15), RGB(15,15,15), RGB(15,15,15));

    PrintString(SCR_1_PLANE, 0, 1, 1, "XENON 2 SOUND LAB");
    PrintString(SCR_1_PLANE, 0, 1, 10, "U/D CHAN  L/R VOL");
    PrintString(SCR_1_PLANE, 0, 1, 11, "A MUTE");
    PrintString(SCR_1_PLANE, 0, 1, 12, "B NEXT  OPT PREV VER");
    PrintString(SCR_1_PLANE, 0, 1, 14, "POS:");

    g_ver = 1u; g_cursor = 0u;
    load_defaults();
    Sounds_Init();
    start_song();
    apply_mix();
    draw_ui();

    for (;;) {
        { u8 v = VBCounter; while (VBCounter == v) ; }
        g_pad_prev = g_pad; g_pad = JOYPAD;
        g_pad_pressed = g_pad & (u8)(~g_pad_prev);

        if (g_pad_pressed & J_DOWN)  { g_cursor = (u8)((g_cursor + 1u) & 3u); draw_ui(); }
        if (g_pad_pressed & J_UP)    { g_cursor = (u8)((g_cursor + 3u) & 3u); draw_ui(); }
        if (g_pad_pressed & J_RIGHT) { g_mute[g_cursor] = 0u; if (g_attn[g_cursor] > 0u)  g_attn[g_cursor]--; draw_ui(); }
        if (g_pad_pressed & J_LEFT)  { g_mute[g_cursor] = 0u; if (g_attn[g_cursor] < 15u) g_attn[g_cursor]++; draw_ui(); }
        if (g_pad_pressed & J_A)     { g_mute[g_cursor] = (u8)(g_mute[g_cursor] ^ 1u); draw_ui(); }
        if (g_pad_pressed & J_B)      { goto_version((u8)((g_ver + 1u) % NSONGS)); draw_ui(); }
        if (g_pad_pressed & J_OPTION) { goto_version((u8)((g_ver + NSONGS - 1u) % NSONGS)); draw_ui(); }

        apply_mix();         /* keep live volumes in force (overrides baked SET_ATTN) */
        Sounds_Update();
        Bgm_DebugSnapshot(&g_dbg);
        PrintHex(SCR_1_PLANE, 0, 6, 14, (u16)g_dbg.song_frame, 4);
    }
}
