/* XENON 2 - SOUND MIXING DESK (test cartridge, 03.08.2026) For SETTING the
   balance between music and effects, on REAL HARDWARE. The emulator is
   only of limited use for that question: the T6W28 sounds different on the
   device, and different again through the NGPC speaker than through
   headphones. CONTROLS UP/DOWN - music volume, 0 = full ... 15 = silent (2
   dB per step). LEFT/RIGHT - select effect (all twelve from sfx_orig.h). A
   - play the effect once. B - rapid fire on/off, plays the effect at the
   GAME RATE (2.3 shots/s, lvl_firerate_stage[0] = 26/60 s). OPTION - cycle
   the music source: OFF -> ARRANGEMENT (as in the game) -> ORIGINAL
   (converted from XENON2.EXE) -> ORIGINAL JINGLE -> OFF. The value set
   here is the same one that sits in xenon.c as MUSIC_ATTN - whatever
   sounds good here can be entered there unchanged. !! carthdr.h MUST come
   first and this .c must be the FIRST object in the link: f_header is
   empty, so the first f_const lands at 0x200000 and is therefore the
   cartridge header. */
#include "ngpc.h"
#include "carthdr.h"
#include "library.h"
#include "sounds.h"
#include "sfx_orig.h"
/* TWO MUSIC SOURCES, so the balance can be checked against both:
   game_theme.c THEME - the MIDI arrangement that runs in the game.
   xenon_songs.c X_THEME - converted from XENON2.EXE, so the ORIGINAL sound
   of the PC speaker driver. X_JINGLE - the short jingle, also from the
   EXE. !! ONLY ONE OF THE TWO MAY SUPPLY THE DEFAULT NOTE_TABLE. Both
   provide it under exactly the same name, behind the same #ifdef. SETTING
   the define once is not enough - from then on it applies to every further
   #include, and the second one answers with "THC1-Error-262: Redeclaration
   of 'NOTE_TABLE'". It has to be TAKEN AWAY again after the first file.
   Which table a song uses is decided by Bgm_SetNoteTable() at runtime
   anyway; the default is only needed because sounds.c links it. */
#define XENON_SONGS_PROVIDE_DEFAULT_NOTE_TABLE
#include "Musik/game_theme.c"
#undef XENON_SONGS_PROVIDE_DEFAULT_NOTE_TABLE
#include "Musik/xenon_songs.c"

#define SFX_COUNT 12
#define MUSIC_COUNT 4            /* off, arrangement, original, original jingle */
#define FIRE_PERIOD 26u          /* 1/60 s, like lvl_firerate_stage[0] in the game */

/* !! ALL SET IN main(), NO INITIALISERS. Measured: a "static u8
   g_music_attn = 2u;" arrived as 0 in this ROM - the start value from
   f_data was not in place when main() ran. In xenon.c that does not show,
   because everything is set during startup there anyway (see CLAUDE.md
   6.0a on g_hs_shown). The same holds here: what counts gets written. */
static u8 g_pad, g_pad_prev, g_pad_pressed;
static u8 g_music_attn;          /* = MUSIC_ATTN from xenon.c */
static u8 g_sel;                 /* selected effect */
static u8 g_music;               /* 0 off, 1 arrangement, 2 original, 3 jingle */
static u8 g_auto;                /* rapid fire */
static u8 g_fire_cd;

/* Short names for the display. Order = the SFX_* numbers from sfx_orig.h.
   cc900 does not take const char* tables (see CLAUDE.md 6.14), hence a
   fixed character array with 10 columns per entry. */
static const char g_names[SFX_COUNT][11] = {
    "SCHUSS    ", "EXPLOSION ", "SMARTBOMB ", "FLAMER    ",
    "LASER     ", "LASERSWEEP", "ENERGIE   ", "WAFFE     ",
    "TAUCHEN   ", "SHOP TUER ", "SHOP CURS ", "SHOP KAUF "
};

/* Names of the music sources, same width as the effect names. */
static const char g_music_names[MUSIC_COUNT][11] = {
    "AUS       ", "ARRANGEMNT", "ORIGINAL  ", "ORIG JINGL"
};

static void music_start(void)
{
    switch (g_music) {
    case 1:     /* the arrangement that runs in the game */
        Bgm_SetNoteTable(THEME_NOTE_TABLE);
        Bgm_StartLoop4Ex(THEME_CH0, THEME_CH0_LOOP, THEME_CH1, THEME_CH1_LOOP,
                         THEME_CH2, THEME_CH2_LOOP, THEME_CHN, THEME_CHN_LOOP);
        break;
    case 2:     /* Megablast, converted straight from XENON2.EXE */
        Bgm_SetNoteTable(X_THEME_NOTE_TABLE);
        Bgm_StartLoop4Ex(X_THEME_CH0, 0, X_THEME_CH1, 0,
                         X_THEME_CH2, 0, X_THEME_CHN, 0);
        break;
    case 3:     /* the short jingle, also from the EXE */
        Bgm_SetNoteTable(X_JINGLE_NOTE_TABLE);
        Bgm_StartLoop4Ex(X_JINGLE_CH0, 0, X_JINGLE_CH1, 0,
                         X_JINGLE_CH2, 0, X_JINGLE_CHN, 0);
        break;
    default:
        Bgm_Stop();
        return;
    }
    /* !! SET AFTER the start. Bgm_StartLoop4Ex runs through Bgm_Start, and
       that clears the fade offset; the master does survive it, but this
       order keeps the two together and makes the value independent of what
       is in RAM at power-on (hardware does not clear RAM). */
    Bgm_SetMasterAttn(g_music_attn);
}

/* !! THE SCREEN IS 20 CHARACTERS WIDE (160 px / 8). Everything from column
   20 on is gone, and a value that reaches into a label eats it - the first
   version wrote from column 15 and turned "HOCH/RUNTER" into "HOCH/RU0TE".
   Values therefore sit right-aligned on lines of their own. */
static void show_attn(void)
{
    /* The step and the matching attenuation in dB, 2 dB per step. Print
       two digits: 10..15 needs two, otherwise the old tens digit would
       stay behind. */
    PrintDecimal(SCR_1_PLANE, 0, 8, 4, (u16)g_music_attn, 2);
    PrintString(SCR_1_PLANE, 0, 12, 4, "-");
    PrintDecimal(SCR_1_PLANE, 0, 13, 4, (u16)(g_music_attn * 2u), 2);
    PrintString(SCR_1_PLANE, 0, 15, 4, "DB");
}

static void show_sel(void)
{
    PrintString(SCR_1_PLANE, 0, 8, 5, g_names[g_sel]);
}

static void show_state(void)
{
    PrintString(SCR_1_PLANE, 0, 8, 6, g_music_names[g_music]);
    PrintString(SCR_1_PLANE, 0, 8, 7, g_auto ? "AN " : "AUS");
}

void main(void)
{
    InitNGPC();
    SetBackgroundColour(RGB(0, 0, 0));
    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);
    SysSetSystemFont();
    SetPalette(SCR_1_PLANE, 0, RGB(0,0,0), RGB(15,15,15), RGB(15,15,15), RGB(15,15,15));

    /* Columns 0..19, the screen gives no more. */
    PrintString(SCR_1_PLANE, 0, 0, 1,  "XENON 2 TONMISCHER");
    PrintString(SCR_1_PLANE, 0, 0, 4,  "MUSIK");
    PrintString(SCR_1_PLANE, 0, 0, 5,  "EFFEKT");
    PrintString(SCR_1_PLANE, 0, 0, 6,  "QUELLE");
    PrintString(SCR_1_PLANE, 0, 0, 7,  "DAUER");
    PrintString(SCR_1_PLANE, 0, 0, 10, "HOCH/RUNTER MUSIK");
    PrintString(SCR_1_PLANE, 0, 0, 11, "LINKS/RECHTS EFFEKT");
    PrintString(SCR_1_PLANE, 0, 0, 12, "A  EFFEKT SPIELEN");
    PrintString(SCR_1_PLANE, 0, 0, 13, "B  DAUERFEUER");
    PrintString(SCR_1_PLANE, 0, 0, 14, "OPT  MUSIKQUELLE");
    PrintString(SCR_1_PLANE, 0, 0, 16, "STUFE OBEN IST");
    PrintString(SCR_1_PLANE, 0, 0, 17, "MUSIC_ATTN XENON.C");

    /* Start values here, not as initialisers - see the head of the
       variables. */
    g_music_attn = 2u;              /* like MUSIC_ATTN in xenon.c */
    g_sel = 0u;                     /* SFX_SHOT */
    g_music = 1u;                   /* arrangement, as in the game */
    g_auto = 0u;
    g_fire_cd = 0u;

    Sounds_Init();
    music_start();
    show_attn();
    show_sel();
    show_state();

    for (;;) {
        { u8 v = VBCounter; while (VBCounter == v) ; }

        g_pad_prev    = g_pad;
        g_pad         = JOYPAD;
        g_pad_pressed = g_pad & (u8)(~g_pad_prev);

        if ((g_pad_pressed & J_UP) && g_music_attn > 0u) {
            g_music_attn--;
            Bgm_SetMasterAttn(g_music_attn);
            show_attn();
        }
        if ((g_pad_pressed & J_DOWN) && g_music_attn < 15u) {
            g_music_attn++;
            Bgm_SetMasterAttn(g_music_attn);
            show_attn();
        }
        if (g_pad_pressed & J_LEFT) {
            g_sel = (u8)((g_sel + SFX_COUNT - 1u) % SFX_COUNT);
            show_sel();
        }
        if (g_pad_pressed & J_RIGHT) {
            g_sel = (u8)((g_sel + 1u) % SFX_COUNT);
            show_sel();
        }
        if (g_pad_pressed & J_A) {
            sfx_orig_play(g_sel);
        }
        if (g_pad_pressed & J_B) {
            g_auto = (u8)(g_auto ? 0u : 1u);
            g_fire_cd = 0u;
            show_state();
        }
        /* OPTION cycles the music source: off -> arrangement -> original
           -> original jingle -> off. One key for all states, because the
           other four are already taken. */
        if (g_pad_pressed & J_OPTION) {
            g_music = (u8)((g_music + 1u) % MUSIC_COUNT);
            music_start();
            show_state();
        }

        /* Rapid fire at the game rate. This loop runs at 60 Hz and the
           fire rate is given in 1/60 s - so the counter fits directly,
           without converting to the 20 fps of the game loop. */
        if (g_auto) {
            if (g_fire_cd == 0u) {
                sfx_orig_play(g_sel);
                g_fire_cd = FIRE_PERIOD;
            } else {
                g_fire_cd--;
            }
        }

        Sounds_Update();
        PrintHex(SCR_1_PLANE, 0, 17, 18, (u16)VBCounter, 2);   /* sign of life */
    }
}
