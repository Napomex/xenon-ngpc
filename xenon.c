// xenon.c ??? Shoot em Up fuer Neo Geo Pocket Color
#include "ngpc.h"

#include "carthdr.h"
#include "library.h"
#include "PNG/Map/map.h"   /* NGPC Tile Analyzer Output ??? NEUE Map (562 Tiles, 256 Sprites) */
#include "worm_paths.h"    /* Wandwurm-Kopf-Pfade (Schritt 19), aus DOS S1/PATHS.BIN ??? NICHT im Tool-Export */
#include "spr_sections.h"  /* 30.07.2026: abschnittsweiser Sprite-Pool, erzeugt von
                              tools/gen_spr_sections.js (SECS/POOL/BACK/FWD). Loest den
                              statischen Gesamt-Pool ab: Spitze 250 statt 318 Kacheln. */
#include "PNG/Logo/logo_tiles.h"   /* Titelscreen-Logo, generiert aus PNG/RAW/Logo.png (Node-Skript) */
#include "Musik/Titel.c"           /* liefert das Default-NOTE_TABLE-Symbol fuer sounds.rel (Linker) */
#include "sounds.h"                 /* NGPCraft T6W28-Sound-Treiber (sounds.c) */
#include "Musik/game_theme.c"      /* THEME_* : XG-Steel-Fassung (Xenon2 XG.mid), Mix 2/1/1/5 gebacken.
                                       Kein NOTE_TABLE-Default (ifdef aus) -> kein Clash mit Titel.c. */

/* Musik: das Titelthema (Menue + Spiel). music_start_theme() startet es IMMER
   von vorne (Nutzerwunsch: bei Spielstart und nach dem Shop von Beginn).
   25.07.2026 gekuerzt (Nutzer): Song endet bei ~2:10 und wiederholt ab ~1:37 -
   die THEME_*_LOOP-Offsets kommen aus tools/music/cut_theme.js (alle 4 Stimmen
   auf demselben Song-Frame -> bleiben ueber die Schleife synchron). */
static void music_start_theme(void) {
    Bgm_SetNoteTable(THEME_NOTE_TABLE);
    Bgm_StartLoop4Ex(THEME_CH0, THEME_CH0_LOOP, THEME_CH1, THEME_CH1_LOOP,
                     THEME_CH2, THEME_CH2_LOOP, THEME_CHN, THEME_CHN_LOOP);
}

/* ============================================================================
   ACHTUNG - DREI ZEILENBEGRIFFE, 19 AUSEINANDER. Vor JEDER Zeilenpruefung hier
   nachsehen, welcher Zaehler gemeint ist. Am 22.07.2026 sind FUENF Fehler
   derselben Sorte aufgefallen (Kamera-Deckel, Boss-Aktivierung, Boss-Anker,
   Shop-Ausloeser, Shop-Ruecksprung), siehe CLAUDE.md ??00.

     g_lvl_row        die Map-Zeile, die gerade OBEN einrollt = Spielerzeile + 19
     g_scroll_y >> 3  die UNTERSTE sichtbare Zeile = wo das Schiff ist
     Zeile aus dem Tool (map.h)   meint die SPIELERPOSITION

   Grund: lvl1_prefill(row_base) legt row_base unten auf Tilemap-Zeile 18 und
   row_base+18 oben auf Zeile 0 (Bildschirm = 152 px = 19 Kachelzeilen) und
   setzt danach g_lvl_row = row_base + 19.

   FAUSTREGEL: Spiel-Ereignisse (Shop, Boss, Checkpoints, Spawns, Kamera-Deckel)
   IMMER gegen g_scroll_y>>3 pruefen. g_lvl_row ist nur fuer das Streaming der
   Kachelzeilen zustaendig. Wer eine Tool-Zeile gegen g_lvl_row prueft, loest
   19 Zeilen zu frueh aus - bei 3 Frames/px rund 15 Sekunden.
   ============================================================================ */
/* Sichtbare Kachelzeilen (152 px / 8). */
#define LVL_SCREEN_ROWS 19u

/* 23.07.2026 (Nutzer): Gegner-Spawn-Vorlauf. Im Original wird eine Welle
   ausgeloest, wenn ihre Map-Zeile am OBEREN Bildrand einrollt (scroll>>4 = erste
   sichtbare Zeile, rendering.md). Unser g_spawn_scroll_row misst aber die
   UNTERSTE Zeile (das Schiff) - also muss der Vorlauf dazu. ABER: nicht unsere
   Bildhoehe (19 Kacheln), sondern die DOS-Sichthoehe, weil die Kachel-Struktur
   1:1 ist (DOS 16px = unsere 8px, Kachelzahlen bleiben), unser Bild aber in
   Kacheln HOEHER ist. DOS: 200px/16 ~ 12,5 Kacheln bzw. die Engine-Konstante
   0xC0 = 192px = 12 (Schiff bei y176 = 11 Kacheln unter dem Rand). = 12.
   Von unten (Schiff) gezaehlt. */
#define SPAWN_LEAD_ROWS 12u

/* 24.07.2026 (Nutzer-HW: "die Wellen kommen zu frueh, ~72-80px zu weit unten"):
   Alle Pfade sind bildschirm-relativ (lvl_path_map_relative durchgehend 0), die
   Gegner-Bildschirmposition haengt also NUR an lvl_spawn_y (Tool), nicht am
   Timing. Nutzerwunsch: nicht die Pfade/spawn_y verschieben, sondern den
   AUSLOESER nach hinten - jede Welle feuert erst, wenn die Schiffszeile
   SPAWN_TRIGGER_DELAY Kacheln WEITER ist (72px = 9 Kacheln). Rein additiv auf die
   Trigger-Zeile, ohne Vorlauf-Rampe oder Pfaddaten anzufassen. */
#define SPAWN_TRIGGER_DELAY 9u   /* 9 Kacheln = 72 px spaeter */

/* LVL_SCROLL_END_ROW (map.h) benennt die LETZTE Kachelzeile der Map, die als
   OBERSTE Bildschirmzeile stehenbleiben soll. Der Scroll-Zaehler g_scroll_y
   misst dagegen die UNTERSTE sichtbare Zeile - deshalb hier einmal umgerechnet
   und im Scroll-Deckel nur noch dieser Wert verwendet. */
#define LVL_SCROLL_END_TOP_ROW \
    ((u16)LVL_SCROLL_END_ROW >= (LVL_SCREEN_ROWS - 1u) \
        ? (u16)((u16)LVL_SCROLL_END_ROW - (LVL_SCREEN_ROWS - 1u)) : 0u)

/* ===== RNG_DETERMINISTIC (22.07.2026) ??? eigener Zufallsgenerator mit festem
   Startwert fuer Regressionslaeufe. ACHTUNG, WICHTIGE EINSCHRAENKUNG: das
   reicht NICHT, um zwei Builds vergleichbar zu machen. Der Emulator-Treiber
   laesst eine feste Zahl EMULATOR-Frames laufen; ein schnellerer Build schafft
   darin mehr SPIELframes, und schon der Titelbildschirm sieht anders aus. Zwei
   unterschiedlich schnelle Builds laufen also zwangslaeufig auseinander, auch
   ohne jeden Fehler. Fuer einen echten Vergleich muesste der Treiber
   Spielframes zaehlen statt Emulator-Frames. ===== */
#define RNG_DETERMINISTIC 0
#if RNG_DETERMINISTIC
static unsigned short g_trng = 0xACE1u;
static unsigned short test_rand16(void) {
    g_trng ^= (unsigned short)(g_trng << 7);
    g_trng ^= (unsigned short)(g_trng >> 9);
    g_trng ^= (unsigned short)(g_trng << 8);
    return g_trng;
}
#define GetRandom(n) ((unsigned short)(test_rand16() % (unsigned short)((n) + 1u)))
#define QRandom()    test_rand16()
#endif

/* TEST-Modus: 1 = kein Auto-Scroll/Drift, Level per Hoch/Runter manuell abfahren
   (zum Vergleich mit dem Map-Builder-Screenshot). 0 = normales Spiel. Vor dem
   "richtigen" Build wieder auf 0 stellen. */
#define TEST_MANUAL_SCROLL 0

/* Scroll-Tempo in Frames pro Pixel. 6 = normales Spieltempo (10px/sec,
   ein Tile in 0.8s). Vorher 10 (6px/s) ??? auf Nutzerwunsch beschleunigt. */
#define SCROLL_TICK_THRESHOLD 3u   /* 30-fps-Cap 17.07.: 6->3 (Scroll-Rate x2, ~10px/s wie bei 60fps@6) */

/* Tilt-Animation: ein Stufenwechsel (M<->L1<->L2 bzw. M<->R1<->R2) alle
   TILT_STEP_FRAMES Frames = 5 Stufen/Sekunde ??? gilt vorwaerts UND rueckwaerts
   (beim Loslassen laeuft die Animation ueber die Zwischenstufe zurueck).
   Nutzerkorrektur 11.07.2026: etwas schneller (war 15 = 4 Stufen/s). */
#define TILT_STEP_FRAMES 6u   /* 30-fps-Cap: 12->6 (Dauer halbiert, gleiche Sekunden) */

/* Schiffstempo: PLAYER_SPEED_STEP/16 px pro Frame (Subpixel-Akku).
   Nutzerkorrektur 11.07.2026: 90 px/s war zu schnell, jetzt 60 px/s ->
   16/16 = 1.0 px/Frame EXAKT bei 60fps (g_move_step bleibt dabei immer
   genau 1 ??? die "bis zu g_move_step Pixel"-Bewegungslogik von der 90px/s-
   Korrektur bleibt trotzdem stehen, falls spaeter wieder >60px/s gewuenscht
   sind). */
#define PLAYER_SPEED_STEP 16u   /* 19.07.2026 (Nutzerbefund "Schiff zu schnell"): 32->16.
   Basis = DOS-Speed-Level 0 = 3 px/Tick = 3:1 zum Scroll (1 px/Tick). Bei uns:
   16/16 = 1 px/Frame, Scroll 1px/3F=0,333 -> 3:1 wie im Original. 32 war 6:1 =
   DOS-Level 1, also eine Stufe zu schnell. */
/* Speedup-Pickup (DOS-Pickup-Typ 0, "S", 19.07.2026): jede Stufe erhoeht das
   Schiffstempo (g_tune_speed) um PLAYER_SPEED_STAGE_STEP, gedeckelt auf MAX.
   Basis 16 + Stufe*16 -> 16/32/48 = DOS-Level 0/1/2 = 3/6/9 px/Tick exakt. */
#define PLAYER_SPEED_STAGE_STEP 16u   /* +1 px/Frame pro Stufe */
#define PLAYER_SPEED_STAGE_MAX  2u    /* 3 Level (0/1/2) wie im Original */

/* Rueckwaertsflug (RUNTER am unteren Bewegungslimit gedrueckt halten):
   Budget in Frames (~2 s am Stueck), laedt sich 1:1 wieder auf, sobald
   nicht rueckwaerts geflogen wird. Das Tempo beschleunigt langsam von 0
   auf BACK_SPEED_MAX/32 px pro Frame ??? traeger Gegenschub gegen den Strom. */
#define BACK_BUDGET_MAX   120u
#define BACK_SPEED_MAX    8u   /* 30-fps-Cap: 4->8 (Rueckwaerts-Tempo x2, gleiche px/s) */
#define BACK_ACCEL_FRAMES 4u   /* 30-fps-Cap: 8->4 (Dauer halbiert) */
/* Harte Distanz-Grenze fuers Zurueckscrollen: maximal 2 Tiles (16px) am
   Stueck. Wird pro Vorwaerts-px wieder aufgefuellt (dadurch auch automatisch
   korrekt am Levelanfang: startet bei 0). Technische Obergrenze waere die
   Ring-Historie von ~90px (~12 Terrain-Zeilen unter der Bar) ??? weiter
   zurueck staenden Muell-Zeilen im Bild. */
#define BACK_PX_MAX 16u

// --- Tile-Slots --
#define TILE_BAR_BASE  148   /* 14 bar tiles: 148-161 */
/* Level-1 Terrain VRAM-Basis; LVL_TILE_DATA_COUNT kommt aus Tiles_Lvl_1.h */
#define LVL1_VRAM_BASE 254u

/* Einheitlicher Sprite-Tile-Pool: Schiff (S1-S35), Schuss (S36), Ziffern
   (S37-S46) und Gegner (S47-S54, S100-S107) kommen alle aus demselben Tile-
   Analyzer-Sprite-Streifen (lvl_sspr_a/b_idx, lvl_meta_*) ??? ein einziger
   Tile-/Paletten-Upload fuer alle Kategorien statt getrennter handgebauter
   Systeme. Seit dem Map-Update 06.07.2026 liegen die dafuer benoetigten
   rohen Tiledaten NICHT mehr in einem einzigen zusammenhaengenden Block
   (der Export hat dazwischen jetzt ~43 unbenutzte Deko-/Belohnungs-Sprites
   S55-S99 sowie S108-S125 eingefuegt) ??? daher wie beim Terrain (?? oben)
   eine kompaktierte Lookup-Tabelle (spr_raw_used/spr_raw_remap) statt der
   fruehren simplen Index-Subtraktion. S47-S54 (alte Gegner-Animation) ist
   in keinem aktuellen Spawn referenziert, aber vorsorglich mit eingebunden.
   Map-Update 06.07.2026 (2. Runde): lvl_sspr_anim_head hat jetzt VIER
   Eintraege (47, 55, 72, 100) ??? Kopf 55 (17 Frames, S55-S71) wird vom neuen
   4. Spawn benutzt und ist daher mit eingebunden; Kopf 72 (S72-S79) ist wie
   Kopf 47 aktuell unbenutzt UND wuerde die 454-511-Reserve komplett
   aufbrauchen (siehe spr_tile_vram_init) ??? bewusst NICHT eingebunden, siehe
   CLAUDE.md ??12. Kuenftige Spawns, die Kopf 47 oder 72 nutzen, brauchen erst
   dann eine Erweiterung dieser Tabelle.
   Map-Update 06.07.2026 (4. Runde): S99 (Standbild, Map-Objekt-Schuss
   "bullet_on_loop", siehe mapobj_bullet_update() weiter unten) mit
   eingebunden ??? einzelnes Tile, kein Overlay.
   Map-Update 07.07.2026 (5. Runde): S99 (Blumen-Schuss ??? war in der 4. Runde
   trotz Kommentar NIE tatsaechlich eingebunden, raw-Index 375 fehlte in
   spr_raw_used; sichtbar als falsches Sprite, gemeldet vom Nutzer 07.07.2026
   nach Emulator-Test), S149-151 (Schuss-Basis + Power-Stufen, siehe
   lvl_power_stage_bullet_spr ??? NICHT S153/154, das war ein Fehlgriff der
   4./5. Runde: S153/154 sehen im Spiel falsch aus, S149 ist der korrekte
   Basis-Schuss, vom Nutzer per Emulator-Test bestaetigt), S155-158
   (Schubduesen-Animationen, Koepfe 155/157) und S144 (Waffen-Modul, alle
   5 Animationsframes inkl. b-Overlay, siehe SPR_WPMODULE) neu eingebunden.
   Tabelle diesmal KOMPLETT aus map.h neu berechnet (nicht nur um neue
   Eintraege ergaenzt) ??? die Annahme, dass raw-Indizes bestehender S-Nummern
   zwischen Map-Exporten stabil bleiben, war falsch (S99 ist der Beweis).
   Map-Update 07.07.2026 (Nacht, 2): Kopf 47 (S47-54, 14 Tiles) rausgenommen
   ??? Spawn 5 fiel bisher faelschlich auf diese Animation zurueck (siehe
   metaenemy_spawn), jetzt korrekt als Metasprite behandelt, Kopf 47 ist
   dadurch von KEINEM Spawn mehr referenziert. Dafuer S80/81/82/83/84/85
   (Metasprite-Zellen 5/7/8/9/10, 8 Tiles) neu rein ??? Netto 6 Tiles weniger. */
#define SPR_ROM_BASE   218u
/* ===== Metasprite-Zellen spiegeln (28.07.2026) =====
   Bit0 = horizontal, Bit1 = vertikal - dieselbe Kodierung wie lvl_rotset_flip.
   Spart Kacheln, wenn eine Zelle nur die gespiegelte Fassung einer anderen ist.
   Rueckfall fuer map.h-Exporte davor: kein Spiegeln, also exakt wie bisher.
   META_FLIP_OF() ist die EINZIGE Lesestelle - so bleibt der Rueckfall an einem
   Ort und die Zeichenpfade sehen in beiden Faellen gleich aus. */
/* 28.07.2026: die map.h liefert lvl_meta_flip - direkt lesen. Der frueher hier
   stehende #ifdef-Rueckfall haette den neuen Export STILL ignoriert, weil der
   Exporter das Praesenz-Define gar nicht schreibt. */
#define META_FLIP_OF(i)  ((u8)lvl_meta_flip[(i)])
#define META_FLIP_MASK(f) ((u8)((((f) & 1u) ? (u8)SPR_HFLIP : 0u) | (((f) & 2u) ? (u8)SPR_VFLIP : 0u)))
/* Map-Update 12.07.2026: Wurm-Rotationssaetze (lvl_rotset_idx, Schritt 12) referenzieren
   12 rohe Sprite-Tiles (424-435), die vorher von KEINEM erreichbaren S-Sprite gebraucht
   wurden (Luecke in spr_raw_used zwischen 423 und 436) ??? 138->150. Neuer physischer
   VRAM-Bereich in spr_tile_vram_init() ist **1-12** (siehe dortigen Kommentar fuer
   die volle Historie ??? 355-366 und 148-159 waren beides Fehlversuche, kollidierten
   mit dem Bar-Shift- bzw. dem TILE_BAR_BASE-Bereich). Der GESAMTE 0xA000-Adressraum
   32-511 ist inzwischen lueckenlos von Font+Terrain+Sprite+Bar-Shift+Bar-Kanonisch
   belegt (fuenf Schreiber, nicht drei/vier wie in der alten CLAUDE.md-Notiz "0
   frei" angenommen) ??? einzige verbleibende Luecke ist 0-31. */
#define SPR_TILE_COUNT 332u   /* 19.07.2026: 234->240, Wandwurm-Rotsets 0/1 (raw 427/429/430/431/433/434, Luecke 423-434) via gen_spr_compaction.js.
                                 18.07.2026: 177->183, Belohnungs-Icons S123/130/157/171
                                 (raw 454,477,505,506,526,527) nachgetragen (reward_icon_for);
                                 zuvor 175->177 Schubduesen-Anim S155/156 (raw 503/504).
                                 Map-Update 15.07.2026: 140->166, Waffen-/Shop-System
/* Map-Update 13.07.2026: der Tool-Export hatte bisher ZWEI getrennte 2-Frame-
   Animationen fuer die Schubduesen (Kopf 155 rueckwaerts, Kopf 157 vorwaerts) ???
   im neuen Export ist nur noch EINE davon uebrig (Kopf 149, siehe
   lvl_sspr_anim_head), die andere wurde im Tool geloescht (siehe map.h-Header-
   Warnung "Sprite-Animation mit Kopf 157 geloescht" ??? 157 war laut altem
   Tool-Screenshot Screenshots\S9.png die VORWAERTS-Variante, 155/149 ist
   also die ueberlebende RUECKWAERTS-Grafik). Nutzeransage: die fehlende
   Richtung per V-Flip aus der verbliebenen erzeugen, beide sind ohnehin
   spiegelbildlich. Rueckwaerts bleibt deshalb ungeflippt (native Grafik),
   vorwaerts bekommt SPR_VFLIP (siehe thrust_update()/draw_sprites()) ???
   Korrektur ggue. der ersten Fassung dieser Session, die es genau andersrum
   hatte. */
#define SPR_S_THRUST_HEAD 155u  /* Kopf der 2-Frame-Schubduesen-Animation 155/156.
                                    Map-Update 18.07.2026: neue map.h verschiebt die
                                    Sprites ab S117 (Offset waechst 122->123/127->130/
                                    132->137/138->144/149->155). Die ueberlebende
                                    Rueckwaerts-Thrust-Anim war alt S149/150, ist jetzt
                                    S155/156 (anim_head[10], len 2, speed 3 ??? identisch).
                                    Vorwaerts weiterhin per SPR_VFLIP aus derselben Grafik. */
#define SPR_S_DIGIT0   37u   /* 37-46: Ziffern 0-9 */
#define SPR_S_ENEMY0   47u   /* 47-54: 8 Animationsframes des Gegners (Kopf 47, aktuell unbenutzt) */

/* Schiff-Metasprite: 7 Zellen je Kippzustand (lvl_meta_*). Reihenfolge im
   Export: 0=Ship_L1 1=Ship_L2 2=Ship_M 3=Ship_R1 4=Ship_R2 */
#define SHIP_META_L1 0u
#define SHIP_META_L2 1u
#define SHIP_META_M  2u
#define SHIP_META_R1 3u
#define SHIP_META_R2 4u
#define SHIP_CELLS   7u

/* Schubduesen-Position relativ zum Schiffsursprung (x,y), NUR fuer die
   neutrale Kippstufe (M) ??? Positionsanpassung je Kippstufe (L1/L2/R1/R2)
   ist bewusst noch offen (Nutzeransage 07.07.2026: "das Kippen machen wir
   spaeter"). Schiff = 24x24px-Box (siehe lvl_meta_dx/dy: Nase oben mitte,
   Fluegel-Zellen bei dx=0/dx=16 auf dy=8-16, Heckreihe dy=16-24).
   Nutzerkorrektur 10.07.2026: rechte und linke Duese brauchen je Richtung
   UNTERSCHIEDLICHE Korrekturen ??? der frueher geteilte THRUST_R_DX-Offset
   (gleich fuer vorwaerts/rueckwaerts) reicht dafuer nicht mehr, daher jetzt
   je Richtung ein eigener rechter Offset (THRUST_*_R_DX). */
#define THRUST_FWD_DX    4    /* Vorwaerts-Schub links: unter dem Fluegel (Heck-Trail),
                                  Nutzerkorrektur 10.07.2026: 5px nach rechts (vorher 0);
                                  Nutzerkorrektur 11.07.2026: 1px nach links (vorher 5) */
#define THRUST_FWD_DY   20    /* Nutzerkorrektur 10.07.2026 (Nacht): 1px runter (vorher 19) */
#define THRUST_FWD_R_DX 12    /* Vorwaerts-Schub rechts, Nutzerkorrektur 10.07.2026:
                                  2px nach links (vorher THRUST_R_DX=14) */
#define THRUST_BACK_DX    3   /* Ruecklauf-Schub links: oberhalb/vorne am Fluegel,
                                  Nutzerkorrektur 10.07.2026: 3px nach rechts (vorher 0) */
#define THRUST_BACK_DY    7   /* Nutzerkorrektur 10.07.2026: 3px runter (vorher 4) */
#define THRUST_BACK_R_DX 13   /* Ruecklauf-Schub rechts, Nutzerkorrektur 10.07.2026:
                                  1px nach links (vorher THRUST_R_DX=14) */

/* Level ist seit dem Map-Update auf 293 Zeilen gewachsen (vorher 140) ??? die
   Terrain-Tiles fuer die GESAMTE Map (237 eindeutige Rohtiles) passen nicht
   gleichzeitig ins VRAM-Budget. Loesung: Level in 2 Segmente geteilt, die
   NIE gleichzeitig geladen sind (Segment-Wechsel = VRAM-Neubefuellung +
   Shop-Platzhalterscreen, siehe STATE_SHOP/shop_resume()):
     Segment A: Zeilen 0-195   (135 Rohtiles)
     Segment B: Zeilen 193-292 (162 Rohtiles, 2-Zeilen-Overlap mit A als
                Sicherheitsmarge fuer die Ringzeilen, die beim Umschalten
                noch sichtbar sind)
   Je Segment ein eigenes used/remap-Paar (Node-Skript im Scratchpad, parst
   map.h fuer den jeweiligen Zeilenbereich inkl. Animationsframes + Map-
   Objekt-Wilt-Tiles). WICHTIG: bei jedem Map-Update BEIDE Segmente neu
   generieren, nicht nur ergaenzen (siehe Kommentar-Historie beim alten
   S99-Bug ??? Rohindizes verschieben sich beim Re-Export global). */
/* Zeile, bei der Segment A pausiert (Shop-Platzhalter, siehe scroll_update()/
   shop_enter()/shop_resume()). LVL1_SEGB_ROW_BASE liegt 2 Zeilen davor, damit
   Segment B's Tile-Tabelle auch die beim Umschalten noch sichtbaren Ringzeilen
   193/194 abdeckt. */
#define LVL1_SEG_SWITCH_ROW 195u
#define LVL1_SEGB_ROW_BASE  193u

/* used[0]=0 (garantiert leeres/transparentes Rohtile) ist absichtlich immer
   an Position 0 reserviert ??? der b_idx==0-Fallback ("kein B-Overlay") in
   lvl1_put_cell/mapobj_apply_row/anim_update loest ueber remap[0] auf; ohne
   diese Reservierung landet dort zufaellig das erste ECHTE Tile des Segments
   statt Transparenz (Bug 10.07.2026: Blumen-Wilt-Tiles, alle mit b_idx=0,
   zeigten dadurch Rohtile 1 als falsches Overlay UND blockierten wegen
   terrain_solid ??? siehe dort ??? weiterhin die Durchfahrt). */
/* Nutzerwunsch 11.07.2026 (sehr spaet): "dynamischer Tile-Austausch" beim
   Segment-Wechsel ??? 43 Rohtiles (SHARED_PREFIX_COUNT) werden von BEIDEN
   Segmenten gebraucht (durchgehende Wandtextur). Bisher lagen sie in A und B
   an unterschiedlichen kompaktierten Positionen (nur 3 von 136 zufaellig
   gleich), der Wechsel musste deshalb ALLE 162 B-Tiles neu hochladen. Jetzt
   bewusst an den gleichen Offsets 0..42 in BEIDEN used-Tabellen platziert
   (siehe lvl_tile_used_B), Rest je Segment dahinter ??? dieselben Rohindizes
   landen dadurch garantiert im selben physischen VRAM-Slot (g_lvl_vram[j]
   ist unabhaengig vom Segment), der gemeinsame Block muss beim Wechsel gar
   nicht mehr anfasst werden. Siehe build_lvl1_from()/shop_resume(). */
#define SHARED_PREFIX_COUNT 9u
#define LVL1_TILE_USED_COUNT_A 134u
static const u16 lvl_tile_used_A[LVL1_TILE_USED_COUNT_A] = {
    0, 112, 113, 172, 173, 180, 181, 182, 183, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 75, 76, 77, 78,
    79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94,
    166, 167, 174, 175, 176, 177, 178, 179, 184, 185, 186, 187, 188, 189, 190, 191,
    192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 211, 212, 213, 214, 215,
    216, 217, 218, 219, 220, 221,
};

static const u8 lvl_tile_remap_A[LVL_TILE_DATA_COUNT] = {
    0, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
    72, 73, 74, 75, 0, 0, 0, 0, 0, 0, 0, 76, 77, 78, 79, 80,
    81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 96, 97, 0, 0, 0, 0, 3, 4, 98, 99,
    100, 101, 102, 103, 5, 6, 7, 8, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 0, 0, 0, 0, 0,
    0, 0, 0, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define LVL1_TILE_USED_COUNT_B 142u
static const u16 lvl_tile_used_B[LVL1_TILE_USED_COUNT_B] = {
    0, 112, 113, 172, 173, 180, 181, 182, 183, 95, 96, 97, 98, 99, 100, 101,
    102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
    138, 142, 143, 144, 145, 146, 147, 148, 160, 161, 168, 169, 170, 171, 222, 223,
    224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,
    256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271,
    272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287,
    288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301,
};

static const u8 lvl_tile_remap_B[LVL_TILE_DATA_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    1, 2, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47, 0, 0, 48, 0, 0, 0, 49, 50,
    51, 52, 53, 54, 55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    56, 57, 0, 0, 0, 0, 0, 0, 58, 59, 60, 61, 3, 4, 0, 0,
    0, 0, 0, 0, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};





























/* Aktives Segment (0=A, 1=B) ??? von lvl1_select_segment() gesetzt, von
   lvl1_put_cell/mapobj_apply_row/anim_update/build_lvl1 gelesen. */
static const u16 *g_lvl_tile_used;   /* u16 seit 18.07.2026 (Rohindizes bis 293, siehe lvl_tile_used_A/B) */
static const u8 *g_lvl_tile_remap;
static u16       g_lvl_tile_used_count;
/* ============ Terrain-Abschnitte (21.07.2026) ============
   Ersetzt die starre Zweiteilung A/B. Erzeugt von tools/gen_terrain_sections.js
   mit FORTSCHREIBENDER Slot-Vergabe: eine Rohkachel behaelt ihren kompaktierten
   Offset, solange sie noch gebraucht wird. Dadurch kippt beim Abschnittswechsel
   kein Offset, waehrend der alte Bildinhalt noch im Ring steht - genau das war
   der Grund, warum der Wechsel bisher nur im Shop moeglich war.
   Nachgewiesen: 3387 Zeile/Abschnitt-Kombinationen ohne Fehler
   (scratchpad/verify_sections.mjs). Vorlauf 40 Zeilen zurueck, 34 voraus. */
#define LVL1_TERR_SEC_COUNT 4u
#define LVL1_TERR_ZONE 183u
static const u16 lvl1_terr_sec_row[LVL1_TERR_SEC_COUNT] = { 0u, 125u, 169u, 258u };

static const u16 lvl1_terr_used[LVL1_TERR_SEC_COUNT][LVL1_TERR_ZONE] = {
  {  /* Abschnitt 0 */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 75, 76, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
    163, 164, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 185, 186,
    187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202,
    203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 326, 327, 328, 329,
    330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0,
  },
  {  /* Abschnitt 1 */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 66, 67, 77, 78, 91, 92, 93,
    94, 111, 112, 113, 114, 119, 120, 121, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 75, 76, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
    163, 164, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 122, 127,
    128, 129, 130, 161, 162, 165, 166, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 208, 209, 210, 211, 212, 213, 214, 326, 327, 328, 329,
    330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0,
  },
  {  /* Abschnitt 2 */
    0, 1, 2, 95, 96, 5, 6, 97, 98, 99, 100, 101, 102, 103, 104, 105,
    106, 17, 18, 107, 108, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 66, 67, 77, 78, 91, 92, 93,
    94, 111, 112, 113, 114, 119, 120, 121, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 75, 76, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
    163, 164, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 122, 127,
    128, 129, 130, 161, 162, 165, 166, 109, 110, 115, 116, 117, 118, 123, 124, 125,
    126, 131, 132, 133, 134, 135, 136, 139, 143, 144, 145, 146, 326, 327, 328, 329,
    330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 147,
    148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 167, 168, 181,
    182, 183, 184, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307,
    308, 309, 310, 0, 0, 0, 0,
  },
  {  /* Abschnitt 3 */
    0, 215, 216, 95, 96, 217, 218, 97, 98, 99, 100, 101, 102, 103, 104, 105,
    106, 219, 220, 107, 108, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
    232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247,
    248, 111, 112, 113, 114, 119, 120, 121, 249, 250, 251, 252, 253, 254, 255, 256,
    257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272,
    273, 274, 169, 170, 275, 276, 277, 278, 175, 176, 279, 280, 179, 180, 122, 127,
    128, 129, 130, 161, 162, 165, 166, 109, 110, 115, 116, 117, 118, 123, 124, 125,
    126, 131, 132, 133, 134, 135, 136, 139, 143, 144, 145, 146, 281, 282, 283, 284,
    285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 311, 312, 313, 314, 315, 147,
    148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 167, 168, 181,
    182, 183, 184, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307,
    308, 309, 310, 316, 317, 318, 319,
  },
};

static const u8 lvl1_terr_remap[LVL1_TERR_SEC_COUNT][LVL_TILE_DATA_COUNT] = {
  {  /* Abschnitt 0 */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 0, 0, 0, 0, 0, 0, 0, 0, 0, 66, 67, 0, 0, 68,
    69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 80, 81, 0, 0, 0, 0, 82, 83, 84, 85, 86, 87, 88,
    89, 90, 91, 92, 93, 0, 0, 0, 0, 94, 95, 96, 97, 98, 99, 100,
    101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116,
    117, 118, 119, 120, 121, 122, 123, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
    134, 135, 136, 137, 138, 139, 140, 141, 142, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
  },
  {  /* Abschnitt 1 */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 41, 42, 0, 0, 0, 0, 0, 0, 0, 66, 67, 43, 44, 68,
    69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 45, 46, 47, 48, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 49,
    50, 51, 52, 0, 0, 0, 0, 53, 54, 55, 94, 0, 0, 0, 0, 95,
    96, 97, 98, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 99, 100, 80, 81, 101, 102, 0, 0, 82, 83, 84, 85, 86, 87, 88,
    89, 90, 91, 92, 93, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    117, 118, 119, 120, 121, 122, 123, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
    134, 135, 136, 137, 138, 139, 140, 141, 142, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
  },
  {  /* Abschnitt 2 */
    0, 1, 2, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 17, 18, 0, 0, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 41, 42, 0, 0, 0, 0, 0, 0, 0, 66, 67, 43, 44, 68,
    69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 45, 46, 47, 48, 3,
    4, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 19, 20, 103, 104, 49,
    50, 51, 52, 105, 106, 107, 108, 53, 54, 55, 94, 109, 110, 111, 112, 95,
    96, 97, 98, 113, 114, 115, 116, 117, 118, 0, 0, 119, 0, 0, 0, 120,
    121, 122, 123, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155,
    156, 99, 100, 80, 81, 101, 102, 157, 158, 82, 83, 84, 85, 86, 87, 88,
    89, 90, 91, 92, 93, 159, 160, 161, 162, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 163, 164, 165, 166, 167, 168, 169, 170, 171,
    172, 173, 174, 175, 176, 177, 178, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
    134, 135, 136, 137, 138, 139, 140, 141, 142, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
  },
  {  /* Abschnitt 3 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
    4, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 19, 20, 103, 104, 49,
    50, 51, 52, 105, 106, 107, 108, 53, 54, 55, 94, 109, 110, 111, 112, 95,
    96, 97, 98, 113, 114, 115, 116, 117, 118, 0, 0, 119, 0, 0, 0, 120,
    121, 122, 123, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155,
    156, 99, 100, 0, 0, 101, 102, 157, 158, 82, 83, 0, 0, 0, 0, 88,
    89, 0, 0, 92, 93, 159, 160, 161, 162, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 2, 5, 6, 17, 18, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47, 48, 56, 57, 58, 59, 60, 61, 62,
    63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78,
    79, 80, 81, 84, 85, 86, 87, 90, 91, 124, 125, 126, 127, 128, 129, 130,
    131, 132, 133, 134, 135, 136, 137, 163, 164, 165, 166, 167, 168, 169, 170, 171,
    172, 173, 174, 175, 176, 177, 178, 138, 139, 140, 141, 142, 179, 180, 181, 182,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
  },
};




















static u8        g_lvl_segment;
static u8 g_terr_sec;        /* aktiver Terrain-Abschnitt */
static u8 g_terr_target;     /* Zielabschnitt waehrend eines Wechsels */
static u16 g_terr_up_idx;    /* naechster Slot beim schrittweisen Upload */

/* Aktiven Terrain-Abschnitt setzen. g_lvl_tile_used/remap zeigen danach auf
   die Tabellen dieses Abschnitts; die Kacheldaten selbst laedt build_lvl1()
   bzw. beim laufenden Wechsel terr_sec_step(). */
static void lvl1_select_section(u8 sec) {
    if (sec >= (u8)LVL1_TERR_SEC_COUNT) sec = (u8)(LVL1_TERR_SEC_COUNT - 1u);
    g_terr_sec            = sec;
    /* BUGFIX 22.07.2026 (Nutzerbericht "Karte am Rand zerstueckelt" nach dem
       Level-Neustart): hier wurde NUR g_terr_sec gesetzt. Stand g_terr_target
       noch auf einem anderen Abschnitt - nach dem Endboss z.B. auf 3 -, dann sah
       terr_sec_step() im naechsten Frame einen offenen Wechsel und lud dessen
       Kacheln ueber den gerade frisch aufgebauten Abschnitt 0: zerrissenes
       Terrain an genau den Stellen, wo sich die Kachelsaetze unterscheiden.
       respawn_do() umging das mit einem eigenen "g_terr_target = sec" (Kommentar
       dort: "keinen halben Wechsel stehen lassen"), game_start() nicht - der
       Fehler traf also auch den normalen Neustart nach Game Over. */
    g_terr_target         = sec;
    g_terr_up_idx         = 0u;
    g_lvl_segment         = sec;   /* Altbestand: 0 = erster Abschnitt */
    g_lvl_tile_used       = lvl1_terr_used[sec];
    g_lvl_tile_remap      = lvl1_terr_remap[sec];
    g_lvl_tile_used_count = (u16)LVL1_TERR_ZONE;
    /* 22.07.2026: die vorberechneten Tilemap-Woerter haengen an
       g_lvl_tile_remap - hier neu bauen. Dreimal pro Level statt 20-mal je
       einrollender Kachelzeile. */
    tile_words_build();
}

static void lvl1_select_segment(u8 segment) {
    /* Rueckwaerts-kompatible Huelle: 0 = erster Abschnitt, 1 = der Abschnitt,
       in dem der Shop liegt. Neuer Code nimmt lvl1_select_section(). */
    lvl1_select_section(segment ? 1u : 0u);
}


/* Bar-Shift-Chunk4 (bar_shift_vram_init) haengt fix an das Ende der
   URSPRUENGLICHEN Terrain-Zone (254-354, 101 Slots) ??? diese Grenze bleibt
   UNVERAENDERT, unabhaengig davon, wie viele Slots ein Segment tatsaechlich
   braucht (135/162). Nicht mit LVL1_TILE_USED_COUNT_A/B verwechseln! */
#define LVL1_FIXED_ZONE_COUNT 101u

/* Terrain-VRAM ist NICHT mehr ein einzelner zusammenhaengender Block: das
   Segment-Budget (bis zu 171 Slots) braucht mehr Platz, als der alte Block
   254-354 (101) hergibt. Zusaetzliche Slots kommen aus zurueckgewonnenem
   Font-Speicher (69-127, siehe font_compact_init) und kleinen frei liegenden
   Luecken ??? wie beim Sprite-Pool (g_spr_tile_vram) ueber eine Indirection-
   Tabelle statt eines einzelnen Basiswerts. Reihenfolge beliebig; 254-354
   bleibt als Chunk erhalten, damit bar_shift_vram_init() (haengt an dessen
   Ende) unveraendert bleibt. */
/* 29.07.2026: 158 -> 179. Die kleinen Tuerme (Anim-Bilder 200-205) und die
   sechs Anemonen (79-86, 182-189) heben das Engpass-Fenster (Zeile 264-267:
   Anemonen bei 232/237/240 im Rueckfenster + Boss-Kammer im Vorlauf) von 158
   auf 185 Kacheln. Gedeckt aus zwei Quellen:
     * die 21 letzten freien VRAM-Kacheln (38-41, 52-68) gehen an die Zone,
       und der 254er-Block laeuft bis 337 (Sprite-Pool beginnt bei 338) -
       zusammen 179 Adressen unterhalb des Pools;
     * die fehlenden 6 kommen aus der in CLAUDE.md §4 dokumentierten Reserve:
       die drei Tentakel-Animationen spielen Bild 4 als WIEDERHOLUNG von
       Bild 2 (Folge 1,2,3,2 - "sieht aus wie 4"), die Strips 197-199 samt
       Splits brauchen damit keinen Slot mehr. Bedarf 185 - 6 = 179.
   Der fruehere 506-511-Rest der Zone ist GESTRICHEN - der Sprite-Pool nutzt
   502-511 wirklich, mit 179+ Slots waere aus dem "in der Praxis nie
   erreichten" Ueberlapp ein echter geworden.
   Der VRAM ist damit RESTLOS vergeben, KEINE freie Kachel und keine Reserve
   mehr. Der naechste Mehrbedarf braucht eine echte Entscheidung (Kandidaten:
   weitere Bild-Wiederholungen, Grafik zusammenlegen, Paletten-Budget). */
/* 30.07.2026: 177 -> 183. Der Export vom 19:29 (Deckel-Korrektur der Anemone +
   die 22 nachgelieferten Turm-Kacheln, LVL_TILE_DATA_COUNT 702 -> 737) hebt das
   Engpass-Fenster auf 183; darunter meldet tools/plan_terrain_sections.mjs
   "unmoeglich" (181/182 geprueft). Die sechs zusaetzlichen Adressen kommen aus
   dem Sprite-Pool: dessen 254er-Anschluss beginnt jetzt bei 342 statt 336.
   DAS GEHT NUR OHNE ENDBOSS AUF: mit Boss braucht der Pool 324 Kacheln, ohne
   ihn 314 - und 314 + 183 = 497 ist exakt der Platz, der neben HUD-Bar (14) und
   Blank (1) in den 512 Kacheln bleibt. NULL Reserve. Der Boss wird per
   NO_BOSS=1 in tools/gen_spr_compaction.js ausgelassen; das ist ein
   Vergleichsstand, kein Endzustand - die Loesung ist das abschnittsweise
   Sprite-Laden (Gegner-Spawns brauchen im Spitzenabschnitt nur 55 der 124
   Kacheln, die heute permanent liegen). */
#define LVL1_ZONE_MAX 183u
static u16 g_lvl_vram[LVL1_ZONE_MAX];

static void lvl1_vram_init(void) {
    u16 i = 0u, t;
    for (t = 38u;  t <= 68u  && i < LVL1_ZONE_MAX; t++) g_lvl_vram[i++] = t;   /* 29.07.2026: Ex-Font (42-51) + die letzten 21 freien Kacheln */
    for (t = 69u;  t <= 127u && i < LVL1_ZONE_MAX; t++) g_lvl_vram[i++] = t;   /* Font-Rueckgewinn */
    for (t = 144u; t <= 147u && i < LVL1_ZONE_MAX; t++) g_lvl_vram[i++] = t;
    for (t = 253u; t <= 253u && i < LVL1_ZONE_MAX; t++) g_lvl_vram[i++] = t;
    for (t = 254u; t <= 341u && i < LVL1_ZONE_MAX; t++) g_lvl_vram[i++] = t;   /* 30.07.2026: bis 341 (Zone 183), Sprite-Pool ab 342 */
}



/* ---- Laufender Abschnittswechsel (21.07.2026) ----
   Beim Wechsel aendern sich 30-60 Kacheln = bis zu 1 KB VRAM. Auf einen Frame
   gelegt zerreisst das das Bild (siehe 7e: Schreibzugriffe waehrend des aktiven
   Bildaufbaus waren die Ursache der gruen/weissen Sprites). Deshalb:
     * nur am FRAME-ENDE schreiben, wie bar_draw_at/score_draw,
     * pro Frame hoechstens TERR_UP_PER_FRAME Slots,
     * und erst NACH dem letzten Slot die Tabellenzeiger umschalten.
   Waehrend des Uebergangs bleibt der alte Abschnitt aktiv. Das ist sicher, weil
   in seinem Vorlauffenster (40 Zeilen zurueck / 34 voraus) alle Kacheln, die
   gerade gestreamt oder gezeichnet werden, in BEIDEN Abschnitten liegen - und
   dank der fortschreibenden Slot-Vergabe am selben Offset. */
#define TERR_UP_PER_FRAME 6u
#define TERR_SEC_LEAD 8u

static void terr_sec_begin(u8 target) {
    if (target == g_terr_sec || target >= (u8)LVL1_TERR_SEC_COUNT) return;
    g_terr_target = target;
    g_terr_up_idx = 0u;
}

/* Am Frame-Ende aufrufen. Liefert 1, solange ein Wechsel laeuft. */
static u8 terr_sec_step(void) {
    volatile u16 *dst;
    const u16 *src;
    u16 n = 0u, w, j, raw;
    if (g_terr_target == g_terr_sec) return 0u;
    while (g_terr_up_idx < (u16)LVL1_TERR_ZONE && n < (u16)TERR_UP_PER_FRAME) {
        j   = g_terr_up_idx++;
        raw = lvl1_terr_used[g_terr_target][j];
        /* Slot unveraendert? Dann nichts hochladen - das ist der ganze Gewinn der
           fortschreibenden Vergabe (typisch bleiben 80-100 von 140 stehen). */
        if (raw == lvl1_terr_used[g_terr_sec][j]) continue;
        dst = (volatile u16*)0xA000u + (u16)g_lvl_vram[j] * 8u;
        src = &lvl_tile_data[raw][0];
        for (w = 0u; w < 8u; w++) dst[w] = src[w];
        n++;
    }
    if (g_terr_up_idx >= (u16)LVL1_TERR_ZONE) {
        lvl1_select_section(g_terr_target);   /* jetzt erst umschalten */
        return 0u;
    }
    return 1u;
}

/* Beim Zeilenwechsel aufrufen: rechtzeitig vor der Grenze den Wechsel anstossen.
   Bei 6 Slots/Frame und ~60 geaenderten Kacheln dauert der Upload rund 10 Frames,
   eine Kachelzeile dagegen ~24 Frames - TERR_SEC_LEAD 8 ist also reichlich. */
static void terr_sec_check(u16 row) {
    u8 nxt;
    if (g_terr_target != g_terr_sec) return;          /* laeuft schon */
    nxt = (u8)(g_terr_sec + 1u);
    if (nxt >= (u8)LVL1_TERR_SEC_COUNT) return;       /* letzter Abschnitt */
    if (row + (u16)TERR_SEC_LEAD >= lvl1_terr_sec_row[nxt]) terr_sec_begin(nxt);
}
/* Paletten: SCR2 HW-Slots 0,5-15 fuer Terrain (12), Slots 1-4 fuer Bar; SCR1 Pals 0-4 Terrain, 15 Sterne */
#define STAR_PAL        15u

// --- Sprite-Slot-Zuteilung (OAM 0-63) --
#define SPR_SHIP       0    /* 0-13: Schiff-Metasprite, 7 Zellen x 2 (a + optionales b-Overlay) ??? FEST */
/* Schuesse sind echtes Multiplexing (Nutzerwunsch 07.07.2026, spaete Nacht):
   mehrere LOGISCHE Instanzen teilen sich WENIGER physische OAM-Slots,
   abwechselnd nach g_flicker-Phase gezeichnet (siehe draw_sprites). Phase 0
   zeichnet die "geraden" logischen Instanzen in die physischen Slots, Phase
   1 die "ungeraden" ??? spart 2 Slots (4???2). Nutzerwunsch 14.07.2026: "nur
   Schiff und Schuesse fest, der Rest dynamisch" ??? Schiff und Spielerschuesse
   bleiben deshalb bei ihren bisherigen festen Slots, ALLES andere (Gegner,
   Metasprite-Gegner, Ziffern, Wurm, Blumen-Schuesse, Schubduesen,
   Waffen-System, Pickup) kommt jetzt aus einem gemeinsamen dynamischen Pool,
   siehe oam_pool_*() unten. */
#define SPR_BULLET_0   14   /* 14-15: 2 physische Slots fuer MAX_BULLETS=4 logische Schuesse
                               (Slot i%2, Phase i/2 ??? siehe draw_sprites) */
#define BULLET_PHYS_SLOTS 2u

/* Nutzerkorrektur 14.07.2026 ("sprite slots immer dynamisch fuellen, sobald
   ein Gegner abgeschossen ist und der Slot frei wird den naechsten
   reinladen"): ersetzt saemtliche bisherigen festen/geliehenen Slot-Bereiche
   (Gegner, Metasprite-Gegner, Ziffern, Wurm, Blumen-Schuesse, Schubduesen,
   Waffen-System, Pickup) durch EINEN gemeinsamen Pool ueber Slots 16-63 (48
   Stueck). Freelist-Stack statt Bitmaske ??? TLCS-900H kann 48 Bits nicht
   sauber/guenstig in einer Bitmaske testen (32-Bit-Operationen sind auf
   dieser CPU teuer/ungelinkt, siehe (s32)-Cast-Warnung weiter unten in
   dieser Datei), ein Stack aus u8-Werten ist sowohl guenstiger als auch
   einfacher.
   PRINZIP: jede Instanz merkt sich ihren eigenen zugeteilten Slot (bzw. ihre
   2 Slots bei a+b-Overlay/2-Zellen-Metasprite) als eigenes Struct-Feld,
   NICHT mehr ueber einen fixen Index wie frueher ??? der "letztes Tile"-
   Fastpath-Cache wandert deshalb vom physischen Slot-Index in die Instanz
   selbst. Zuteilung passiert beim ersten Zeichnen nach Spawn (oder erneut
   jeden Frame, solange eine aktive Instanz noch KEINEN Slot hat ??? genau das
   erfuellt den Nutzerwunsch "beim Freiwerden sofort den naechsten
   reinladen", ohne dass irgendein System aktiv Vorrang erzwingen muesste:
   wer zuerst zeichnet, greift zuerst zu). Freigabe passiert, sobald eine
   Instanz inaktiv wird ODER nicht mehr sichtbar ist (y>=CLIP_Y) ??? der Slot
   landet sofort zurueck im Pool und kann im SELBEN Frame noch an eine
   andere wartende Instanz gehen, da alle Zeichenbloecke innerhalb desselben
   draw_sprites()-Aufrufs nacheinander laufen.
   REIHENFOLGE der Zeichenbloecke in draw_sprites() legt implizit die
   Prioritaet bei Slot-Knappheit fest (Nutzerwunsch "Ziffern sollen zuerst
   ausgeblendet werden"): Gegner -> Metasprite-Gegner -> Wurm ->
   Blumen-Schuesse -> Schubduesen -> Waffen-System -> Pickup -> Ziffern
   (score_draw(), ganz zuletzt aufgerufen). Bereits zugeteilte Slots werden
   NIE verdraengt (kein Force-Eviction) ??? nur neu ankommende Anfragen sehen
   die Reihenfolge, ein bereits gezeichnetes Objekt bleibt bis zu seinem
   eigenen Despawn stabil sichtbar. */
#define OAM_POOL_BASE 16u
#define OAM_POOL_SIZE 48u
#define OAM_NONE 0xFFu
static u8 g_oam_pool[OAM_POOL_SIZE];
static u8 g_oam_pool_n;   /* Anzahl frei im Stack */
static u8 g_oam_is_free[64];   /* 25.07.2026: 1 = Slot steht in der Freelist (O(1)-Doppel-Free-Schutz, siehe oam_pool_free) */

/* ===== 25.07.2026 FRUEH-AUSSTIEG im Zeichenpfad =====
   Messung (Arena, VBlanks je 30 Frames): draw_sprites() kostet 23,5, davon nur
   9 die Metasprite-Gegner. Der Rest verteilt sich zu je 1-2 VBlanks auf ein
   Dutzend Systeme, die NICHTS zu tun haben - jedes inaktive Objekt kostet
   trotzdem seinen Schleifendurchlauf und meist einen Funktionsaufruf
   (wp_pet_hide & Co.). Einzeln an der Rauschgrenze, zusammen ein Viertel der
   Framezeit; genau deshalb hat die Halbierungssuche keinen "Schuldigen"
   gefunden.

   Je System ein busy-Flag:
     - das jeweilige *_update() setzt es, solange irgendeine Instanz aktiv ist
       (es laeuft ohnehin jeden Frame ueber dasselbe Array, kostet dort nichts),
     - der Zeichenpfad ueberspringt sein System bei busy==0 und setzt das Flag
       am ENDE seines Durchlaufs selbst zurueck.

   ENTSCHEIDEND gegen Geister-Sprites (die Fehlerklasse vom 19.07.2026): das
   Flag wird erst in DEM Durchlauf geloescht, der die OAM-Slots freigibt - nie
   vorher. Der Ablauf ist also: letzte Instanz wird inaktiv -> Flag steht noch
   -> Zeichenpfad laeuft ein letztes Mal, gibt alle Slots frei -> Flag faellt.
   Wird das Flag zu frueh geloescht, bleiben Sprites stehen; wird es zu spaet
   geloescht, kostet es nur einen Frame Rechenzeit. Deshalb im Zweifel spaeter. */
static u8 g_busy_bullets, g_busy_mobullets, g_busy_ebullets, g_busy_enemies;
static u8 g_busy_metas, g_busy_worms, g_busy_wworms, g_busy_pickups;
static u8 g_busy_wpbul, g_busy_wpent;
static u8 g_busy_wcrawl;   /* 28.07.2026: Wandkriecher, siehe wcrawls_update/draw */

static void oam_pool_init(void) {
    u8 i;
    for (i = 0u; i < 64u; i++) g_oam_is_free[i] = 0u;
    for (i = 0u; i < OAM_POOL_SIZE; i++) {
        g_oam_pool[i] = (u8)(OAM_POOL_BASE + i);
        g_oam_is_free[(u8)(OAM_POOL_BASE + i)] = 1u;
    }
    g_oam_pool_n = OAM_POOL_SIZE;
}
/* 28.07.2026: wie viele Slots noch frei sind - fuer die Alles-oder-nichts-Vergabe
   der Wandkriecher (siehe wcrawls_draw). */
static u8 oam_pool_free_count(void) { return g_oam_pool_n; }
/* 29.07.2026 GEPRUEFT UND VERWORFEN: "niedrigsten freien Slot zuerst vergeben"
   statt LIFO, um dem Chaining zusammenhaengende Bloecke zu erhalten. Im
   Emulator gemessen: Chain-Bits bleiben bei 9 von 58 - KEIN Effekt. Grund ist
   simple Arithmetik, nicht Fragmentierung: bei 58 sichtbaren Sprites sind 42
   der 48 Pool-Slots belegt, nur 6 frei - ein 8-Slot-Block fuer einen
   4-Zellen-Metasprite ist dann unmoeglich, egal in welcher Reihenfolge
   vergeben wird. Der Hebel muesste den Pool ENTLASTEN, nicht ordnen. */
/* 29.07.2026, ZWEI Umbauten hier GEPRUEFT UND VERWORFEN (beide exakt �0):
     * "niedrigsten freien Slot zuerst" - kollidiert direkt mit der Blocksuche;
     * ZWEI-ENDEN-Vergabe (Einzelslots von oben, Bloecke von unten).
   Messlage dahinter: in der Endboss-Szene tragen kurz nach dem Spawn 44 von 58
   Sprites ein Chain-Bit, nach ~150 Frames nur noch 9 - die Metasprites
   verlieren ihre zusammenhaengenden Bloecke und zeichnen dann 8 Einzelslots
   statt einer Kette. Die Vergabereihenfolge ist dafuer aber NICHT die Ursache
   (beide Umbauten liessen die 9 unveraendert); bei 42 von 48 belegten
   Pool-Slots ist schlicht kein 8er-Block mehr unterzubringen. Ein wirksamer
   Hebel muesste die ZAHL gleichzeitiger Sprites senken, nicht ihre Verteilung. */
static u8 oam_pool_alloc(void) {
    u8 s;
    if (g_oam_pool_n == 0u) return OAM_NONE;
    g_oam_pool_n--;
    s = g_oam_pool[g_oam_pool_n];
    g_oam_is_free[s] = 0u;
    return s;
}
/* 25.07.2026 BLOCK-ALLOKATOR fuer Sprite-Chaining (Nutzerwunsch "chaining fuer
   alle Metasprites"): die K2GE-Kette bezieht sich immer auf den VORHERGEHENDEN
   OAM-Eintrag, ein verketteter Metasprite braucht deshalb n AUFEINANDERFOLGENDE
   Slots. Laeuft nur beim ersten Zeichnen einer Instanz (Spawn), nicht pro Frame -
   die O(48)-Suche ist dort unkritisch. Liefert den Basis-Slot oder OAM_NONE;
   bei OAM_NONE faellt der Zeichenpfad auf die alte Einzelslot-Vergabe zurueck,
   die Grafik bleibt also in jedem Fall korrekt, nur ohne Chaining-Ersparnis. */
static u8 oam_pool_alloc_run(u8 n) {
    u8 base, k, i, j;
    if (n == 0u || n > (u8)OAM_POOL_SIZE) return OAM_NONE;
    for (base = (u8)OAM_POOL_BASE; (u8)(base + n) <= (u8)(OAM_POOL_BASE + OAM_POOL_SIZE); base++) {
        for (k = 0u; k < n; k++)
            if (!g_oam_is_free[(u8)(base + k)]) break;
        if (k < n) continue;
        for (k = 0u; k < n; k++) {
            u8 s = (u8)(base + k);
            g_oam_is_free[s] = 0u;
            for (i = 0u; i < g_oam_pool_n; i++) {
                if (g_oam_pool[i] == s) {
                    for (j = (u8)(i + 1u); j < g_oam_pool_n; j++) g_oam_pool[j - 1u] = g_oam_pool[j];
                    g_oam_pool_n--;
                    break;
                }
            }
        }
        return base;
    }
    return OAM_NONE;
}
/* 25.07.2026: EINEN bestimmten Slot nehmen, wenn er frei ist (1 = bekommen).
   Fuer das a/b-Paar normaler Gegner: nur wenn oam0+1 frei ist, laesst sich das
   b-Overlay verketten (Kette bezieht sich auf den DIREKT vorhergehenden Slot).
   Sonst bleibt es beim alten, unverketteten Slot - immer korrekt, nur ohne
   Ersparnis. */
static u8 oam_pool_take(u8 slot) {
    u8 i, j;
    if (slot < (u8)OAM_POOL_BASE || slot >= (u8)(OAM_POOL_BASE + OAM_POOL_SIZE)) return 0u;
    if (!g_oam_is_free[slot]) return 0u;
    g_oam_is_free[slot] = 0u;
    for (i = 0u; i < g_oam_pool_n; i++) {
        if (g_oam_pool[i] == slot) {
            for (j = (u8)(i + 1u); j < g_oam_pool_n; j++) g_oam_pool[j - 1u] = g_oam_pool[j];
            g_oam_pool_n--;
            break;
        }
    }
    return 1u;
}
static void oam_pool_free(u8 slot) {
    u8 i;
    if (slot == OAM_NONE) return;
    /* SCHUTZ 21.07.2026 (Nutzerbericht "Schiffsspitze weg, dort wechselnde Sprites"):
       nur Slots aus dem Pool duerfen zurueck in die Freelist. Statische Strukturen
       starten mit 0 (nicht OAM_NONE=0xFF) ??? eine nie gespawnte Instanz sah damit
       Slot 0 als "belegt" an und gab ihn frei. OAM 0 ist aber die SCHIFFSSPITZE
       (SPR_SHIP 0, feste Slots 0-15): der Pool vergab sie danach an Schuesse und
       Gegner, die dort ihre Grafik zeichneten. Betrifft jede Struktur mit
       OAM-Feldern, deshalb hier zentral abgefangen statt an jeder Fundstelle. */
    if (slot < (u8)OAM_POOL_BASE || slot >= (u8)(OAM_POOL_BASE + OAM_POOL_SIZE)) return;
    /* Schutz gegen Doppel-Free (2026-07-19, Nutzerbericht "eingefrorene Geister-
       Sprites an wechselnden Stellen"): ein Slot, der schon in der Freelist steht,
       darf NICHT erneut rein ??? sonst geben zwei alloc()s denselben Slot aus (zwei
       Objekte teilen ihn -> Geister) und bei n==SIZE schreibt g_oam_pool[n++] sogar
       ausserhalb des Arrays (Speicherkorruption). O(48), aber korrekt. */
    /* 25.07.2026: der Doppel-Free-Schutz war eine LINEARE 48er-Suche bei JEDEM
       free() - in dichten Metasprite-Szenen (Pool erschoepft, staendiges
       Alloc/Free) summiert sich das erheblich. Jetzt O(1) ueber ein Frei-Flag
       je Slot; dieselbe Sicherheit, ohne Schleife. */
    if (g_oam_is_free[slot]) return;
    g_oam_is_free[slot] = 1u;
    if (g_oam_pool_n < (u8)OAM_POOL_SIZE) g_oam_pool[g_oam_pool_n++] = slot;
    (void)i;
}
/* 25.07.2026 OAM-SCRUB (Nutzer: "vereinzelt bleiben Gegner nach dem Abschuss
   stehen, sporadisch"): auf echter HW kann der UnsetSprite-Schreibzugriff eines
   sterbenden Gegners im Display-Race verloren gehen (Fehlerklasse wie §7e) -
   der Slot ist logisch frei, das Sprite bleibt aber sichtbar stehen, bis der
   Pool den Slot zufaellig wiederverwendet. Im Emulator NICHT reproduzierbar
   (3 komplette Ghost-Hunt-Levelflüge = 0 Treffer, tools/ghosthunt.py; der
   Emulator verliert keine Schreibzugriffe). Heilung: jeden Frame EINEN freien
   Pool-Slot vorsorglich erneut ausschalten (Rundlauf ueber die Freelist) -> ein
   verlorenes "Aus" heilt binnen ~1,5 s. Kostet 1 OAM-Byte-RMW pro Frame. */
static u8 g_oam_scrub_i;
static void oam_scrub_step(void) {
    if (g_oam_pool_n == 0u) return;
    if (g_oam_scrub_i >= g_oam_pool_n) g_oam_scrub_i = 0u;
    UnsetSprite(g_oam_pool[g_oam_scrub_i]);
    g_oam_scrub_i++;
}
#define MAX_BULLETS  4
#define MAX_ENEMIES  10   /* eine Kette braucht bis zu 8 gleichzeitig (lvl_spawn_count) */
#define MAX_MAPOBJ_BULLETS 16   /* logisch ??? Nutzerkorrektur 14.07.2026: jede aktive Instanz
                                    zieht sich jetzt selbst einen Slot aus dem OAM-Pool, kein
                                    Flicker-Multiplexing mehr noetig */
/* 22.07.2026 von 2 auf 6: solange alle Waffen dieselbe Bahn flogen, reichten zwei
   Projektile. Mit eigenen Bahnen je Waffe teilen sich sonst mehrere montierte
   Waffen zwei Plaetze und verschlucken sich gegenseitig die Schuesse. 6 ist
   gegenueber dem OAM-Pool (48) unkritisch, zumal sie flackernd nur die halbe Zeit
   einen Slot halten. Original: Drone allein haelt 80. */
#define MAX_WPBULLETS 6
/* Schritt 18 (Gegner-Feuerverhalten). Zurueckportiert 17.07.2026 aus
   Archiv/xenon_split_wip_20260716_2221.c ??? beim 16.07-Reset verlorengegangen,
   siehe Warnblock in CLAUDE.md Abschnitt 1. lvl_spawn_fire_rate[] ist fuer 35
   der 140 Spawns gesetzt, also ein echtes Feature, kein Stub. Eigener kleiner
   Pool, getrennt von Spieler-/Waffen-/Map-Objekt-Schuessen (das Original hat
   pro Level nur wenige Tuerme mit Feuerrate aktiv). */
/* 28.07.2026: 4 -> 8. Ein platzender Wandkriecher feuert LVL_WCRAWL_SHOTS (3)
   Kugeln AUF EINMAL; bei 4 Plaetzen haette eine einzige Salve den Pool fuer alle
   uebrigen Gegner leergeraeumt. OAM kostet das nichts Zusaetzliches: die
   Gegner-Schuesse teilen sich ihre Slots ueber FLICKER_VIS (ebullets_draw), es
   sind also nie alle gleichzeitig sichtbar. */
#define MAX_ENEMY_BULLETS 8u
#define ENEMY_SHOT_SPEED_FIX 32   /* 30-fps-Cap: 16->32 (2px/Frame, gleiche px/s wie 60fps) */
#define MAX_PICKUPS  12   /* 21.07.2026: 4->12 fuer den Cash-Regen des Endbosses.
                            Das Original wirft 18 Token (9x50 + 9x100 Credits, Spawner
                            1000:15e4); 18 x 2 OAM-Slots waeren 36 von 48 - machbar nur,
                            weil die Todesroutine vorher den Gegner-Ring leert, aber zu
                            knapp. 12 Token = 6x50 + 6x100 = 900 Credits, bewusste
                            Abweichung. Vorher: 18.07.2026: 1->4, damit dicht aufeinanderfolgende
                             Container/Gruppen (Levelanfang row 0/2/9/18) ihre
                             Belohnungen gleichzeitig zeigen koennen (draw_sprites
                             zeichnet jetzt alle Slots, siehe dort). Jeder aktive
                             Pickup zieht max. 2 OAM aus dem Pool. */
#define MAX_METAENEMIES 8   /* Map-Update 14.07.2026, lvl_spawn_count[4] jetzt 8 statt 4 */
#define MAX_WORMS 1u          /* lvl_spawn_count[5]=1 */
#define MAX_WORM_SEGS 6u      /* >= max(lvl_spawn_worm_segs); Bounds-Check beim Spawn (Map-Update 13.07.2026: 8->6) */
/* BUGFIX 21.07.2026 ("Alien-Kaefer zerrupft, da fehlen Teile"): war 2, weil die
   damaligen Metasprites (Index 5,7-10) alle genau 2 Zellen hatten. Die neue map.h
   bringt Index 11/12/13 mit je 4 Zellen (2x2-Bloecke, gespawnt u.a. in den Zeilen
   60, 106, 238 und 259-274). Beide Schleifen (metaenemy_spawn OAM-Vergabe und die
   Zeichenschleife in draw_sprites) laufen bis META_CELLS ??? mit 2 wurden nur die
   OBEREN beiden Zellen gezeichnet, die untere Reihe fehlte komplett. Kein
   Speicherueberlauf (die Schleifen sind durch META_CELLS begrenzt, nicht durch
   lvl_meta_count), aber eben ein halber Gegner.
   Wert = max(lvl_meta_count) OHNE die Schiffs-Metasprites 0-4 (die haben 7 Zellen,
   werden aber ueber SHIP_CELLS/ship_draw gezeichnet, nicht als TMetaEnemy).
   Kosten: 8 Gegner x (4 statt 2 OAM-Slots + last_snum) = ~48 Byte RAM.
   MERKE: bei jedem Map-Update lvl_meta_count[] pruefen! */
#define META_CELLS 4
/* Korrektur 10.07.2026 (Abend): die vorige Analyse (90->10) hat nur
   Slot-Verbrauch gemessen, nicht ob der Gegner ueberhaupt je sichtbar war ???
   das war der eigentliche Bug hinter "viele Gegner erscheinen erst gar
   nicht". Simulation gegen die echten Pfaddaten (Node-Skript im Scratchpad)
   zeigt: die Pfade verlassen den Screen absichtlich fuer bis zu 145 Frames
   am Stueck (Kurven/Loops) und kommen wieder rein ??? mit GRACE=10 stirbt der
   Gegner laengst, bevor er zurueckkommt (bei Spawn 3 sogar BEVOR er je
   sichtbar war). GRACE=60..70 liefert in der Simulation 0 verlorene und
   ALLE Gegner mindestens einmal sichtbar (bei GRACE=10 nur 9 von 15).
   Korrektur 11.07.2026 (Nacht, Nutzerbericht "Metasprite ab Zeile 66 sehe
   ich nicht"): Re-Simulation gegen ALLE 5 aktuellen Spawns zeigt, Spawn 4
   (der animierte Metasprite-Gegner) verlaesst den Screen fuer 78 Frames am
   Stueck, BEVOR er sein langes, prominentes Sichtfenster (176 Frames ab
   Bildschirmmitte) erreicht ??? mit GRACE=70 stirbt er 9 Frames zu frueh,
   genau in dieser Luecke (nur ein kurzer, 2-3px-Rand-Blitz beim Spawn war je
   sichtbar). Alle anderen Spawns brauchen hoechstens GRACE=42. GRACE=80
   deckt Spawn 4 mit kleiner Marge ab, ohne die anderen zu beeinflussen. */
/* Neu simuliert 17.07.2026 gegen die 121 Pfade / 140 Spawns des aktuellen
   Exports (der alte 80er-Wert stammt aus der 4-Pfade-??ra und war laengst
   ueberholt ??? siehe CLAUDE.md Abschnitt 4). Die neuen Pfade verlassen den
   Bildschirm teils >80 Frames am Stueck und kommen zurueck; 80 toetete sie
   in der Luecke. Sweep MIT Pool-Druck, sichtbare Gegner-Frames ueber das ganze
   Level: 80 -> 31273, 120 -> 31666, 150 -> 31796, 200 -> 31796, 250 -> 31911,
   300 -> 31911. Monoton steigend, Plateau ab 250. Zwei unabhaengige
   Simulationen (mit und ohne Pool-Modell) sind sich einig, dass 250 gegenueber
   150 exakt die letzten 115 Frames rettet (der eine verbleibende Spawn 16).
   Kein messbarer Preis: gespawnte Kettenglieder (75) und Pool-Peak (10/10)
   sind bei JEDEM Wert identisch, nichts bleibt am Levelende haengen. */
#define ENEMY_OFFSCREEN_GRACE 250u  /* Frames Kulanz ausserhalb des Bildschirms, bevor der Slot freigegeben wird */
/* Map-Update 12.07.2026 (Wurm, Schritt 12): eigene, groessere Kulanz noetig ???
   der Wurm spawnt ABSICHTLICH bereits unsichtbar (spawn_y=149, hinter der
   HUD-Bar, "steckt im Wurmloch") und braucht laut Node-Simulation (Scratchpad,
   gegen die echten Pfad-4-Daten + Wurm-Delay) 129-147 Frames (je nach
   Segment-Verzoegerung), bevor er ueberhaupt zum ERSTEN Mal sichtbar wird ???
   das ist kein spaeteres Wiedereintreten wie bei normalen Gegnerpfaden
   (ENEMY_OFFSCREEN_GRACE=80 deckt dort nur bereits-gesehene Gegner ab, siehe
   dortigen Kommentar), sondern die anfaengliche Versteck-Phase selbst. Mit
   GRACE=80 starben alle 8 Segmente lange bevor sie je zu sehen waren. */
#define WORM_OFFSCREEN_GRACE 170u

// --- Spielfeld --
#define SCR_W  160
#define SCR_H  152
#define BAR_Y         144
#define CLIP_Y        136
#define PLAYER_Y_MAX  119   /* y+24=143 < BAR_Y, unterste erlaubte Y-Position */

// --- 2bpp-Tile-Daten --

/* kanonische Bar-Tiles: 10 Designs (alle Pixel-Shift-faehig auf SCR_1)
   0-6: cool-body  pal1  (idx1=color2, idx2=color3, idx3=color4)
   7-8: warm       pal2  (idx1=color5, idx2=color6, idx3=color7)
   9:   life       pal3 */
/* Bar-Tiles 0-9: Palette 1 (cool). Tiles 10-13: Palette 4 (pal4: idx1=Salmon 0x0ECE,
   idx2=cool-mid 0x0644, idx3=cool-bright 0x0A77) ??? Salmon-Pixel eingebacken. */
static const unsigned short gfxBar[14][8] = {
    { 0x0000, 0x5555, 0xFFFE, 0xFFFA, 0xFFE8, 0xFFA0, 0xFE80, 0xAA00 }, /* 0  left-diag (col 1) */
    { 0x0000, 0x0021, 0x009E, 0x027E, 0x09FE, 0x27FE, 0x9FFF, 0x6AAA }, /* 1  right-diag        */
    { 0x0000, 0x5555, 0xAAAA, 0x0000, 0x0000, 0x0000, 0x5555, 0xAAAA }, /* 2  cool middle       */
    { 0x0000, 0x5557, 0xFFFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x3FFE, 0xAAAA }, /* 3  cool r-trans      */
    { 0x0000, 0x1555, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0xEAAA }, /* 4  cool right-cap    */
    { 0x0000, 0x5554, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xAAAB }, /* 5  cool H-flip       */
    { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, /* 6  blank             */
    { 0x0000, 0x5555, 0x6AA0, 0x6AAA, 0x6AA0, 0x6AAA, 0x6AA0, 0xBFFF }, /* 7  warm left         */
    { 0x0000, 0x5556, 0x0AAB, 0x0AAB, 0x0AAB, 0x0AAB, 0x0AAB, 0xFFFF }, /* 8  warm right        */
    { 0x0000, 0x5555, 0xAAAA, 0xFFFF, 0xFFFF, 0xFFFF, 0x5555, 0xAAAA }, /* 9  life              */
    /* Kombinierte Tiles (Pal4): Salmon y1,x0 in right-cap (cols 0+18) */
    { 0x0000, 0x7FFF, 0xEAAA, 0xEAAA, 0xEAAA, 0xEAAA, 0xEAAA, 0xAAAA }, /* 10 right-cap+Salmon  */
    /* Salmon y1,x6 in right-diag (col 9) */
    { 0x0000, 0x0027, 0x00BA, 0x02EA, 0x0BAA, 0x2EAA, 0xBAAA, 0xEAAA }, /* 11 right-diag+Salmon */
    /* Salmon y6,x0 in r-trans (col 15) */
    { 0x0000, 0xFFFE, 0xAAAA, 0xEAAA, 0xEAAA, 0xEAAA, 0x6AAA, 0xAAAA }, /* 12 r-trans+Salmon    */
    /* Salmon y1,x7 in H-flip (col 19) */
    { 0x0000, 0xFFFD, 0xAAAB, 0xAAAB, 0xAAAB, 0xAAAB, 0xAAAB, 0xAAAA }  /* 13 H-flip+Salmon     */
};

/* SCR_1-Palette pro Tile-Design */
static const unsigned char barPal[14] = { 1,1,1,1,1,1,1,2,2,3, 4,4,4,4 };
/* 0:cap+S  1:left-diag  2-8:digit  9:diag+S  10-14:life  15:rtrans+S  16-19:warm+caps+S */
/* Spalte 19 war Design 13 ("H-Flip+Salmon") ??? das ist Design 10 zeilenweise
   gespiegelt (verifiziert), daher hier direkt Design 10 + Flip in put_cell()
   (siehe bar_draw_at/bar_shift_update: Spalte 19 bekommt immer flip=1). */
static const unsigned char barMapDef[20] = { 10,0,6,6,6,6,6,6,6,11,9,9,9,9,9,12,7,8,10,10 };
/* dynamische Spalten-Map (wird in game_start initialisiert, per Leben-Verlust aktualisiert) */
static unsigned char g_bar_col[20];

/* Ziffern, Schuss, Schiff und Gegner kommen jetzt aus dem einheitlichen
   Sprite-Streifen-Export (lvl_sspr_*, siehe spr_draw_s/spr_draw_s_single
   weiter unten) ??? die alten handgebauten gfxBullet/gfxEnemy/gfxStarA/
   gfxStarB/gfxDigit-Tabellen sind entfallen (Archiv\xenon.c bzw. Git-
   Historie f??r den alten Stand). */
/* Terrain-Grafik + Level-Map kommen komplett aus PNG/Map/Tiles_Lvl_1.h
   (NGPC-Tile-Analyzer-Export, LVL_MAP_W x LVL_MAP_H, siehe lvl1_put_row) */

// --- Spielzustand --
#define STATE_PLAY  0
#define STATE_OVER  1
#define STATE_SHOP  2   /* VRAM-Segment-Umschaltung, siehe shop_enter()/shop_resume() */

/* firerate_stage/power_stage/weapons_active/weapon_cooldown: Schiffszustand
   fuer Gruppen-Belohnungs-Pickups (Schritt 14/15) ??? siehe apply_pickup().
   power_stage waehlt den Schuss-Sprite direkt aus lvl_power_stage_bullet_spr
   (S149 Basis, S150/151 hoehere Stufen, siehe draw_sprites) ??? 1 OAM-Slot,
   kein Overlay/Flackern noetig. */
typedef struct {
    u8  x, y, fire_cd, lives, inv_cd;
    u8  energy;                               /* 22.07.2026: Energie 0..PLAYER_MAX_ENERGY */
    u8  firerate_stage, power_stage, speed_stage;   /* speed_stage: Speedup-Pickup 19.07.2026 */
    u16 weapons_active;                       /* Bitmaske ueber LVL_WEAPON_COUNT */
    u8  weapon_cooldown[LVL_WEAPON_COUNT];
} TPlayer;
typedef struct { u8 x, y, active; u8 oam; u16 last_snum; } TBullet;   /* oam/last_snum: dynamischer Pool-Slot + Flacker 19.07.2026 */
/* Gruppen-Belohnungs-Pickup (Schritt 14+15): entsteht an der Todesposition
   des letzten Gegners einer Gruppe und wird bei Schiffskontakt via
   apply_pickup() eingeloest. Bewegung SCREEN-ABSOLUT in 2 Phasen (Schritt 15,
   Nutzerkorrektur 10.07.2026 ??? vorher driftete das Pickup nur passiv mit dem
   Scroll statt dem in map.h definierten Pfad zu folgen):
   0=Anflug (geradlinig von der Todesposition zu LVL_PICKUP_ANCHOR_X/Y,
     Tempo LVL_PICKUP_HOMING_SPEED), 1=auf lvl_pickup_path_data ab dem
   Anker, 2=fertig (despawnt). */
typedef struct {
    u8  active;
    u8  x, y;              /* abgeleitete Bildschirmposition (Zeichnen/Kollision) */
    s16 x_fix, y_fix;       /* 12.4-Fixed, absolute Bildschirmposition */
    s16 homing_dx, homing_dy;  /* Pro-Frame-Schritt waehrend des Anflugs, einmalig berechnet */
    u16 homing_frames, homing_tick;
    u16 path_frame;
    u8  dir;
    u8  state;
    u8  kind;
    u16 value;
    u16 spr;
    u8  manim;        /* 27.07.2026: Metaanim-Index der Waffe (0xFF = keiner). Waffen-
                         Container zeigen die erste Zelle jedes Metaanim-Bildes -
                         S250..253 bei der Kanone. Ohne das stand das Icon still,
                         weil S250 selbst kein Anim-Kopf ist. */
    u8  oam0, oam1;   /* Nutzerkorrektur 14.07.2026: dynamisch aus oam_pool_*() statt SPR_PICKUP */
} TPickup;

/* Map-Objekt-Schuss (??13 "bullet_on_loop", Schritt 13 der Anweisung): screen-
   absolute Bewegung wie bei Gegner-Pfaden, x_fix/y_fix 12.4-Fixed, dx/dy aus
   lvl_mapobj_bullet_dx/dy (s16, ebenfalls 4.4-Fixed wie Pfad-Deltas). */
typedef struct {
    u8 active; s16 x_fix, y_fix; s16 dx, dy; u16 spr;
    u8 oam; u16 last_snum;   /* Nutzerkorrektur 14.07.2026: dynamisch aus oam_pool_*() */
    u8 prio, flip;           /* 30.07.2026, siehe MAPOBJ_SHOT_* - beim Zeichnen gesetzt */
} TMapobjBullet;

/* ===== Map-Objekt-Schuss: Darstellung (30.07.2026, drei Nutzerbefunde) =====
   1. "das Projektil der Anemone, welche gespiegelt wird, ist nicht gespiegelt"
      Alle sechs Anemonen teilen sich EINE Grafik (lvl_mapobj_bullet_spr = 0x8106
      = S262), und die ist asymmetrisch. Die rechte Wand ist die gespiegelte
      Seite (so legt es auch das Tool ab, siehe mobjWiltedFor) und schiesst nach
      LINKS - erkennbar am Vorzeichen von lvl_mapobj_bullet_dx. Also: dx < 0
      => H-Flip. Kein hart verdrahteter Objektindex noetig.
   2. "das Projektil muss beim Abschuss HINTER die Anemone"
      Die Anemone ist ein Map-KACHEL-Objekt, liegt also auf SCR1/SCR2. Ein Sprite
      kommt nur ueber die Prioritaet dahinter: PR.C = 01 wird laut Hardware-
      Renderer (Ngpcraft core/renderer.py, Pipeline-Schritt 2) VOR beiden
      Scroll-Ebenen gezeichnet, also hinter ihnen sichtbar. library.h kennt nur
      SPR_FRONT (3<<3), daher hier eigens definiert.
      Im Korridor liegt Kachel 0 (durchsichtig), das Projektil bleibt auf dem
      ganzen Flug sichtbar - verdeckt wird es NUR von der Anemone selbst und von
      Fels, und genau das ist der gewuenschte "kommt da raus"-Eindruck.
   3. "das Projektil muss min 6 Pixel hoeher"
      Nachgemessen an den Kacheldaten: S262 belegt in seiner 8x8-Zelle nur die
      Zeilen 5..7 (Spalten 0..4), haengt also 5 px unter dem Sprite-Ursprung -
      deshalb sass es zu tief. MAPOBJ_SHOT_Y_UP hebt den Abschusspunkt.
   Punkt 2 und 3 gelten NUR fuer die Anemonen: der Turmschuss S99 sitzt mittig in
   seiner Zelle (Zeilen 2..5) und braucht keine Hebung, und die Tuerme sollen
   nicht plötzlich hinter dem Fels verschwinden. Unterschieden wird ueber
   lvl_mapobj_gated[] - in diesem Export sind genau die sechs Anemonen getaktet
   (Rate 6, Schuss bei Bild 12), alles andere feuert ungetaktet. */
#define SPR_BEHIND      (u8)(1<<3)   /* PR.C=01: hinter beiden Scroll-Ebenen */
/* 6 -> 9 -> 8. Zweite Runde: "das Projektil sitzt 3 Pixel zu weit unten" (9),
   dritte Runde: "1 px runter" (8). Passt zur Kachelmessung: S262 beginnt erst in
   Zeile 5 seiner Zelle, haengt also 5 px unter dem Sprite-Ursprung. */
#define MAPOBJ_SHOT_Y_UP 8u

/* x-Ausgleich der SPIEGELUNG, dritte Runde (Nutzer: "3 px nach links").
   Der Wert 3 ist keine Geschmacksfrage, er steckt in der Grafik: S262 belegt in
   seiner 8x8-Zelle nur die Spalten 0..4. H-gespiegelt wird daraus 3..7, das
   sichtbare Projektil sitzt auf der gespiegelten Seite also genau 3 px weiter
   RECHTS als auf der anderen. Deshalb wird NUR die gespiegelte Seite (die
   rechte Wand, dx < 0) um 3 px nach links geschoben - damit verlaesst das
   Projektil auf beiden Waenden die Muendung an derselben Stelle. Die linke Wand
   bleibt unangetastet; soll sie mit, muss der Ausgleich unbedingt VOR die
   gespiegelt-Abfrage. */
#define MAPOBJ_SHOT_X_FLIPFIX 3u

/* Gegner-Schuss (Schritt 18, zurueckportiert 17.07.2026). vx/vy sind 4.4-Fixed
   und werden EINMAL beim Abschuss bestimmt ??? kein Nachfuehren, kein Vorhalten
   (so auch im Original, siehe map.h-Kommentar bei lvl_spawn_fire_rate). Alle
   Gegner-Schuesse teilen dasselbe Sprite (LVL_ENEMY_BULLET_SPR), deshalb hier
   kein last_snum: das Tile aendert sich nie, der Positions-only-Fastpath greift
   ueber die blosse Existenz des Slots. */
/* 21.07.2026: spr ergaenzt. Bis dahin teilten sich ALLE Gegnerschuesse
   LVL_ENEMY_BULLET_SPR; der Endboss feuert aber ein eigenes Projektil (S195).
   0 = das gemeinsame Sprite, sonst eine S-Nummer. last_snum bleibt unnoetig:
   ein Slot behaelt sein Sprite fuer die Lebensdauer des Schusses. */
typedef struct { u8 active; s16 x_fix, y_fix, vx, vy; u8 oam; u16 spr; } TEnemyBullet;

/* Gegner: Bewegung ueber vorkompilierte Bewegungspfade (lvl_path_*,
   screen-absolut, unabhaengig vom Scrolling), Grafik ueber eine eigene,
   pro Instanz laufende Sprite-Animation (lvl_sspr_anim_*) ??? siehe
   ANWEISUNG_Export_Integration.md Schritt 7+8. x/y sind die aus x_fix/y_fix
   abgeleitete Bildschirmposition (fuer Kollision/Zeichnen, wie vorher). */
typedef struct {
    u8  active;
    u8  x, y;
    s16 x_fix, y_fix;
    /* s16, NICHT u8 (Bugfix 17.07.2026): lvl_spawn_x/y sind im Export s16 und
       enthalten seit dem 140-Spawn-Update echte Werte >=128 (z. B. Spawn 4:
       x=172, y=129 ??? Anflug von rechts bzw. von unten). Als u8 gespeichert und
       per (s8) zurueckgelesen wurde daraus -84/-127, also "weit ausserhalb" ???
       der Gegner war voll sichtbar, aber off_timer lief ab Frame 0 und
       ENEMY_OFFSCREEN_GRACE toetete ihn nach exakt 80 Frames ("Gegner
       verschwinden"). Gleiche Fehlerklasse wie der TWorm-Bugfix (spawn_y=149),
       dort ist s16 bereits die Loesung ??? siehe TWorm-Kommentar. */
    s16 spawn_x, spawn_y;
    u16 path;
    u16 frame;
    const s8 *pp;    /* naechster Pfadschritt, siehe PATH_PTR */
    u16 path_len;    /* 22.07.2026: beim Spawn gemerkt statt jeden Frame aus dem ROM */
    u8  c_anim_len, c_anim_speed, c_fire_rate;
    u8  c_needs_b;   /* 22.07.2026: b-Overlay noetig? nur bei Sprite-Wechsel neu lesen */
    u8  chain_b;     /* 25.07.2026: 1 = oam1 ist oam0+1 und per Chain-Bit angehaengt
                        -> Bewegung schreibt nur oam0 (siehe spr_draw_chain_cell) */
    u8  is_static;   /* 1 = Standbild (spr_num direkt zeichnen), 0 = anim_idx/anim_frame */
    u16 spr_num;     /* nur bei is_static: feste 1-basierte S-Nummer */
    u8  anim_idx;
    u8  anim_tick, anim_frame;
    u8  off_timer;   /* Frames durchgehend ausserhalb des Bildschirms (Despawn-Kulanz) */
    u8  spawn_idx;   /* Index in lvl_spawn_* (Punkte/Gruppe beim Kill, siehe check_collisions) */
    u8  fire_acc;    /* Schritt 18: 8-Bit-Akkumulator fuers Gegner-Feuern, siehe enemy_fire_tick() */
    u8  path_acc;    /* Subpixel-Akku fuer den Pfad-Fortschritt, siehe g_tune_espeed */
    s16 hz_dx, hz_dy;   /* Performance-Fix 11.07.2026: Hitzone EINMAL beim Spawn
                           aufgeloest (hitzone_resolve) statt bei jedem Bullet-
                           Kollisionstest neu die 17 lvl_hitzone_*-Eintraege zu
                           durchsuchen ??? bei vielen gleichzeitigen Schuessen/
                           Gegnern (z. B. Feuern in dichten Wellen) macht die
                           wiederholte Suche einen Grossteil der Kollisionskosten
                           aus (siehe check_collisions/hit_test_cached). */
    u8  hz_w, hz_h;
    u8  oam0, oam1;   /* Nutzerkorrektur 14.07.2026: dynamisch aus oam_pool_*() statt SPR_ENEMY_0 */
    u16 last_snum;
    u8  health;       /* Trefferpunkte (Health/Schaden-System 19.07.2026), aus lvl_spawn_health beim Spawn */
    u8  flash;        /* 23.07.2026: Rest-Frames Treffer-Flash (0=aus), siehe FLASH_PAL */
} TEnemy;

/* Mehrteiliger animierter Gegner (Schritt 11 "mehrteilige, animierte Gegner
   auf Bewegungspfaden") ??? eigene, separate Struktur statt TEnemy zu
   ueberladen: Bewegung ist identisch zu TEnemy (screen-absoluter Pfad), aber
   die Grafik sind IMMER META_CELLS Zellen (lvl_meta_*) statt einem einzelnen
   a+b-Sprite. metaanim=0xFFFF -> Standbild (meta_idx fest), sonst Index in
   lvl_metaanim_* (meta_idx wird pro Frame aus lvl_metaanim_frames nachgeschlagen). */
typedef struct {
    u8  active;
    u8  x, y;
    s16 x_fix, y_fix;
    s16 spawn_x, spawn_y;   /* s16, siehe TEnemy.spawn_x-Kommentar (Bugfix 17.07.2026) */
    u16 path;
    u16 frame;
    const s8 *pp;    /* naechster Pfadschritt, siehe PATH_PTR */
    u16 path_len;    /* 22.07.2026, siehe TEnemy */
    u16 meta_idx;
    u16 metaanim;
    u8  anim_tick, anim_frame;
    u8  off_timer;
    u8  spawn_idx;
    u8  path_acc;       /* siehe TEnemy.path_acc / g_tune_espeed */
    s16 hz_dx, hz_dy;   /* siehe TEnemy.hz_dx-Kommentar (Performance-Fix 11.07.2026) */
    u8  hz_w, hz_h;
    u8  oam[META_CELLS];        /* Nutzerkorrektur 14.07.2026: dynamisch aus oam_pool_*() */
    /* BUGFIX 21.07.2026 ("Alien-Kaefer zeigt nicht alle Ebenen"): gezeichnet wurde
       mit spr_draw_s_single(), also NUR die a-Ebene. Die Kaefer-Metasprites (11-13)
       haben aber auf ALLEN vier Zellen ein b-Overlay -> die halbe Grafik fehlte.
       Zweiter Slot je Zelle, nur belegt wenn die Zelle wirklich eine b-Ebene hat. */
    u8  oam_b[META_CELLS];
    u16 last_snum[META_CELLS];
    u8  health;                 /* Trefferpunkte (Health/Schaden-System 19.07.2026), aus lvl_spawn_health */
    u8  flash;                  /* 23.07.2026: Rest-Frames Treffer-Flash (0=aus), siehe FLASH_PAL */
    /* 25.07.2026 ZELL-CACHE (HW-fps, Nutzer "24 fps wo viele Metasprites kommen"):
       der Zeichenpfad las pro Zelle und Frame SIEBEN ROM-Werte (meta_off, count,
       dx, dy, num, 2x sspr_b_idx) - bei 8 Gegnern x 4 Zellen ~230 ROM-Zugriffe je
       Frame, jeder mit HW-Wartezyklen (im Emulator unsichtbar). Diese Werte
       haengen NUR am meta_idx, nicht an der Position: einmal beim Bildwechsel
       aufloesen, danach nur noch RAM lesen. Gleiche Fehlerklasse/Fix wie die
       Spawn-Werte am 22.07. (damals der Haupthebel 15-20 -> 28-30 fps). */
    /* 25.07.2026 SPRITE-CHAINING: Basis-Slot eines zusammenhaengenden OAM-Blocks
       (cnt*2 Slots, a/b abwechselnd). OAM_NONE = kein Block bekommen -> alte
       Einzelslot-Vergabe. Bei Chaining schreibt nur der Anker seine Position,
       die uebrigen Zellen halten konstante Deltas. */
    u8  chain_base;
    u8  chain_cells;            /* Zellzahl, fuer die der Block vergeben wurde */
    u16 c_cached_idx;           /* meta_idx, fuer den der Cache gilt (0xFFFF = leer) */
    u8  c_cnt;                  /* lvl_meta_count */
    u8  c_dx[META_CELLS], c_dy[META_CELLS];
    u16 c_num[META_CELLS];
    u8  c_needb[META_CELLS];
    u8  c_flip[META_CELLS];     /* 28.07.2026: Zellen-Spiegelung, siehe META_FLIP_OF */
} TMetaEnemy;

/* Wurm-Kette (Rotationssatz-Sprite, Schritt 12 "Rotationssaetze & Wurm-Ketten") ???
   mehrere Segmente auf DEMSELBEN Pfad, jedes mit eigenem, gestaffelt startendem
   Frame-Zaehler (frame startet negativ = Segment "steckt noch im Wurmloch",
   bewegt sich noch nicht). Grafik kommt NICHT aus lvl_sspr_* / lvl_meta_* wie bei
   TEnemy/TMetaEnemy, sondern aus lvl_rotset_idx/pal[rot][dir8] ??? dir8 kommt pro
   Frame aus lvl_path_dir8[] an der aktuellen Pfad-Position (nicht aus der
   Spawn-spr). frame ist s16 (nicht u16 wie bei TEnemy/TMetaEnemy), weil er
   waehrend der Verzoegerung negativ ist. */
typedef struct {
    u8  alive;
    s16 frame;
    const s8 *pp;    /* naechster Pfadschritt, siehe PATH_PTR */
    s16 x_fix, y_fix;
    u8  x, y;
    u8  off_timer;   /* wie TEnemy.off_timer, pro Segment unabhaengig */
    u8  oam;          /* Nutzerkorrektur 14.07.2026: dynamisch aus oam_pool_*() statt g_worm_oam_slot */
    u16 last_snum;
    u8  last_flip;
} TWormSeg;

/* Map-gebundene Pfade (lvl_path_map_relative[path]==1, siehe Anweisung Abschnitt
   "MAP-GEBUNDENE PFADE"): die Bildschirm-Y-Position wird zusaetzlich um den seit
   dem Spawn-Trigger akkumulierten Scroll-Betrag korrigiert (scroll_at_spawn),
   damit ein Segment, das sich gerade nicht bewegt (frame<0 ODER eine (0,0)-Pause
   im Pfad), exakt an seiner Map-Stelle (dem Wurmloch) haengen bleibt statt
   bildschirmfest stehenzubleiben ??? nur Y, das Spiel scrollt ausschliesslich
   vertikal (g_scroll_y). */
typedef struct {
    u8  active;              /* 1 solange mindestens 1 Segment alive ist */
    /* s16, NICHT u8 wie bei TEnemy/TMetaEnemy: spawn_y=149 (dieser Wurm,
       "steckt hinter der HUD-Bar") ist eine echte grosse POSITIVE Zahl, die
       zufaellig >=128 ist ??? der bei TEnemy ueberall genutzte
       (s16)(s8)spawn_x/y-Trick geht davon aus, dass jeder u8-Wert >=128 ein
       gewrapptes kleines Negativ ist (z.B. -6 -> 250) und interpretiert 149
       faelschlich als -107, was sowohl Zeichen- als auch Grace-Logik
       zerstoert (Bugfix 12.07.2026, Nutzerbericht "Wurm geht nicht"). s16
       vermeidet die Mehrdeutigkeit komplett, kostet nur 2 Byte/Wurm. */
    s16 spawn_x, spawn_y;
    u16 path;
    u8  rot;                 /* Rotationssatz-Index, spr & 0x1FF */
    u16 scroll_at_spawn;
    u8  num_segs;
    u8  delay;
    u8  spawn_idx;
    s16 hz_dx, hz_dy;        /* einmal aufgeloest, gilt fuer ALLE Segmente/Richtungen
                                 (Anweisung Schritt 16: Rotationssatz-Hitzone ist
                                 richtungsunabhaengig) */
    u8  hz_w, hz_h;
    TWormSeg seg[MAX_WORM_SEGS];
} TWorm;

static TPlayer g_player;
static TBullet g_bullets[MAX_BULLETS];
static TMapobjBullet g_mo_bullets[MAX_MAPOBJ_BULLETS];
/* Blumen-Schuesse scrollen mit der Welt mit (Nutzer 19.07.: "fliegen nicht zur
   Scrolpos"). Merkt g_scr1_y vom Vorframe, um pro Frame den Scroll-Versatz auf
   alle aktiven Schuesse zu addieren (die Blume selbst sitzt bei ring_row*8-g_scr1_y,
   der Schuss muss dieselbe Bewegung bekommen, sonst haengt er relativ zur Map). */
static u8 g_mo_last_scr1y;
static TEnemyBullet  g_ebullets[MAX_ENEMY_BULLETS];   /* Schritt 18, siehe enemy_fire() */
static TEnemy  g_enemies[MAX_ENEMIES];
static TMetaEnemy g_metaenemies[MAX_METAENEMIES];
static TWorm   g_worms[MAX_WORMS];
/* Nutzerkorrektur 14.07.2026: die "letztes Tile"-Fastpath-Caches fuer
   Gegner/Metasprite-Gegner/Wurm/Blumen-Schuesse/Waffen-Schuesse wandern mit
   dem OAM-Pool-Umbau aus diesen parallelen Arrays HINEIN in die jeweiligen
   Struct-Felder (oam/last_snum, siehe TEnemy/TMetaEnemy/TWormSeg/
   TMapobjBullet/TWpBullet) ??? ein physischer Slot-Index bedeutet ab jetzt
   nicht mehr zuverlaessig "dieselbe Instanz wie letzten Frame", da Slots
   dynamisch wandern koennen. Nur Schiff/Spielerschuesse (feste Slots) UND
   die Singletons Schubduesen/Waffen-Modul (kein Array, aber jetzt mit
   eigenem dynamischem Slot statt SPR_THRUST_0/SPR_WPMODULE) behalten
   eigene Cache-Variablen. */
static u16     g_thrust_last_s;                    /* 0 = versteckt/nie gezeichnet */
static u8      g_thrust_last_back;                 /* Map-Update 13.07.2026: Flip haengt jetzt
   zusaetzlich von der Richtung ab (siehe SPR_S_THRUST_HEAD-Kommentar), NICHT mehr nur vom
   Slot -- muss daher separat gecacht werden, sonst haelt der Positions-only-Fastpath bei
   Richtungswechsel faelschlich das alte Flip-Bit (gleiches Tile, anderes Flip). */
static u8      g_thrust_oam0, g_thrust_oam1;       /* dynamisch, OAM_NONE = kein Slot zugeteilt */
static u16     g_wpmod_last_s;
static u8      g_wpmod_oam0, g_wpmod_oam1;         /* dynamisch, OAM_NONE = kein Slot zugeteilt */
/* ---- Module der uebrigen Waffen (27.07.2026, Nutzerbericht "Kanone gekauft,
   feuert, ist aber am Schiff nicht sichtbar") ----
   Bis hierher zeichnete draw_sprites() FEST das Modul von Waffe 0
   (lvl_weapon_dx[0]/dy[0], Animation aus lvl_weapon_spr[0]) - egal welche Waffe
   gekauft war. Ein Modul je Waffe gab es nie. Der Block unten ergaenzt genau das
   fuer die Waffen 1..n und laesst den erprobten Pfad von Waffe 0 unangetastet.
   Je Waffe bis zu WPX_CELLS Zellen (die Kanone ist ein 8x16-Metasprite mit
   zwei Zellen), je Zelle ein a- und ein b-Slot aus dem OAM-Pool. Slots werden
   erst geholt, wenn die Waffe aktiv ist, und sofort zurueckgegeben, wenn nicht -
   sonst blockiert eine verkaufte Waffe dauerhaft Poolplaetze. */
#define WPX_CELLS 4u
/* ---- Uebergangswert fuer die Kanonen-Position (27.07.2026) ----
   Die Anbringung gehoert in die map.h (lvl_weapon_dx/dy, im Tool unter
   "Powerup-/Waffen-Typen"). Dort steht fuer Waffe 2 aber noch 0/0, also saesse
   das Modul auf der linken oberen Ecke des Schiffs. Bis der naechste Export die
   richtigen Werte bringt, springt hier -8/10 ein - die vom Nutzer am Bild
   bestaetigte Position.
   SELBSTABSCHALTEND: greift NUR, solange beide Map-Werte 0 sind. Sobald im Tool
   etwas eingetragen ist, gewinnt die map.h automatisch. Der Block darf dann
   ersatzlos raus. Gilt fuer Modul UND Mundung - wp_spawn() rechnet mit denselben
   Werten, deshalb steht die Umrechnung in wpx_dx()/wpx_dy(). */
#define WPX_FALLBACK_W  2u
#define WPX_FALLBACK_DX (-8)
#define WPX_FALLBACK_DY (10)
/* ---- Muendungs-Versatz (27.07.2026) ----
   Der Schuss startete bis dahin exakt auf der Modul-Ecke (lvl_weapon_dx/dy). Sitzt
   das Rohr nicht in der Ecke, laesst sich das so nicht feinjustieren. Das Tool hat
   dafuer jetzt "Muendung dx/dy" je Waffe; der Wert kommt ZUSAETZLICH zu dx/dy.
   Aeltere map.h kennen die Arrays nicht - LVL_WEAPON_MUZZLE ist das Erkennungs-
   merkmal, ohne das hier 0 herauskommt.
   Der Uebergangswert +2/0 fuer die Kanone gilt NUR, solange die map.h die Arrays
   noch nicht mitbringt; mit dem naechsten Export gewinnt sie automatisch. */
#ifdef LVL_WEAPON_MUZZLE
#define WPX_MUZZLE_DX(w) (lvl_weapon_muzzle_dx[w])
#define WPX_MUZZLE_DY(w) (lvl_weapon_muzzle_dy[w])
#else
#define WPX_MUZZLE_DX(w) (((w) == (u8)WPX_FALLBACK_W) ? (s8)2 : (s8)0)
#define WPX_MUZZLE_DY(w) ((s8)0)
#endif
static s8 wpx_dx(u8 w) {
    if (w == (u8)WPX_FALLBACK_W && lvl_weapon_dx[w] == 0 && lvl_weapon_dy[w] == 0)
        return (s8)WPX_FALLBACK_DX;
    return lvl_weapon_dx[w];
}
static s8 wpx_dy(u8 w) {
    if (w == (u8)WPX_FALLBACK_W && lvl_weapon_dx[w] == 0 && lvl_weapon_dy[w] == 0)
        return (s8)WPX_FALLBACK_DY;
    return lvl_weapon_dy[w];
}
static u8      g_wpx_oam[LVL_WEAPON_COUNT][WPX_CELLS][2];
static u16     g_wpx_last[LVL_WEAPON_COUNT][WPX_CELLS];
static u8      g_wpx_n[LVL_WEAPON_COUNT];          /* zuletzt belegte Zellenzahl */
/* Freilaufender Takt fuer die Zellen ab Index 1 (Unterbau der Kanone laeuft auch
   im Leerlauf). Bewusst ein eigener Zaehler statt VBCounter: der wrappt bei 256
   und 256 ist kein Vielfaches von speed*len, was einen sichtbaren Ruckler je
   Umlauf gaebe. u16, wird in draw_sprites() einmal je Frame erhoeht. */
static u16     g_wpx_tick;
/* ---- Modul-Animation je Waffe (27.07.2026) ----
   BUGFIX zur Fassung von vorhin ("Heckwaffe animiert nicht mehr"): dort war der
   Teiler die im Tool gesetzte Bildrate (speed>>1 = 5 Frames je Bild). Die
   Heckwaffe feuert aber alle 3-13 Frames, und JEDER Schuss setzt den Zaehler auf
   0 zurueck - er kam nie ueber Bild 0 hinaus, die Animation stand.
   Jetzt wird die Animation ueber das TATSAECHLICHE Schussintervall gestreckt:
   Teiler = Intervall / Bildzahl, mindestens 1. Damit laeuft sie genau einmal je
   Schuss durch, egal wie schnell gefeuert wird - langsam bei niedriger Feuerrate,
   automatisch schneller bei hoher. Je Waffe eigene Zaehler, weil die Kanone eine
   andere Rate hat als die Heckwaffe. */
static u8      g_wp_afrm[LVL_WEAPON_COUNT];   /* aktuelles Bild */
static u8      g_wp_atck[LVL_WEAPON_COUNT];   /* Frames seit dem letzten Bildwechsel */
static u8      g_wp_adiv[LVL_WEAPON_COUNT];   /* Frames je Bild, beim Schuss gesetzt */
/* PERFORMANCE 27.07.2026 (Nutzerbefund "18-22 fps, 88 VBlanks"): wp_anim_len()
   und sspr_anim_find() durchsuchen ROM-Tabellen linear. Sie standen je Frame in
   zwei heissen Schleifen - einmal je Waffe (11x) und einmal je fliegendem
   Projektil. Auf echter Hardware kostet jeder ROM-Zugriff Wartezyklen; genau
   diese Fehlerklasse hat am 22.07. schon 15->28 fps ausgemacht. Beide Werte
   haengen nur an lvl_weapon_*, aendern sich also nie -> einmal in game_start()
   aufloesen und danach nur noch RAM lesen. */
static u8      g_wp_alen[LVL_WEAPON_COUNT];   /* Bildzahl der Modul-Animation */
static u8      g_wp_bai[LVL_WEAPON_COUNT];    /* Anim-Index des Projektils, 0xFF = keiner */
static TPickup g_pickups[MAX_PICKUPS];
/* Restanzahl lebender Gegner je Gruppe (Index = lvl_spawn_group-Nummer,
   1-basiert; Index 0 unbenutzt/"keine Gruppe"). Bei Erreichen von 0 spawnt
   die zur Gruppe passende lvl_group_reward_* Belohnung (Schritt 14).
   Bugfix 10.07.2026: Array-Groesse darf NICHT von LVL_GROUP_REWARD_COUNT
   abhaengen ??? das ist die Anzahl der Belohnungs-TABELLENZEILEN
   (lvl_group_reward_id[]), nicht die hoechste Gruppen-NUMMER. Aktuell z.B.
   LVL_GROUP_REWARD_COUNT=3, aber lvl_spawn_group[] enthaelt die Nummer 4 ->
   g_group_count[4] war ein Off-by-one-Schreibzugriff hinter dem Array-Ende
   (undefiniertes Verhalten, benachbarte Variablen konnten korrumpiert
   werden) und der Zaehler fuer Gruppe 4 (3 von 5 Spawns!) hat nie
   zuverlaessig funktioniert -> "keine Gruppenbelohnung". Fester, groSSzuegig
   bemessener Puffer statt an eine Tabellengroesse gekoppelt zu sein, die
   mit jedem Map-Export etwas anderes bedeuten kann. */
/* MUSS groesser sein als die hoechste lvl_spawn_group-Nummer (nicht die Anzahl
   der Belohnungszeilen!). Map-Update 18.07.2026: das neue Belohnungssystem
   nutzt Gruppen bis 141 (Spawn 139 = Gruppe 141) ??? mit den alten 32 Slots
   schrieb game_start() g_group_count[141] weit hinter das Array-Ende (Overflow),
   und alle Belohnungen fuer Gruppen >31 funktionierten nie. 142 deckt 0..141 ab;
   bei kuenftigen Exporten mit hoeheren Gruppen-Nummern erneut anheben. */
#define GROUP_COUNT_SLOTS 142u
static u8      g_group_count[GROUP_COUNT_SLOTS];
/* Schubduesen-Animationszustand (ein gemeinsamer Flacker-Takt fuer beide
   Richtungen ??? beim Richtungswechsel springt die Animation einfach neu an,
   das faellt bei einer 2-Frame-Flamme nicht auf). */
static u8      g_thrust_tick, g_thrust_frame;
static u8      g_thrust_dir;    /* 0=aus, 1=vorwaerts (HOCH), 2=rueckwaerts (RUNTER) ??? beide
   nutzen seit Map-Update 13.07.2026 dieselbe Animation (SPR_S_THRUST_HEAD),
   rueckwaerts zusaetzlich V-geflippt, siehe dortigen Kommentar. */
static u8      g_thrust_aidx;   /* Performance-Fix 11.07.2026: von thrust_update() gesetzter
                                    sspr_anim_index_for()-Treffer, von draw_sprites() wiederverwendet
                                    statt im selben Frame ein zweites Mal die 12 Anim-Koepfe zu
                                    durchsuchen (siehe sspr_anim_index_for/hitzone_resolve-Kommentar
                                    fuer dasselbe Muster). */
/* Waffen-Modul-Animation (Nutzerwunsch 10.07.2026, Nacht: "Waffe soll ihre
   Animation abspielen wenn sie feuert") ??? S144 hat laut map.h (lvl_weapon_spr
   Bit14) eine Animation, alle 5 Frames MIT b-Overlay (Bugfix 11.07.2026,
   siehe SPR_WPMODULE-Kommentar). Laeuft NUR waehrend tatsaechlich gefeuert
   wird, siehe weapon_update(); im Ruhezustand Frame 0 (Standbild). */
static u8      g_wpmod_tick, g_wpmod_frame;
static u8      g_wpmod_aidx;   /* Performance-Fix 11.07.2026, siehe g_thrust_aidx-Kommentar */
/* Flacker-Takt fuer Schuss- und Ziffern-Sprites (Nutzervorgabe 07.07.2026,
   Nacht: "nur jedes zweite Frame, versetzt") ??? jede Instanz i ist sichtbar,
   wenn FLICKER_VIS(i), blinkt also mit 15Hz@30fps, benachbarte Instanzen
   phasenversetzt. Update 19.07.2026 (Nutzerwunsch "alle Bullets versetzt 15fps"):
   ALLE Schuss-Typen (Ship-vorwaerts, Heck, Gegner, Pflanze) nutzen jetzt diesen
   Takt und geben ihren OAM-Slot auf der OFF-Phase FREI -> spart OAM (nur ~die
   Haelfte belegt gleichzeitig). Ziffern bleiben wie bisher (Slot fest, nur
   versteckt). */
#define FLICKER_VIS(i) ((((u8)(g_flicker + (u8)(i))) & 1u) == 0u)
static u8      g_flicker;
/* 22.07.2026 von u16 auf u32: mit u16 lief der Punktestand bei 65535 ueber (im
   Emulator-Rundflug gemessen: 59740 -> 1704), obwohl das HUD sieben Stellen hat.
   Die Ziffernausgabe kam bisher mit %10 und /10 aus - das geht bei 16 Bit, bei
   32 Bit fehlt der Toolchain aber die Division (C9H_divlu, siehe ??0.1). Deshalb
   rechnet score_draw() jetzt mit fortlaufender Subtraktion, siehe SCORE_POW10. */
static u32     g_score;        /* echter Punktestand (Ziel) */
static u32     g_score_shown;  /* angezeigter Stand: rollt in 10er-Schritten auf g_score zu */
#define SCORE_MAX 9999999uL    /* 7 Stellen, mehr zeigt die Bar nicht */
/* Shop-Waehrung (20.07.2026), STRIKT getrennt vom Punktestand ??? genau wie im
   Original (DOS: Geld liegt in 0x915a/5c, 32 Bit; Punkte separat, upgrades.md ??1).
   Quelle ist ausschliesslich die Gruppen-Belohnung kind 0 ("Geld", im Tool die
   Cash-Blasen 50/100), NICHT lvl_spawn_points ??? das bleibt reiner Score.
   u32 weil die Waffen bis 6000 kosten und ein langer Durchlauf sonst die 65535
   reissen wuerde (DOS ist aus demselben Grund 32-bittig). */
static u32     g_cash;
static u8      g_score_roll_p; /* Frame-Zaehler fuer den naechsten +10-Odometer-Schritt */
static u16     g_scroll_y;
/* Pro Spawn-Eintrag (nicht pro Scan-Index!) getrackt ??? lvl_spawn_row ist NICHT
   nach Zeile sortiert (05.07.2026-Export: 22,44,33,77,100), ein einfacher
   sequenzieller Scan wuerde Ketten dadurch in falscher Reihenfolge/gleichzeitig
   ausl??sen. Jeder Eintrag prueft unabhaengig seine eigene Trigger-Zeile. */
static u8      g_spawn_fired[LVL_SPAWN_COUNT]; /* 1 = Trigger-Zeile bereits passiert */
static u8      g_spawn_left[LVL_SPAWN_COUNT];  /* verbleibende Gegner der Kette */
static u8      g_spawn_timer[LVL_SPAWN_COUNT]; /* Restframes bis zum naechsten Kettenglied */
/* Fix 16.07.2026 (Spawn-Timing, siehe enemies_update): eigener Trigger-Zeilen-
   Zaehler ab 0, entkoppelt vom Prefill-Offset in g_lvl_row. */
static u16     g_spawn_scroll_row;
/* 22.07.2026: Zahl der Wellen mit noch ausstehenden Mitgliedern. Erspart die
   113er-Schleife in enemies_update, solange nichts ansteht (der Normalfall). */
static u8      g_spawn_pending;
/* Performance 17.07.2026: die Spawn-Trigger-Pruefung (ROM-Zugriff auf
   lvl_spawn_row pro nicht-gefeuertem Spawn) lief JEDEN Frame ueber alle 140
   Spawns ??? dabei kann ein Spawn nur faellig werden, wenn g_spawn_scroll_row
   sich aendert (alle ~48 Frames). Flag setzen wenn die Trigger-Zeile waechst,
   Trigger-Scan nur dann. Startwert 1, damit der erste Frame prueft. */
static u8      g_spawn_row_changed;
static u8      g_spawn_row_frame_acc;
static u8      g_state;
static u8      g_shop_a_count;  /* STATE_SHOP: Anzahl der bereits erkannten J_A-Edge-Presses (0-2) */

/* ============ Respawn-Checkpoints (Punkt 4, 21.07.2026) ============
   Aus S1/EVENTSBG.PC (Typ-0-Records, Handler 1000:35ce) gezogen und auf unsere
   Karte umgerechnet (Zeile = 293 - Scroll/16). Genutzt werden im Original nur
   triggerY (Respawn-Scroll) und word4 (Respawn-X); das Y-Feld im Record ist
   ungenutzt, der Wiedereinstieg nimmt ein FESTES Y (0xB0 = 176 DOS = 88 bei uns).
   Wie die Wurmpfade bewusst HARTKODIERT: der Tool-Export kennt keine Checkpoints,
   und diese Werte sind Originaldaten, keine Design-Entscheidung.
   Verhalten laut 1000:1a62 -> 18f5 + 195b (siehe CLAUDE.md 0.4e):
     * Scroll springt auf die Checkpoint-Zeile zurueck, Rueckwaerts-Limit ebenso
     * KEINE Unverwundbarkeit - die Sicherheit kommt daher, dass Checkpoints an
       sicheren Stellen liegen
     * Welt wird gewischt, Wellen werden neu scharf gemacht
     * Energie voll, Autofire zurueck auf 1; Punkte/Tempo bleiben erhalten */
#define CHECKPOINT_RESPAWN_Y 88u
/* 22.07.2026: Die Checkpoints kommen jetzt aus dem Tool (Checkpoint-Editor unter
   "Scroll-Verhalten"), Export lvl_checkpoint_row[]/lvl_checkpoint_x[]. Bis die
   map.h neu exportiert ist, greift der Rueckfall auf die bisher hier fest
   verdrahteten Originalwerte - so baut der Code mit ALTEN und NEUEN Exporten.
   Die Namen werden per Makro umgebogen, damit die Nutzungsstellen unberuehrt
   bleiben (und weil cc900 mit nicht-trivialen statischen Initialisierern,
   also auch Zeiger-Aliassen, Probleme macht - siehe ??4). */
#ifdef LVL_CHECKPOINT_COUNT
#define LVL1_CHECKPOINT_COUNT ((u8)LVL_CHECKPOINT_COUNT)
#define lvl1_checkpoint_row   lvl_checkpoint_row
#define lvl1_checkpoint_x     lvl_checkpoint_x
#else
#define LVL1_CHECKPOINT_COUNT 8u
/* 28.07.2026 +7: die Map ist auf 300 Zeilen gewachsen, die Scroll-Formel lautet
   damit Zeile = 300 - Scroll/16 statt 293 - Scroll/16. Letzter Checkpoint 292 =
   Boss-Arena, deckt sich mit BOSS_BODY_ROW_LO. */
static const u16 lvl1_checkpoint_row[LVL1_CHECKPOINT_COUNT] = { 12u, 48u, 84u, 120u, 147u, 187u, 229u, 292u };
static const u8  lvl1_checkpoint_x[LVL1_CHECKPOINT_COUNT]   = { 76u, 84u, 84u, 76u, 76u, 76u, 76u, 36u };
#endif
static u8 g_checkpoint;        /* Index des zuletzt passierten Checkpoints */
static u8 g_checkpoint_seen;   /* 0 = noch keiner passiert -> Levelanfang */
static u8 g_respawn_pending;   /* Tod erkannt, Wiedereinstieg am Frame-Ende */

/* 23.07.2026: Loadout-/Geld-Rueckfall beim Tod (s0.4e, "wird beim Passieren
   mitgesichert"). Beim Passieren eines Checkpoints wird der Stand von Geld,
   Waffen und Power festgehalten; respawn_do() setzt ihn beim Wiedereinstieg
   zurueck. Tempo (speed_stage) und Punkte bleiben laut Original erhalten,
   Autofire/Energie setzt respawn_do() ohnehin (firerate=0, Energie voll). */
static u32 g_cp_cash;
static u16 g_cp_weapons;
static u8  g_cp_power;

/* 23.07.2026: waehrend des Boss-Cash-Regens ist der Spieler unverwundbar
   (Original 1000:3c56, s0.5 Pkt.8). >0 solange Regen-Token offen sind. */
static u8  g_boss_rain;

/* 23.07.2026: Super Nashwan Power (Ablauf-Timer + VOR-Nashwan-Loadout, s. u.). */
static u16 g_nashwan_timer;
static u16 g_nash_weapons;
static u8  g_nash_power, g_nash_firerate;

static u8      g_shop_entered;
/* 22.07.2026: g_shop_done (EIN Latch fuer alle Shops) ersetzt durch einen Latch
   JE AUSLOESER. Vorher wertete der Code ohnehin nur lvl_shop_trigger_row[0] aus -
   der zweite Ausloeser (kind 1 = "nach Endboss", 300 Frames Verzoegerung) war gar
   nicht implementiert, der Shop nach dem Boss oeffnete nie. */
static u8      g_shop_fired[LVL_SHOP_TRIGGER_COUNT];
static u16     g_shop_delay;       /* >0: Countdown in Frames bis STATE_SHOP */
static u16     g_shop_return_row;  /* Spielerzeile beim Shop-Eintritt, siehe shop_resume() */
/* 22.07.2026: 1 = der gerade offene Shop wurde vom ENDBOSS ausgeloest (kind 1).
   Beim Verlassen laeuft dann nicht der normale Ruecksprung, sondern Level 1
   beginnt von vorn - mit allem Erspielten (Nutzerwunsch, solange es kein
   Level 2 gibt). Siehe level_loop_restart(). */
static u8      g_shop_was_boss;
static u8      g_shop_last_item;   /* Item, von dem aus in die Knopfreihe gesprungen wurde */  /* STATE_SHOP: shop_enter() (Screen/Text) schon gezeichnet? */

/* Passenden Ausloeser suchen und scharf schalten. kind 0 = bei Map-Zeile `row`
   (row 0 = Eintrag ungenutzt), kind 1 = nach dem Endboss. Die Verzoegerung aus
   lvl_shop_trigger_delay ist beim Boss das Sammelfenster fuer den Cash-Regen. */
static void shop_trigger_check(u8 kind, u16 row) {
    u8 t;
    if (g_shop_delay != 0u) return;             /* es laeuft schon einer */
    for (t = 0u; t < (u8)LVL_SHOP_TRIGGER_COUNT; t++) {
        if (g_shop_fired[t]) continue;
        if (lvl_shop_trigger_kind[t] != kind) continue;
        /* >= statt ==: exakte Gleichheit ist zerbrechlich. lvl1_prefill setzt
           g_lvl_row auf row_base+19, ein Wiedereinstieg an Checkpoint 3 (Zeile
           113) startet also bei 132 und SPRINGT ueber die Ausloesezeile 125 -
           der Shop kaeme nie. Der Latch g_shop_fired[] verhindert Mehrfach-
           ausloesen, deshalb ist >= hier gefahrlos. */
        if (kind == 0u && (lvl_shop_trigger_row[t] == 0u || row < lvl_shop_trigger_row[t])) continue;
        g_shop_fired[t] = 1u;
        g_shop_delay    = (u16)(lvl_shop_trigger_delay[t] + 1u);   /* +1, weil 0 = "aus" */
        g_shop_was_boss = (u8)(kind == 1u);   /* Endboss-Shop -> beim Verlassen Level-Neustart */
        return;
    }
}

/* Am Frame-ENDE takten (wie respawn_do): der Zustandswechsel zieht shop_enter()
   nach sich, und das schreibt VRAM. */
static void shop_delay_tick(void) {
    if (g_shop_delay == 0u) return;
    g_shop_delay--;
    if (g_shop_delay == 0u) {
        g_state = STATE_SHOP;
        g_shop_a_count = 0u;
        /* Ruecksprungzeile merken: shop_resume() setzte die Welt bisher auf die
           fest verdrahtete LVL1_SEGB_ROW_BASE zurueck (siehe dort). */
        g_shop_return_row = (u16)(g_scroll_y >> 3);
    }
}

/* ===== Live-Tuning (17.07.2026, Nutzerwunsch) =====
   Vier Fahrgefuehl-Werte, im Spiel per OPTION verstellbar. Die #defines oben
   bleiben als Startwerte stehen ??? was hier steht, ist zur Laufzeit aenderbar.
   Hintergrund: diese Werte stammen NICHT aus dem Original, sondern aus
   Tuning-Runden am 10./11.07. ("90 px/s war zu schnell" -> 60). Ein Abgleich
   mit den echten Originaldaten (DOS_Version/.../docs/formats/timing.md, 18.2-Hz-
   Tick) zeigt: Scroll passt (1,25 vs 1,14 Kacheln/s), das Schiff ist mit 37,5%
   Bildbreite/s dagegen doppelt so agil wie das Original (17%). Die gemeldete
   Zaehigkeit liegt also vermutlich NICHT am Tempo -> deshalb messen statt raten. */
/* Ohne Initializer: die #defines stehen weiter unten (COAST_FRAMES erst ~Z.789),
   waeren hier also noch nicht bekannt ??? und cc900 mag nicht-triviale
   Static-Initializer ohnehin nicht (siehe Kalibrier-Build-Kommentar 16.07.).
   Startwerte setzt tune_init() aus main(). */
static u8 g_tune_speed;    /* /16 px pro Frame */
static u8 g_tune_scroll;   /* Frames pro Scroll-Pixel (kleiner = schneller) */
static u8 g_tune_coast;    /* Auslauf-Frames beim Loslassen (0 = aus) */
static u8 g_tune_tilt;     /* Frames pro Kippstufe (kleiner = schneller) */
/* Gegner-Tempo als MULTIPLIKATOR auf den Pfad-Fortschritt: 16 = 1.0x (Originalwert),
   8 = 0.5x, 32 = 2.0x. Bewusst NICHT die lvl_path_data-Deltas skalieren ??? die
   bestimmen die FORM der Kurve; sie zu strecken machte den Bogen groesser, nicht
   den Gegner schneller. Stattdessen laeuft der Pfad-Frame schneller/langsamer,
   die Bahn bleibt exakt dieselbe. */
static u8 g_tune_espeed;
static u8      g_tilt_timer;   /* Frames seit letztem Tilt-Stufenwechsel */
static s8      g_tilt_level;   /* -2=L2 -1=L1 0=M 1=R1 2=R2 */
static u8      g_scr1_y;
static u8      g_scroll_tick;  /* 0-9, bei 10 -> 1px scrollen (1px/10 Frames) */
static u8      g_move_acc;     /* Subpixel-Akku Schiffstempo (0-15 Sechzehntel-px) */
static u8      g_move_step;    /* px-Schritt dieses Frames (0/1) */
/* Auslaufen (Nutzerwunsch 11.07.2026): haelt beim Loslassen einer Richtung
   noch COAST_FRAMES lang die zuletzt gedrueckte Richtung fest, statt sofort
   stehenzubleiben ??? je Achse unabhaengig (Diagonalflug: nur eine Achse
   loslassen laesst nur die auslaufen). Nutzerkorrektur 11.07.2026: 6 Frames
   war unsichtbar, auf 20 angehoben, dann als zu viel gemeldet -> auf 1/3
   von 20 (~7 Frames) reduziert. */
#define COAST_FRAMES 4u   /* 30-fps-Cap: 7->4 (Auslauf-Dauer halbiert) */
static u8      g_coast_x, g_coast_y;      /* verbleibende Auslauf-Frames je Achse */
static s8      g_last_dir_x, g_last_dir_y; /* -1/0/1, Richtung waehrend des Auslaufens */
static u8      g_back_acc;        /* Subpixel-Akku Rueckwaertsflug (1/32 px) */
static u8      g_back_speed;      /* aktuelles Rueckwaerts-Tempo, 0..BACK_SPEED_MAX */
static u8      g_back_accel_tick; /* Frame-Zaehler fuer die Beschleunigung */
static u8      g_back_budget;  /* Restframes Rueckwaertsflug (max BACK_BUDGET_MAX) */
static u8      g_back_px;      /* verfuegbare Rueckwaerts-Distanz in px (max BACK_PX_MAX) */
/* u16 (nicht u8): LVL_MAP_H ist seit dem Map-Update 293 Zeilen, > 255 */
static u16     g_lvl_row;   /* naechste NEUE Map-Zeile, die oben einrollt (startet bei 19, zaehlt hoch bis LVL_MAP_H) */
static u8      g_bar_vrow;  /* physische Ringzeile (0-31), die aktuell an Bildschirm-y=144 liegt (Bar) */
/* Bar-Rahmen-Redraw wird bei Zeilenwechsel NICHT mehr sofort (mitten im Frame in
   scroll_update) gezeichnet, sondern via Flag ans FRAME-ENDE verschoben (mit den
   Ziffern, bar_redraw_flush vor score_draw). Grund (Nutzer-HW 18.07.): die Ziffern
   (Frame-Ende) flackern NIE, der mitten im Frame geschriebene Rahmen schon ???
   Schreiben ins Bar-VRAM waehrend der Beam es liest. 0=nichts, 1=vorwaerts (Strip
   leeren), 2=rueckwaerts (Terrain-Restore alte Zeile). */
static u8      g_bar_redraw;
static u8      g_bar_redraw_old;   /* alte Ringzeile fuer den Rueckwaerts-Restore */
static u8      g_bar_restore_pending = 0xFFu;   /* 0xFF = nichts offen; sonst Ringzeile, die EINEN Frame SPAETER Terrain wird (siehe bar_redraw_flush) */
/* ??5.4 (18.07.2026): Ziffern sind BG-Tiles in der Bar-Ringzeile. Dieser Cache
   haelt die zuletzt gezeichnete Zahl; bar_draw_at() setzt ihn auf 0xFFFF, damit
   score_draw() die Ziffern nach jedem Bar-(Neu)Zeichnen wieder aufsetzt. */
static u32     g_score_last_shown = 0xFFFFFFFFuL;
static u16     g_score_aux_last = 0xFFFFu;   /* zuletzt gezeigter Diagnosewert (FPS/Palette) */
/* Buchfuehrung: welche map_row liegt gerade in physischer Ringzeile X (Index 0-31)?
   Noetig, damit die Bar/Separator eine verlassene Ringzeile beim Weiterziehen exakt
   auf das dort eigentlich hingehoerende Terrain zur??cksetzen kann. u16 aus demselben
   Grund wie g_lvl_row. */
static u16     g_row_map[32];

// --- Input --
/* ===== TESTPHASE-Hilfen (Wandwurm-Test 2026-07-19) ??? auf 0 setzen zum Deaktivieren.
   1 = Schiff unsterblich (player_hit wirkungslos) + Pause mit OPTION-Taste. ===== */
#define WORM_TEST_MODE 0   /* 21.07.2026 AUS: Schiff wieder verwundbar, normaler Scroll,
                              Woermer spielen regulaer. Zum Wurm-Testen wieder auf 1. */
/* ===== TESTPHASE Shop (2026-07-20): 1 = die HUD-Ziffern zeigen g_cash statt
   FPS/Wellen-ID. Damit laesst sich auf echter HW pruefen, ob die eingesammelten
   Cash-Blasen (50/100) zu den Waffenpreisen (1000-6000) passen, SOLANGE der Shop
   selbst noch keine Cash-Anzeige hat. Hat Vorrang vor WORM_TEST_MODE.
   Auf 0 setzen = alles bleibt wie vorher (FPS-Zaehler, Nutzerwunsch 18.07.). ===== */
#define CASH_TEST_MODE 0
/* ===== ROW_TEST_MODE (22.07.2026): 1 = die HUD-Ziffern zeigen g_lvl_row
   (die naechste oben einrollende Map-Zeile) statt FPS. Eingefuehrt, um den
   nicht ausloesenden Shop-Trigger (g_lvl_row == lvl_shop_trigger_row[0]) im
   Emulator nachzuvollziehen: laeuft der Zaehler ueber 125 hinweg, liegt es
   nicht am Streaming. Hat Vorrang vor CASH_TEST_MODE/WORM_TEST_MODE. ===== */
#define ROW_TEST_MODE 0
/* ===== BOSS_TEST_MODE (22.07.2026): 1 = die HUD-Ziffern zeigen den Boss-
   Zustand als HP*1000 + Reichweite*10 + aktiv, also z.B. 30111 = 30 HP,
   Tentakel voll ausgefahren (11), aktiv. Damit laesst sich im Emulator
   ablesen, ob Treffer ankommen, ob die Glieder schlucken und ob der
   Ausfahr-/Einzieh-Zyklus laeuft. Hat Vorrang vor ROW_TEST_MODE. ===== */
#define BOSS_TEST_MODE 0
/* ===== FPS_PROFILE_MODE (22.07.2026): 1 = die HUD-Ziffern zeigen g_vbc_sum,
   also die VBlanks ueber 30 Spielframes. 60 = saubere 30 fps, 90 = 20 fps.
   Lineare Groesse zum Profilieren des bekannten FPS-Einbruchs (??7e/TODO 11).
   Hat Vorrang vor ROW_TEST_MODE. ===== */
#define FPS_PROFILE_MODE 0
/* ===== ENERGY_TEST_MODE (22.07.2026): 1 = die HUD-Ziffern zeigen
   Energie*100 + Leben, z.B. 3905 = volle 39 Energie und 5 Leben. Zum Pruefen
   des neuen Energiesystems, solange die Leiste im HUD noch fehlt. ===== */
#define ENERGY_TEST_MODE 0
/* ===== SPLIT_OFF_TEST (22.07.2026): 1 = MicroDMA-Rastersplit im Gameplay AUS.
   Zerlegt die HUD-Bar optisch (sie scrollt dann mit), dient NUR der Messung:
   ??7e verdaechtigt den Split (152x2 Transfers + Tabellenbau je VBlank) als
   Ursache des FPS-Einbruchs. Differenz zu SPLIT_OFF_TEST 0 = seine Kosten. ===== */
#define SPLIT_OFF_TEST 0
/* ===== BUSY_TEST (22.07.2026): >0 = kuenstliche Rechenlast pro Frame (Anzahl
   Schleifendurchlaeufe). KALIBRIERUNG des Messaufbaus: steigt g_vbc_sum damit
   ueber 60, modelliert der Emulator CPU-Zeit und die Messungen taugen etwas.
   Bleibt er bei 60, bekommt jeder Frame beliebig viel Rechenzeit - dann ist der
   FPS-Einbruch im Emulator grundsaetzlich nicht messbar. ===== */
#define BUSY_TEST 0
/* ===== TELEMETRY (22.07.2026): 1 = legt am Frame-Ende ein paar Kennwerte an
   eine FESTE RAM-Adresse, die der Emulator-Treiber auslesen kann (m.read).
   Ohne das hat tools/emu_tour.py keine Rueckmeldung und muss Schiffsposition
   und Scroll aus der Zeit hochrechnen - das driftet weg, das Schiff streift
   Terrain, der Scroll stockt, und die Hochrechnung wird dadurch noch falscher.
   0x6000 liegt hinter far_area (endet laut xenon.map bei 0x5FBA) im freien
   RAM-Loch bis 0x6C00, kollidiert also mit nichts. ===== */
/* ===== SCORE_TEST_VALUE (22.07.2026): >0 = Punktestand beim Levelstart auf
   diesen Wert setzen. Prueft die divisionsfreie 7-stellige Ziffernausgabe
   (score_draw/SCORE_POW10) ohne stundenlang Gegner abschiessen zu muessen. ===== */
#define SCORE_TEST_VALUE 0uL
/* ===== WEAPON_TEST_MASK (22.07.2026): >0 = diese Waffen beim Levelstart als
   montiert setzen (Bitmaske ueber lvl_weapon_*, Bit 0 = Heckwaffe).
   Der Umweg ueber den Shop taugte zum Pruefen nicht: die Tastenfolge landet je
   nach Besitzstand auf BUY oder SELL, ein Versuch hat die vorhandene Waffe
   verkauft statt eine neue zu kaufen. Hiermit laesst sich die WIRKUNG isoliert
   pruefen, ohne die Shop-Oberflaeche dazwischen.
   Beispiele: 0x0001 nur Heck ?? 0x0002 nur Waffe 1 ?? 0x0007 Heck + 1 + 2. ===== */
#define WEAPON_TEST_MASK 0x0000u
/* ===== FPS_IN_SCORE (22.07.2026, Nutzerwunsch): 1 = die beiden LINKEN
   Ziffern der Punkteanzeige zeigen die FPS, die restlichen fuenf den
   Punktestand (also bis 99999). Zum Mitlesen waehrend des Spielens. ===== */
#define FPS_IN_SCORE 1
/* ===== FPS_VBC_DISPLAY (25.07.2026, Nutzerwunsch "lass fps und VBlank im
   Zaehler, bis alles laeuft"): zeigt zusaetzlich zu den FPS die VBlank-Summe,
   OHNE den PROFILE_MODE dafuer einzuschalten. Das ist der springende Punkt -
   PROFILE_MODE belegt OPTION+HOCH/RUNTER mit der Blockauswahl, und ein
   versehentlicher Druck legt Block 5 (alle Schuss-Systeme) still. Genau dieses
   Phantom hat schon einmal einen halben Tag gekostet (CLAUDE.md-Notiz zum
   Spieltest-Build). Hier bleibt OPTION die Unverwundbarkeit.

   Anzeige:  [FPS][FPS] 0 0 [VBlanks je 30 Frames]
     060 = saubere 30 fps    090 = 20 fps    120 = 15 fps
   Der Punktestand ist damit verdeckt - bewusst, bis die Messerei durch ist.
   Vor der Veroeffentlichung auf 0 (dann zeigt die Anzeige wieder Punkte). ===== */
#define FPS_VBC_DISPLAY 1
/* ===== PAL_SELFCHECK (22.07.2026): 1 = Sprite-Paletten jeden Frame gegen die
   Quelle vergleichen (spr_pal_check). Die Zahl der Abweichungen laesst sich
   ueber PAL_SHOW_IN_SCORE ins HUD holen - siehe spr_pal_check() fuer die
   Auswertung. Kostet 48 Wortvergleiche pro Frame, im Emulator gemessen
   unkritisch (das Spiel nutzt nur ~1/4 seines Frame-Budgets). ===== */
#define PAL_SELFCHECK 1
/* Die beiden linken Ziffern zeigen normal die FPS. Solange OPTION GEHALTEN
   wird, stattdessen den groessten je gemessenen Palettenfehler (g_pal_worst) -
   so sind beide Werte ablesbar, ohne dass die Anzeige mehrdeutig wird.
   0 = die Palettenzahl gar nicht anzeigen. */
#define PAL_SHOW_ON_OPTION 1
/* 1 = bei erkannter Abweichung die Paletten neu laden (Selbstheilung).
   Erst einschalten, wenn PAL_SELFCHECK ueberhaupt Abweichungen zeigt. */
/* 22.07.2026 EINGESCHALTET fuer den naechsten Hardware-Durchgang: der Nutzer
   meldet vor dem ersten Shop gruen/rot/gelbe SPRITES (Map in Ordnung) und nach
   dem Shop korrekte Farben - und shop_resume() ruft genau spr_pal_load() erneut
   auf. Wenn ein Neuladen die Farben repariert, ist der einmalige Upload in
   main() ueberfuehrt. Kostet nur etwas, wenn wirklich eine Abweichung auftritt. */
#define PAL_SELFHEAL 1
/* ===== PAL_FAULT_TEST (22.07.2026): >0 = beim Levelstart absichtlich so viele
   Farbwoerter verfaelschen. NUR zum Pruefen, dass die Selbstpruefung ueberhaupt
   ausschlaegt und die Anzeige stimmt - ein Messgeraet, das man nie ausschlagen
   gesehen hat, beweist naemlich gar nichts. Vor der Auslieferung auf 0. ===== */
#define PAL_FAULT_TEST 0
/* ===== PROFILE_MODE (22.07.2026) ??? systematische FPS-Untersuchung auf ECHTER
   HARDWARE. Der Emulator laeuft mit sauberen 30 fps und ~1/4 Frame-Budget, auf
   Hardware sind es 15-20 - und zwar auch mit dem Stand vom 21.07., der Einbruch
   ist also alt (??7e, "nie profiliert").

   Damit das nicht ein Dutzend Flash-Durchgaenge kostet, laesst sich hier EIN
   Block zur Laufzeit stilllegen und die Wirkung sofort ablesen:

     OPTION + HOCH   naechster Block          OPTION + RUNTER  vorheriger
     OPTION halten   linke Ziffern = gewaehlter Block statt FPS

   Anzeige waehrend PROFILE_MODE: [FPS][FPS] [Block] 0 0 0 0

     0  nichts aus (Referenzwert)
     1  Raster-Split/MicroDMA   (??7e-Hauptverdaechtiger: 152x2 Transfers/VBlank)
     2  draw_sprites            (OAM-Schreibzugriffe, groesster Einzelblock)
     3  Gegner/Woermer-Update   (Pfade, KI)
     4  Kollisionen
     5  Schuesse (eigene, gegnerische, Map-Objekte, Waffen)
     6  scroll_update           (Terrain-Streaming)
     7  anim_update
     8  score_draw + Bar

   ERGEBNIS 1. Durchgang (Nutzer-HW 22.07.2026): Block 3 = 30 fps, alle
   anderen 15-20. Die Zeit steckt also im Gegner-/Wurm-Update - NICHT im
   MicroDMA (Block 1) und nicht im Zeichnen (Block 2). Damit ist der
   Hauptverdaechtige aus ??7e ausgeschlossen. Block 3 aufgeteilt:

     9  nur enemies_update
    10  nur metaenemies_update
    11  nur worms_update       (Ketten-Gegner aus dem Tool)
    12  nur wallworms_update   (die hartcodierten Wandwuermer)

   25.07.2026: Block 2 (Zeichnen) ist der dominante Posten (Nutzermessung:
   Referenz 26 fps, ohne Zeichnen 52). Deshalb ist er jetzt ebenso aufgeteilt:

    13  nur Schiff zeichnen
    14  nur Gegner zeichnen        (TEnemy)
    15  nur Metasprite-Gegner zeichnen
    16  nur Wurm-Ketten zeichnen
    17  nur Wandwuermer zeichnen
    18  nur Waffen-Schuesse zeichnen
    19  nur Sonderwaffen zeichnen  (Bombe/Laser/Electro/Minen)
    20  Spielerschuesse, Blumen-Schuesse, Pickup-/Waffenmodul-Slotvergabe
    21  Schiff-Blinken + Schubduesen
    22  Waffenmodul-Sprite
    23  Pickups zeichnen

   Damit ist draw_sprites() LUECKENLOS aufgeteilt: 13-23 decken jede
   Anweisung ab. Was Block 2 kostet, sich aber auf keinen Teilblock verteilt,
   kann dann nur noch Aufrufkosten/Rahmen sein - oder ein Messartefakt.

   Messung 25.07.2026 (Arena, 7 Kaefer, ungedeckelt): 0=31, 2=52-54, 13=31,
   14=31, 15=41. In Framezeit: Referenz 32,3 ms, Kaefer 7,9 ms, Zeichnen
   gesamt 13,0 ms -> ~5 ms stecken in den uebrigen Zeichen-Systemen, die
   17-19 jetzt aufschluesseln.

   Das Bild wird dabei teils unvollstaendig - das ist gewollt, gemessen wird die
   ZAHL. Vor der Veroeffentlichung auf 0. ===== */
#define PROFILE_MODE 0
#define TELEMETRY 0
/* 23.07.2026 NUR ZUM NACHWEIS des Loadout-Rueckfalls (s0.4e): nach Checkpoint 0
   Loadout aufwerten (+500 Cash, Waffe 1, Power 2), kurz darauf einen Tod
   erzwingen. Braucht TELEMETRY. VOR VEROEFFENTLICHUNG: 0. */
#define ROLLBACK_TEST 0
/* 23.07.2026 NUR ZUM NACHWEIS der Cash-Regen-Unverwundbarkeit (s0.5 Pkt.8):
   loest im normalen Spiel den Regen aus und versucht danach jeden Frame Schaden.
   Solange g_boss_rain steht, muss die Energie stehen bleiben; sobald die Token
   verfallen, greift Schaden wieder. Braucht TELEMETRY, KEIN God-Mode. Vor
   Veroeffentlichung: 0. */
#define RAIN_TEST 0
/* 23.07.2026 NUR ZUM NACHWEIS von Energie-Pickup (kind 5) + Smart Bomb (kind 6):
   senkt kurz die Energie und ruft die beiden apply_pickup-Effekte auf. Braucht
   TELEMETRY. Vor Veroeffentlichung: 0. */
#define PICKUP_TEST 0
/* 23.07.2026 NUR ZUM NACHWEIS von Super Nashwan Power (kind 7): loest es bei
   Zeile 6 aus. Braucht TELEMETRY. Vor Veroeffentlichung: 0. */
#define NASHWAN_TEST 0
#define TELEMETRY_ADDR 0x6B00u
/* 28.07.2026: von 0x6800 auf 0x6B00. 0x6800 lag INZWISCHEN MITTEN IN DEN
   VARIABLEN von xenon.c (f_area reichte bis 0x68A7) - die Telemetrie hat damit
   echte Spielvariablen ueberschrieben und der ROM startete nicht mehr. Der
   Linker meldet so etwas NICHT, es ist eine reine Adressabsprache.
   Aktuell (nach dem Zusammenlegen der Map-Objekt-Gitter): f_area endet bei
   0x6955, der lcf gibt RAM bis 0x6BFF -> 0x6B00 liegt sicher dazwischen.
   NACH JEDER neuen Variablen gegen xenon.map pruefen: die Zeile
   "ram far_area DATA ... f_area (sounds.rel)" plus ihre Laenge ist das Ende. */
/* ===== WARP_CHECKPOINT (22.07.2026): -1 = aus (normaler Levelstart).
   0..7 = das Spiel startet direkt am jeweiligen Checkpoint aus
   lvl1_checkpoint_row[] (5/41/77/113/140/180/222/285). 7 = Boss-Arena.
   Vor der Veroeffentlichung auf -1. ===== */
/* 24.07.2026 DETERMINISTISCHER Gegner-Benchmark: statt Freiflug + Gegner-Pile-up
   (schwankende Zahlen 64/80/90) haelt bench_determ_tick() JEDEN Frame eine FESTE
   Zahl Gegner an festen Positionen (Tempo 0 -> stehen still, sterben/despawnen
   nie), schaltet alle weiteren Spawns ab und friert den Scroll in ruhigem Terrain
   ein (kein Wurm). Szene damit jeden Frame identisch -> Block-Toggles liefern
   WIEDERHOLBARE Deltas. Auf 0 -> alter Freiflug-Benchmark (WARP=113, Hold=Wurm). */
#define BENCH_DETERM 0    /* 25.07.2026 aus: normales Spiel (kein fester Gegner-Benchmark) */
/* 1 = WURMBAND-Variante des deterministischen Benchmarks: KEINE festen Gegner,
   stattdessen alle Wandwurm-Slots dauerhaft voll, Scroll IM Band (Zeile 113).
   Misst den einzigen Ort, der real unter 30 fps faellt. 0 = normales Spiel. */
#define BENCH_DETERM_WORM 0    /* 25.07.2026 aus: normale gestaffelte Wandwuermer */
/* ===== BENCH_META (25.07.2026, Nutzerwunsch): "5 Metasprites muessen dauerhaft
   im Bild fliegen, sonst geht es in der Variabilitaet unter". Szene am
   SCROLL-ENDE / Endboss eingefroren; dort kreisen BENCH_META_N Alien-Kaefer
   (4-Zellen-Metasprites mit a+b-Ebene = 8 OAM-Slots je Kaefer) dauerhaft auf
   einer festen Kreisbahn. Konstante Zeichen- UND Update-Last -> die
   Profiling-Block-Toggles (OPTION+Hoch/Runter) liefern wiederholbare Zahlen.
   Vor der Veroeffentlichung auf 0. ===== */
#define BENCH_META 0
/* 28.07.2026 NUR ZUM NACHWEIS der Wandkriecher (tools/probe_plobs.py): springt in
   die Kriecherzone. Der Weg dorthin laesst sich nicht abfliegen - ohne Telemetrie
   ist die Schiffsposition unbekannt und das Schiff verkeilt sich am Terrain.
   Vor Veroeffentlichung: 0. */
#define WCRAWL_TEST 0
#define BENCH_META_N 7u
#if BENCH_META
#define WARP_CHECKPOINT 7   /* Boss-Arena (Zeile 285) = Scroll-Ende */
#elif BENCH_DETERM_WORM
#define WARP_CHECKPOINT 3   /* direkt ins Wandwurm-Band (Zeile 113) */
#elif BENCH_DETERM
#define WARP_CHECKPOINT -1  /* kein Warp: Start Zeile 0, Hold in ruhigem Terrain (s.u.) */
#else
#define WARP_CHECKPOINT -1  /* 25.07.2026 normaler Levelstart (Map hat eh keine Checkpoints) */
#endif
/* ===== PAL_RELOAD_TEST (22.07.2026) ??? HARDWARE-Diagnose fuer die grasgruenen/
   gelben Sprites im ERSTEN Levelabschnitt (im Emulator nicht reproduzierbar,
   siehe ??7e; nach dem Shop sind die Farben laut Nutzer korrekt).
   Verdacht: die einmaligen Uploads in main() (spr_tiles_upload/spr_pal_load,
   danach build_lvl1/build_bar_assets) laufen OHNE VBlank-Sync mitten im
   aktiven Bildaufbau. shop_resume() macht dieselben Uploads spaeter noch
   einmal - dort steht das Bild (Raster-Split aus, DMA aus), deshalb sitzen
   die Farben ab da.
   1 = spr_pal_load() nach N Frames Spielzeit EINMAL wiederholen.
   Springen die Farben dann sichtbar auf richtig um, ist der Erstupload die
   Ursache und der Fix gehoert dorthin (Upload ins VBlank / Anzeige aus).
   Bleiben sie falsch, wird die Palette laufend zerschossen - dann weiter
   bei den OAM-/Palettenschreibzugriffen im Frame. ===== */
#define PAL_RELOAD_TEST 0
#define PAL_RELOAD_FRAME 120u
/* ===== GOD_MODE (21.07.2026, Nutzerwunsch fuer den HW-Test): 1 = Schiff
   unsterblich, SONST nichts. Bewusst getrennt von WORM_TEST_MODE, das
   zusaetzlich das Pause-Gate, die Testwurm-Taste B und eine abweichende
   Scroll-Behandlung mitbringt - fuer einen echten Spieltest will man die nicht.
   Vor der Veroeffentlichung auf 0. ===== */
#define GOD_MODE 1   /* 24.07.2026: dauerhaft unsterblich (Benchmark-Build) */
/* 24.07.2026 Benchmark-Build: Gegner sterben NICHT durch Spielerschuss/-kontakt
   (Kollision laeuft weiter -> Kosten bleiben messbar, aber die Szene bleibt
   stabil). Zusammen mit GOD_MODE + WARP_CHECKPOINT ergibt das eine feste
   Benchmark-Szene. Auf 0 = normales Sterben. */
#define BENCH_NOKILL 0    /* 25.07.2026 aus: Gegner/Wuermer sterben normal, Scroll-Hold aus */
/* ===== A/B-Test-ROMs Performance (29.07.2026, Nutzerauftrag): vier Builds mit
   IDENTISCHER Szene (BENCH_DETERM + BENCH_NOKILL + BENCH_FIRE + Waffen), je ein
   Hebel. Wirkung NUR auf echter Hardware messbar (VBC-Anzeige, ungedeckelt).
   Alle vier Schalter vor der Veroeffentlichung auf 0. ===== */
/* Dauerfeuer ohne Eingabe: A gilt jeden Frame als gehalten -> konstanter
   Schuss-Strom, die Szene ist ohne Zutun reproduzierbar. */
#define BENCH_FIRE 0
/* ROM A: ALLE Spieler-Schuesse zurueck aufs Flacker-Multiplexing.
   HW-MESSUNG 29.07.2026: Basis 95-107, Flicker 105 -> KEIN GEWINN. Der Hebel
   ist tot und traegt obendrein die "unsichtbare Schuesse nach Reload"-Klasse
   (Alloc/Free-Churn). NICHT wieder versuchen - bleibt nur als Messbuild-
   Schalter dokumentiert. */
#define TEST_FLICKER_ALLBULLETS 0
/* Block 4 (check_collisions + wallworms_collide + wcrawls_collide) nur jeden
   ZWEITEN Frame. STANDARD AN seit 29.07.2026 - zusammen mit der
   Strecken-Pruefung (BULLET_SWEEP: Schuss-Box um den 16px-Zwei-Frame-Weg nach
   unten verlaengert, Waffen-Schuss-Box vs. Wuermer ±4 ringsum), sonst tunneln
   schnelle Schuesse durch schmale Ziele.
   HW-MESSUNG 29.07.2026 (Durchschnitt ueber 30 Fenster, gleiche Szene):
   Basis 95 - B allein 92 - C allein 94 - B+C 90 - B+C mit YFILTER 48: 88. */
#define TEST_COLL_HALF 1
/* Schuss-Beweger (Hauptkanone, Blumen-/Map-Objekt-Schuesse, Gegner-Schuesse)
   im 2-Frame-Takt mit DOPPELTEM Schritt - identische Flugbahn, halbe
   Update-Rechnung, und auf Standframes unterdrueckt der OAM-Dirty-Check die
   Positions-Schreibzugriffe. HW-MESSUNG 29.07.2026 (Ø): allein 95 -> 94,
   zusammen mit TEST_COLL_HALF 95 -> 88.
   SEIT 29.07. STANDARD AN - mit Mittelpunkt-Tests in bullets_update
   (Map-Objekt-Zellen sind 8 px hoch, das Bossauge 16 - ein reiner
   16px-Endpunkt-Test wuerde sie ueberspringen). Die Waffen-Schuesse
   (weapon_update) bleiben im 1-Frame-Takt - dort haengt das FEUERN mit im
   selben Durchlauf. Sichtbar: Hauptkanone springt 16 px je 2 Frames
   (fiel auf Hardware im Messlauf nicht negativ auf; bei Bedarf auf 0). */
#define TEST_BULLET_UPDATE_HALF 1
/* ===== Animations-Messung (29.07.2026): kosten die Map-Animationen ueberhaupt
   messbar Zeit? Erst die OBERGRENZE bestimmen (alles aus), dann erst ueber
   Tempi reden - sonst aendert man Grafik fuer nichts.
     1 = anim_update() komplett uebersprungen (Terrain steht still)
     2 = alle Anim-Tempi VERDOPPELT (halb so viele Bildwechsel) */
#define TEST_ANIM_OFF 0
#define TEST_ANIM_SLOW 0
/* GEPRUEFT UND WIRKUNGSLOS (29.07.2026, HW-Messung im Turmband, � je 60 s):
   Referenz 73 - Animationen ganz aus 66 (-7, das Maximum) - Tempo halbiert 70
   (-3). Drei Umbauten brachten dagegen EXAKT NULL und sind wieder ausgebaut:
     * anim_update() ans Frame-Ende verschoben          -> 73 (�0)
     * Zellen ausserhalb des Bildschirms ueberspringen  -> 73 (�0)
     * Schleifenbedingungen aus dem RAM statt aus dem ROM -> 73 (�0)
   Die Kosten haengen also ALLEIN an der Zahl der Bildwechsel (Tempo), nicht am
   Zeitpunkt, nicht an unsichtbaren Zellen und nicht an ROM-Wartezyklen. Die
   beiden Messschalter unten bleiben als Werkzeug erhalten. */
/* Warp-Ziel fuer die Zonen-Messung: 0 = aus, sonst Map-Zeile (respawn_do). */
#define TEST_WARP_ROW 0
#if TEST_COLL_HALF || TEST_BULLET_UPDATE_HALF
static u8 g_testpar;   /* Frame-Paritaet der 2-Frame-Takte (Toggle in der Hauptschleife) */
#endif
/* 24.07.2026 Benchmark: Scroll im Wurm-Band anhalten (Zeile), damit man stehen
   bleibt und sich Wandwuermer (unsterblich) auf's Maximum sammeln = Worst-Case-
   Szene. Auf 0/gross setzen zum Deaktivieren. */
#if BENCH_DETERM_WORM
#define BENCH_HOLD_ROW 113u  /* IM Wandwurm-Band (92-130) einfrieren */
#elif BENCH_DETERM
#define BENCH_HOLD_ROW 12u   /* frueh + ruhig (weit vor dem Wurm-Band 92-130): Szene friert schnell ein, keine Wandwuermer */
#else
#define BENCH_HOLD_ROW 113u
#endif

/* ===== UNCAP_FPS (25.07.2026): 1 = der feste 30-fps-Deckel wird auf 60 fps
   angehoben (ein VBlank Mindestdauer statt zwei). NUR zum Messen.
   Grund: der 30er-Deckel entspricht 60 VBlanks je 30 Frames. Jeder Block, der
   genug spart, um darunter zu kommen, zeigt deshalb exakt 060 - egal ob er 10
   oder 40 VBlanks kostet. Nutzermessung im Einbruch: Referenz 70-77, Zeichnen
   aus 060, Kollisionen aus 060. Beide sind also gross, aber welcher groesser
   ist, verdeckt der Deckel. Ohne ihn stehen echte Zahlen da.
   Nebenwirkung: das Spiel laeuft bis zu doppelt so schnell - zum Spielen
   untauglich, zum Messen genau richtig. Vor der Veroeffentlichung auf 0. ===== */
#define UNCAP_FPS 0
/* ===== BENCH_NOWAIT (30.07.2026) - NUR fuer die Zyklusmessung im Emulator =====
   1 = die Hauptschleife wartet GAR NICHT mehr auf VBlanks. Das Bild ist danach
   unbrauchbar, das ist gewollt: solange die Schleife wartet, verbrennt sie
   Zyklen und ueberdeckt genau das, was man messen will.
   Ohne das Warten gilt: Zyklen je Spielframe = reine Rechenarbeit. In
   Verbindung mit PROF_FIXED laesst sich damit die Kostenverteilung im
   EMULATOR bestimmen, ohne dass jemand zehn ROMs auf Hardware durchspielt.
   Spielframes sind ueber den Scroll zaehlbar (1 px je SCROLL_TICK_THRESHOLD
   Frames), siehe tools/probe_cycleload.py.
   GRENZE: der Emulator kennt keine Wartezyklen des Bausteins, die ABSOLUTE
   Zeit stimmt also nicht (CLAUDE.md: gemessen wird auf Hardware). Die
   VERTEILUNG zwischen den Bloecken stimmt - und die reicht, um den
   Verdaechtigen zu finden. Vor Veroeffentlichung: 0. */
#define BENCH_NOWAIT 0

static u8 g_pad, g_pad_prev, g_pad_pressed;
/* 29.07.2026: EINEN Block fest stilllegen, ohne PROFILE_MODE einzuschalten.
   Fuer Diagnose-ROMs, bei denen der Nutzer nur eine Zahl ablesen soll - die
   Blocknummer steht im Dateinamen, nicht auf dem Bildschirm. Wichtig: mit
   PROFILE_MODE zeigt die HUD-Anzeige das Profiler-Layout OHNE Durchschnitt
   ([fps][Block][VBC]) - genau deshalb laeuft das hier getrennt, damit die
   Anzeige dieselbe bleibt wie in allen anderen Mess-ROMs. 0 = alles an. */
#define PROF_FIXED 0
#if PROF_FIXED
#undef PROF_OFF
#endif
#if PROF_FIXED
#define PROF_OFF(n) ((u8)(n) == (u8)PROF_FIXED)   /* Diagnose-ROM, siehe PROF_FIXED */
#elif PROFILE_MODE
/* 0 = nichts stillgelegt, 1..8 = dieser Block wird uebersprungen. Siehe
   PROFILE_MODE fuer die Zuordnung und die Tastenbelegung. */
static u8 g_prof_sel;
#define PROF_OFF(n) (g_prof_sel == (u8)(n))
#else
#define PROF_OFF(n) 0
#endif
#if PROFILE_MODE || FPS_VBC_DISPLAY
/* Merker fuer den "hat sich nichts geaendert"-Schnellausstieg in score_draw:
   die VBlank-Zahl aendert sich auch dann, wenn Punktestand und FPS stehen. */
static u16 g_prof_last_vbc; static u8 g_prof_last_sel;
#endif
#if WORM_TEST_MODE
static u8 g_paused;   /* Testphase: Pause-Toggle per OPTION-Taste */
static u8 g_cur_wave; /* Testphase: zuletzt getriggerte Welle (Spawn-Index) fuer HUD */
#endif

static void input_update(void) {
    g_pad_prev    = g_pad;
    g_pad         = JOYPAD;
    g_pad_pressed = g_pad & (u8)(~g_pad_prev);
}

// --- Hilfsfunktionen --
static u8 collide(u8 ax, u8 ay, u8 bx, u8 by, u8 r) {
    u8 dx = (ax > bx) ? (u8)(ax - bx) : (u8)(bx - ax);
    u8 dy = (ay > by) ? (u8)(ay - by) : (u8)(by - ay);
    return (u8)(dx < r && dy < r);
}

/* ============ Hitzonen (Schritt 16, Nutzerkorrektur 10.07.2026) ============
   Bisher nie implementiert ??? Schiff/Gegner-Treffer liefen ausschliesslich
   ueber die generische Punkt+Radius-Funktion collide() oben. lvl_hitzone_*
   definiert PRAEZISERE, meist kleinere Trefferzonen je Sprite-Referenz
   (gleiches spr-Encoding wie lvl_spawn_spr: 0x1000|meta fuers Schiff je
   Kippstufe, 0xC000|n/0x8000|n fuer Gegner-Sprite-Streifen ??? siehe
   ANWEISUNG_Export_Integration.md Schritt 16). Alle 17 aktuellen Eintraege
   haben count=1 (ein Rechteck) ??? deshalb hier bewusst EIN Rechteck pro
   Referenz statt der in der Anweisung vorgesehenen Mehrfach-Rechteck-Schleife;
   liefert ein kuenftiger Export count>1, wird nur das erste Rechteck
   verwendet (dann nachbessern). */
/* Nur die RELATIVE Hitzone (dx/dy/w/h, ohne Ursprungsposition) aufloesen ???
   Performance-Fix 11.07.2026: der eigentliche 17-Eintraege-Suchlauf lebt
   jetzt hier, einmal aufrufbar OHNE Positionsbezug, damit er beim Spawn
   einmalig gecacht werden kann (siehe TEnemy.hz_dx/hit_test_cached) statt
   bei jedem Kollisionstest neu zu laufen. spr=0 (Projektile, siehe
   check_collisions/weapon_update) hat garantiert keinen Treffer (alle 17
   Eintraege haben Bit 12/13/15 gesetzt) ??? direkter Fallback ohne Suche. */
static void hitzone_resolve(u16 spr, u8 fallback_w, u8 fallback_h,
                             s16 *dx, s16 *dy, u8 *w, u8 *h) {
    u8 i;
    if (spr != 0u) {
        for (i = 0u; i < (u8)LVL_HITZONE_COUNT; i++) {
            if (lvl_hitzone_spr[i] == spr) {
                u16 off = lvl_hitzone_off[i];
                *dx = lvl_hitzone_dx[off];
                *dy = lvl_hitzone_dy[off];
                *w  = lvl_hitzone_w[off];
                *h  = lvl_hitzone_h[off];
                return;
            }
        }
    }
    /* Keine Hitzone definiert -> volle Sprite-/Metasprite-Flaeche (Fallback,
       wie in der ANWEISUNG gefordert). */
    *dx = 0; *dy = 0;
    *w = fallback_w; *h = fallback_h;
}

static u8 rects_overlap(s16 ax, s16 ay, u8 aw, u8 ah, s16 bx, s16 by, u8 bw, u8 bh) {
    if ((s16)(ax + (s16)aw) <= bx || (s16)(bx + (s16)bw) <= ax) return 0u;
    if ((s16)(ay + (s16)ah) <= by || (s16)(by + (s16)bh) <= ay) return 0u;
    return 1u;
}

/* BEIDE Seiten schon aufgeloest ??? Performance-
   Fix 11.07.2026: check_collisions() prueft das Schiff pro Frame gegen bis
   zu MAX_ENEMIES+MAX_METAENEMIES Gegner; die Schiffs-Hitzone haengt nur vom
   Kippstatus ab und aendert sich innerhalb eines einzelnen check_collisions()-
   Aufrufs nie ??? einmal vorher aufloesen (hitzone_rect) und hier nur noch die
   Rechtecke vergleichen, statt bei JEDEM Gegner erneut zu suchen. */
static u8 rects_overlap_cached(s16 arx, s16 ary, u8 arw, u8 arh,
                                u8 bx, u8 by, s16 bdx, s16 bdy, u8 bw, u8 bh) {
    return rects_overlap(arx, ary, arw, arh,
                          (s16)((s16)bx + bdx), (s16)((s16)by + bdy), bw, bh);
}

/* Schiff-Fallback-Flaeche 24x24 (siehe lvl_meta_dx/dy-Kommentar bei
   THRUST_*). ship_hitzone_rect()/ship_hitzone_cache_init() sind weiter unten
   definiert (brauchen tilt_by_level, das erst dort deklariert ist). */
#define PICKUP_GRAB_R 18u   /* Einsammel-Radius Mitte-zu-Mitte, siehe pickups_update */
#define SHIP_HIT_FALLBACK_W 24u
#define SHIP_HIT_FALLBACK_H 24u
/* Generischer Fallback fuer Gegner ohne passende Hitzone: 1 Streifen-Tile (8x8). */
#define ENEMY_HIT_FALLBACK_W 8u
#define ENEMY_HIT_FALLBACK_H 8u
/* Projektile (Schuss/Waffen-Schuss) haben selbst keine Hitzone-Eintraege ???
   als kleine feste Box behandelt (Tile-Groesse), Gegenstelle bleibt praezise. */
#define BULLET_HIT_W 8u
#define BULLET_HIT_H 8u
/* 29.07.2026 Strecken-Pruefung (Anti-Tunneln fuer TEST_COLL_HALF): zwischen
   zwei Kollisionspruefungen legt ein Hauptkanonen-Schuss 16 px zurueck (8 px/
   Frame x 2 bzw. ein 16er-Doppelschritt). Die Schuss-Box wird deshalb bei den
   halbierten Pruefungen um den zurueckgelegten Weg nach UNTEN verlaengert
   (der Schuss fliegt aufwaerts, die Strecke liegt unter der aktuellen
   Position) - ein schmales Ziel zwischen zwei Abtastpunkten wird so trotzdem
   getroffen. HW-Messung 29.07.: Block 4 halbiert = 95-107 -> 83-105 VBC. */
#if TEST_COLL_HALF
#define BULLET_SWEEP 16u
#else
#define BULLET_SWEEP 0u
#endif
/* Y-Distanz-Prefilter (Kollisions-Opt 16.07.2026): liegt ein Gegner vertikal
   weiter als das weg, kann seine Hitzone die Schuss-Box unmoeglich schneiden.
   29.07.2026: BLEIBT BEI 48, auch mit der um BULLET_SWEEP verlaengerten Box.
   Ein zwischenzeitliches 64 war ein Denkfehler - die Verlaengerung wirkt nur
   nach UNTEN (dyv negativ), die OBERE Filtergrenze haengt allein an der
   Gegner-Hitzone. Nachgerechnet an den echten Daten (lvl_hitzone_dy/h):
   groesstes dy+h = 21, unterste Grenze dy-24 = -24 - der 48er-Filter hat nach
   beiden Seiten mehr als doppelte Reserve. Und die 64 waren TEUER: sie liessen
   deutlich mehr Paare in den Rechteck-Test, HW-Messung 29.07. Ø 90 -> 88. */
#define BULLET_YFILTER 48

/* SCR2-Terrain: logische Palette 0-11 -> HW-Slots 0,5,6-15. Die Bar belegt
   Slots 1-4 (cool/warm/life/salmon); Slot 5 (frueher Ziffern) ist seit dem
   Ziffern-Sprite-Umbau frei und wird fuer Terrain mitgenutzt ??? die Map
   braucht seit dem 20x120-Update 12 Paletten. MEHR als 12 passen nicht
   (16 HW-Slots minus 4 Bar) ??? falls ein kuenftiger Export mehr liefert,
   meckert der Groessen-Check in build_lvl1(). */
static const u8 scr2_pal_hw[12] = { 0u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u };
#define SCR2_PAL_N (sizeof(lvl_pal_scr2) / sizeof(lvl_pal_scr2[0]))
#define SCR1_PAL_N (sizeof(lvl_pal_scr1) / sizeof(lvl_pal_scr1[0]))
/* ===== Ersatz-Zuordnung fuer nicht abbildbare SCR2-Paletten (28.07.2026) =====
   Die Map darf bis zu 16 logische SCR2-Paletten benutzen, abbilden koennen wir
   nur die ersten 12 (die HUD-Bar belegt die HW-Slots 1-4). Bis hierher stand an
   allen Zeichenstellen
       a_hw = (a_p < sizeof(scr2_pal_hw)) ? scr2_pal_hw[a_p] : 0u;
   also STILL Palette 0 - und die ist grau/blau. Sichtbar wurde das als
   "abgeschossene Blumen grau statt braun" und "ein Bild der Wurmloch-Animation
   grau" (Nutzerbericht 28.07.).
   Statt 0 nehmen wir jetzt die FARBLICH NAECHSTE der nutzbaren Paletten. Das
   ersetzt den Export nicht - richtig wird es erst, wenn das Tool mit dem neuen
   Paletten-Budget (12) neu exportiert - aber es sieht dann nach "leicht andere
   Farbe" statt nach "kaputt" aus.
   g_digit_pal_hw loest dasselbe Problem fuer die Ziffern auf der HUD-Bar: die
   sind Sprite-Streifen-Kacheln (S37-S46), werden aber als HINTERGRUND gezeichnet
   und brauchen deshalb eine SCR2-Palette mit denselben Farben. Bisher stand dort
   fest scr2_pal_hw[2] - im Export vom 28.07. enthaelt die logische Palette 2 ein
   Braun, daher die "braunen Striche in den Ziffern". Jetzt wird die passende
   Palette einmalig GESUCHT statt geraten. */
static u8 g_scr2_pal_map[16];   /* logische Map-Palette -> HW-Slot, inkl. Ersatz */
static u8 g_digit_pal_hw = 6u;  /* HW-Slot fuer die Bar-Ziffern (Startwert = alter Festwert) */

static u16 pal_dist(const u16 *a, const u16 *b) {
    /* Summe der quadrierten Kanalabstaende ueber die Farben 1..3 (0 ist immer
       transparent/Hintergrund). 12-Bit 0BGR, je Kanal 4 Bit. */
    u16 sum = 0u;
    u8 k;
    for (k = 1u; k < 4u; k++) {
        u8 ar = (u8)(a[k] & 0xFu),        br = (u8)(b[k] & 0xFu);
        u8 ag = (u8)((a[k] >> 4) & 0xFu), bg = (u8)((b[k] >> 4) & 0xFu);
        u8 ab = (u8)((a[k] >> 8) & 0xFu), bb = (u8)((b[k] >> 8) & 0xFu);
        s8 dr = (s8)(ar - br), dg = (s8)(ag - bg), db = (s8)(ab - bb);
        sum = (u16)(sum + (u16)((s16)dr * dr) + (u16)((s16)dg * dg) + (u16)((s16)db * db));
    }
    return sum;
}

static void scr2_pal_map_init(void) {
    u8 p, q, usable = (u8)sizeof(scr2_pal_hw);
    if (usable > (u8)SCR2_PAL_N) usable = (u8)SCR2_PAL_N;
    for (p = 0u; p < 16u; p++) {
        if (p < usable) { g_scr2_pal_map[p] = scr2_pal_hw[p]; continue; }
        if (p >= (u8)SCR2_PAL_N) { g_scr2_pal_map[p] = scr2_pal_hw[0]; continue; }
        { u8 best = 0u; u16 bd = 0xFFFFu;
          for (q = 0u; q < usable; q++) {
              u16 d = pal_dist(lvl_pal_scr2[p], lvl_pal_scr2[q]);
              if (d < bd) { bd = d; best = q; }
          }
          g_scr2_pal_map[p] = scr2_pal_hw[best]; }
    }
    /* Ziffern: die SCR2-Palette suchen, die der Sprite-Palette der Ziffern
       am naechsten kommt (mit dem Export vom 28.07. ist das eine EXAKTE
       Uebereinstimmung, der alte Festwert lag daneben). */
    { u8 dpal = lvl_sspr_a_pal[(u16)(SPR_S_DIGIT0 - 1u)];
      u8 best = 2u; u16 bd = 0xFFFFu;
      if (dpal < (u8)(sizeof(lvl_pal_sprites) / sizeof(lvl_pal_sprites[0]))) {
          for (q = 0u; q < usable; q++) {
              u16 d = pal_dist(lvl_pal_sprites[dpal], lvl_pal_scr2[q]);
              if (d < bd) { bd = d; best = q; }
          }
      }
      g_digit_pal_hw = scr2_pal_hw[best]; }
}
/* Boss-Body-Treffer-Flash-Ziele (siehe Boss-Block + scr_pal_load). SCR2 ist voll
   belegt -> die Body-a-Zellen werden auf die BESTEHENDE, helle Bar-Palette 4
   ("Salmon") umgeschaltet (Farb-Shift, deren Farben bleiben, Bar unberuehrt).
   SCR1-Slot 14 ist frei -> dort eine echte weisse Flash-Palette fuer die b-Ebene. */
#define BOSS_FLASH_SCR2_PAL 4u
#define BOSS_FLASH_SCR1_PAL 14u
/* 28.07.2026: stehen jetzt weiter oben, direkt bei scr2_pal_hw - scr2_pal_map_init()
   braucht SCR2_PAL_N und wird vor dieser Stelle definiert. */

/* ============ Kompakter Eigenfont (statt vollem BIOS-Systemfont) ============
   SysSetSystemFont() (main()) fuellt beim Boot ALLE 96 ASCII-Tiles auf
   Slot 32-127 ??? PrintString() (library.c) adressiert Zeichen-Tiles DIREKT
   ueber den ASCII-Code, ohne Lookup. Fuer Text brauchen wir aber nur 37
   Zeichen (A-Z, 0-9, Leerzeichen ??? GAME OVER/PRESS A/SHOP). Deshalb: einmalig
   die 37 benoetigten Glyphen aus ihren BIOS-Positionen in eine kompakte Zone
   (Slot 32-68) umkopieren, danach zaehlt Slot 69-127 (59 Slots) als frei fuer
   Terrain (siehe g_lvl_vram/lvl1_vram_init). PrintStringCustom() ersetzt
   PrintString() ueberall im Spiel und nutzt font_tile_for_char() als Lookup
   statt der rohen ASCII-Adressierung. */
/* 28.07.2026: Der kompakte 8x8-Font ist ERSATZLOS ENTFALLEN.
   Er war zuletzt nur noch fuer "GAME OVER"/"PRESS A" da und belegte dafuer
   dauerhaft die VRAM-Kacheln 32-41 - bei einem Adressraum, der bis auf die
   letzte Kachel vergeben ist. Der Game-Over-Bildschirm schreibt den Text jetzt
   im 16x16-Intro-Alphabet auf schwarzen Grund (siehe STATE_OVER); dort ist der
   VRAM ohnehin frei, weil weder Terrain noch Sprites zu sehen sind.
   Entfallen sind damit: FONT_BASE/FONT_CHAR_COUNT, font_charset,
   font_compact_init(), font_tile_for_char(), PrintStringCustom() und der
   SysSetSystemFont()-Aufruf in main(). Die Kacheln 32-41 gehen an den
   Sprite-Pool (spr_tile_vram_init).
   WER WIEDER 8x8-TEXT BRAUCHT: entweder den Shop-Font (lvl_shop_font_*, liegt
   im ROM und wird beim Shop ohnehin geladen) oder das Intro-Alphabet - NICHT
   den BIOS-Font, der steht nach dem Wegfall von SysSetSystemFont() nicht mehr
   im VRAM. */

// --- Level-1 Terrain VRAM installieren --
/* start_idx (Nutzerwunsch 11.07.2026, "dynamischer Tile-Austausch"): die
   ersten SHARED_PREFIX_COUNT Eintraege in lvl_tile_used_A/B sind bewusst
   IDENTISCH (siehe dortigen Kommentar) und liegen dadurch in JEDEM Segment
   auf denselben physischen VRAM-Slots (g_lvl_vram[j] ist segmentunabhaengig)
   ??? beim Umschalten muss dieser gemeinsame Block nicht neu hochgeladen
   werden, nur der segmentspezifische Rest. Initialer Aufbau (leeres VRAM)
   braucht weiterhin start_idx=0, siehe Aufrufer. */
static void build_lvl1(u16 start_idx) {
    volatile u16 *p;
    volatile u16 *dst;
    const u16 *src;
    u16 w, j;
    u8 i, slot;

    scr2_pal_map_init();   /* 28.07.2026: Ersatz-Zuordnung + Ziffernpalette bestimmen */
    /* SCR2-Terrain-Paletten: lvl_pal_scr2 -> HW-Slots laut scr2_pal_hw.
       Groessen-Check: liefert ein Export mehr Paletten als die Tabelle
       Slots hat, werden die ueberzaehligen NICHT geladen (Tiles damit
       sichtbar falsch -> faellt beim Testen sofort auf statt still
       irgendeinen Slot zu ueberschreiben). */
    p = SCROLL_2_PALETTE;
    for (i = 0u; i < (u8)SCR2_PAL_N && i < (u8)sizeof(scr2_pal_hw); i++) {
        slot = scr2_pal_hw[i];
        p[(u16)slot * 4u + 0u] = 0x0000u;
        p[(u16)slot * 4u + 1u] = lvl_pal_scr2[i][1];
        p[(u16)slot * 4u + 2u] = lvl_pal_scr2[i][2];
        p[(u16)slot * 4u + 3u] = lvl_pal_scr2[i][3];
    }
    /* SCR1-Terrain-Paletten: direkt Slots 0..N-1 (Export liefert aktuell 5;
       Slot 15 = STAR_PAL bleibt reserviert) */
    p = SCROLL_1_PALETTE;
    for (i = 0u; i < (u8)SCR1_PAL_N; i++) {
        p[(u16)i * 4u + 0u] = 0x0000u;
        p[(u16)i * 4u + 1u] = lvl_pal_scr1[i][1];
        p[(u16)i * 4u + 2u] = lvl_pal_scr1[i][2];
        p[(u16)i * 4u + 3u] = lvl_pal_scr1[i][3];
    }
    /* 23.07.2026: Boss-Body-Treffer-Flash. SCR2 ist voll belegt (Terrain 0,5..15 +
       Bar 1..4) - es gibt KEINEN freien SCR2-Slot fuer eine eigene weisse Palette
       (ein erster Versuch, Slot 4 weiss zu ueberschreiben, faerbte Bar-Zellen weiss).
       Loesung: der Body-Flash SCHALTET die Body-Zellen auf eine BESTEHENDE, kontrast-
       reiche SCR2-Palette um (Slot 4 "Salmon", hell) - deren Farben bleiben unveraendert,
       die Bar ist also unberuehrt (nur die Palettennummer der Body-Zellen wechselt =
       Farb-Shift = Flash). Fuer die SCR1-b-Ebene ist Slot 14 im Export frei -> dort
       eine echte WEISSE Flash-Palette (harmlos, kein anderer nutzt SCR1-Slot 14). */
    { volatile u16 *ps1 = SCROLL_1_PALETTE;
      ps1[(u16)BOSS_FLASH_SCR1_PAL * 4u + 1u] = 0x0FFFu;
      ps1[(u16)BOSS_FLASH_SCR1_PAL * 4u + 2u] = 0x0FFFu;
      ps1[(u16)BOSS_FLASH_SCR1_PAL * 4u + 3u] = 0x0FFFu; }
    /* Tile-Daten (kompaktiert): nur die Tiles des AKTIVEN Segments
       (lvl1_select_segment() vorher aufgerufen) nach VRAM kopieren, Ziel-
       Slots ueber die g_lvl_vram-Indirection (mehrteilige Zone, siehe oben). */
    for (j = start_idx; j < g_lvl_tile_used_count; j++) {
        dst = (volatile u16*)0xA000u + (u16)g_lvl_vram[j] * 8u;
        src = &lvl_tile_data[g_lvl_tile_used[j]][0];
        for (w = 0u; w < 8u; w++) dst[w] = src[w];
    }
}

/* ============ Einheitlicher Sprite-Tile-Pool ============
   Schiff (S1-S35), Schuss (S36), Ziffern (S37-S46) und Gegner (S47-S54,
   S100-S107) kommen alle aus demselben Tile-Analyzer-Sprite-Streifen
   (lvl_sspr_*, lvl_meta_*) ??? ein Tile-/Paletten-Upload statt getrennter
   Systeme. Die dafuer benoetigten rohen Tile-Indizes sind NICHT zusammen-
   haengend (der Export enthaelt zusaetzlich aktuell unbenutzte Deko-/
   Belohnungs-Sprites) ??? spr_raw_used/spr_raw_remap kompaktieren wie beim
   Terrain, KOMPLETT aus map.h neu berechnet bei jedem Update (nicht nur
   ergaenzt ??? verschobene Rohindizes bestehender S-Nummern sind sonst NUR als
   falsch dargestelltes Sprite sichtbar, NIE als Compiler-Fehler; Historie:
   S99-Bug 07.07., Segment-Neuberechnungen 10./11.07.). Map-Update 12.07.2026:
   Wurm-Rotationssaetze (Schritt 12) kamen als neue Referenzquelle dazu (12
   Rohindizes, vorher von keinem erreichbaren S-Sprite gebraucht) ??? 138->150
   von jetzt 150 zugeteilten Slots belegt, siehe SPR_TILE_COUNT-Kommentar. */
#define SPR_RAW_REMAP_SIZE 527u   /* max(spr_raw_used) - SPR_ROM_BASE + 1 */
/* Map-Update 13.07.2026: KOMPLETT aus dem neuen map.h neu berechnet, nicht nur
   ergaenzt (siehe Kommentar-Historie oben, S99-Bug 07.07. etc.) ??? der Tool-
   Export hat diesmal 5 rohe Tiles UND 8 Sprite-Streifen-Eintraege geloescht
   (LVL_TILE_DATA_COUNT 503->498, LVL_SSPR_COUNT 158->150), betraf aber
   ausschliesslich den reinen Sprite-Anteil (Terrain-Rohindizes unveraendert,
   siehe lvl_tile_used_A/B). Ausschliesslich zur Laufzeit tatsaechlich
   erreichbare S-Nummern/Rohindizes aufgenommen: Schiff 1-35 (a+b), Ziffern
   37-46 (a), Gegner-Spawn-Koepfe S55/S100 (a+b, 17+8 Frames), Metasprite-
   Zellen S80-85 (a), Blumen-Schuss S99 (a), Waffen-Schuss S143 (a),
   Feuerstufen-Schuss S143-145 (a), Waffen-Modul-Kopf S138-142 (a+b, 5
   Frames ??? S138 ist zugleich Gruppen-Belohnung 4/5-Icon, siehe
   lvl_group_reward_spr), Schubduesen-Kopf S149/150 (a, siehe
   SPR_S_THRUST_HEAD-Kommentar), Gruppen-Belohnungs-Icons S108/S115 (a+b),
   Wurm-Rotset 0 Rohtiles 427/429/430 (rot 1 aktuell von keinem Spawn
   referenziert, bewusst nicht eingebunden, wie schon Kopf 47/72 vorher).
   135 von 135 belegten Slots, dadurch 15 weniger als die vorherigen 150 ???
   die dadurch frei werdenden physischen Adressen ergeben sich automatisch
   ueber die Abbruchbedingung `i < SPR_TILE_COUNT` in spr_tile_vram_init(),
   OHNE dass die dortigen Adressbereiche angepasst werden muessen. Bekannte
   kleine kosmetische Einschraenkung: S142 (Waffen-Modul letztes Frame) und
   S150 (Schubduesen zweites Frame) referenzieren neu rohes Tile 0 (echtes
   Leer-Bild laut Export) ??? 0 liegt unterhalb SPR_ROM_BASE und ist bewusst
   NICHT im Sprite-Pool, spr_vram()s bestehender Bounds-Check faengt das ab
   (kein Absturz), zeigt in diesem einen Animationsframe aber ein beliebiges
   echtes Sprite statt wirklich leer ??? selten sichtbar, nicht weiter verfolgt. */
static const u16 spr_raw_used[SPR_TILE_COUNT] = {
    345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356,
    357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368,
    369, 370, 371, 372, 373, 374, 375, 376, 377, 378, 379, 380,
    381, 382, 383, 384, 385, 386, 387, 388, 389, 390, 391, 392,
    393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404,
    405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416,
    417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428,
    429, 430, 431, 432, 433, 434, 435, 436, 437, 438, 439, 440,
    441, 442, 443, 444, 445, 446, 447, 448, 449, 450, 451, 452,
    453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464,
    465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 479, 481,
    482, 483, 485, 486, 487, 488, 489, 490, 491, 494, 495, 496,
    497, 498, 499, 500, 501, 502, 506, 519, 520, 529, 540, 541,
    542, 543, 544, 545, 546, 547, 548, 549, 550, 551, 555, 556,
    557, 558, 561, 562, 565, 566, 569, 570, 571, 573, 576, 577,
    578, 580, 582, 583, 586, 587, 590, 591, 592, 593, 595, 596,
    597, 599, 601, 602, 603, 604, 605, 606, 607, 608, 609, 610,
    611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622,
    623, 624, 625, 626, 627, 628, 629, 630, 631, 632, 633, 634,
    635, 636, 637, 638, 639, 640, 641, 642, 643, 644, 645, 646,
    647, 648, 649, 650, 651, 652, 653, 654, 655, 656, 657, 658,
    659, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670,
    671, 672, 673, 674, 675, 676, 677, 678, 679, 682, 683, 684,
    685, 686, 687, 688, 689, 690, 691, 692, 693, 696, 697, 698,
    699, 700, 701, 702, 703, 704, 705, 706, 707, 710, 711, 712,
    713, 714, 715, 716, 717, 718, 719, 720, 721, 722, 723, 724,
    725, 726, 727, 728, 729, 730, 731, 732, 733, 734, 735, 736,
    737, 738, 739, 740, 741, 742, 743, 744,
};

static const u16 spr_raw_remap[SPR_RAW_REMAP_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
    65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
    81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96,
    97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,
    113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
    129, 0, 0, 0, 0, 130, 0, 131, 132, 133, 0, 134, 135, 136, 137, 138,
    139, 140, 0, 0, 141, 142, 143, 144, 145, 146, 147, 148, 149, 0, 0, 0,
    150, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 151, 152, 0,
    0, 0, 0, 0, 0, 0, 0, 153, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 0, 0,
    0, 166, 167, 168, 169, 0, 0, 170, 171, 0, 0, 172, 173, 0, 0, 174,
    175, 176, 0, 177, 0, 0, 178, 179, 180, 0, 181, 0, 182, 183, 0, 0,
    184, 185, 0, 0, 186, 187, 188, 189, 0, 190, 191, 192, 0, 193, 0, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210,
    211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226,
    227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242,
    243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258,
    259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 0, 0,
    273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 0, 0, 285, 286,
    287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 0, 0, 297, 298, 299, 300,
    301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316,
    317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331,
};




























/* 30.07.2026: Groesse jetzt SPR_SEC_ZONE statt SPR_TILE_COUNT. Mit dem
   abschnittsweisen Pool liegen nie mehr als SPR_SEC_ZONE (250) Sprite-Kacheln
   gleichzeitig im VRAM - die restlichen Adressen des alten Bereichs bleiben
   ungenutzt und stehen fuer die Terrain-Zone bereit. */
static u16 g_spr_tile_vram[SPR_SEC_ZONE];

/* --- aktiver Sprite-Abschnitt (Muster: g_terr_sec/g_terr_target/terr_sec_step) --- */
static u8  g_spr_sec;        /* aktiver Abschnitt */
static u8  g_spr_target;     /* Zielabschnitt waehrend eines Wechsels */
static u16 g_spr_up_idx;     /* naechster Slot beim schrittweisen Upload */
static const u16 *g_spr_used;   /* Slot -> Rohkachel des aktiven Abschnitts */
static const u16 *g_spr_remap;  /* Rohkachel-SPR_SEC_ROM_BASE -> Slot */

static void spr_tile_vram_init(void) {
    u16 i = 0u, t;
    for (t = 128u; t <= 134u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    for (t = 137u; t <= 143u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    for (t = 177u; t <= 242u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    for (t = 454u; t <= 501u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    for (t = 502u; t <= 511u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    /* Map-Update 12.07.2026 (Wurm-Rotationssaetze, 12 neue Tiles). ZWEI
       Fehlversuche an diesem Tag, bevor der Bereich hier wirklich frei war:
       1) 355-366 GEHOERT dem Bar-Shift-System (bar_shift_vram_init():
          LVL1_VRAM_BASE+LVL1_FIXED_ZONE_COUNT = 254+101 = 355, belegt
          355-453 komplett fuer die 126 HUD-Bar-Subpixel-Tiles) ??? sichtbar
          als "Lifebar dreht durch".
       2) 148-159 GEHOERT dem TILE_BAR_BASE-Bereich (148-161, 14 Tiles) ???
          InstallTileSetAt(gfxBar, ..., TILE_BAR_BASE) in bar_shift_build(),
          ein FUENFTER Schreiber, den ein Grep nach "0xA000u +" NICHT findet
          (laeuft ueber die library.c-Hilfsfunktion, kein direkter
          Pointer-Ausdruck in xenon.c) ??? sichtbar als "Schuss-/Duesen-Tiles
          falsch", weil genau diese S-Nummern (raw 494/495/499-502) durch die
          Neusortierung von spr_raw_used zufaellig auf die letzten 12
          kompaktierten Plaetze rutschten und dadurch DIESEN Bereich trafen.
       Ein vollstaendiger Node-Audit aller bekannten VRAM-Bereiche (Font,
       Terrain-Zone, bisheriger Sprite-Pool, Bar-Shift, Bar-Kanonisch) zeigt:
       32-511 ist LUECKENLOS komplett belegt (bis auf den bekannten, in der
       Praxis nie geschriebenen Terrain/Sprite-Ueberlapp bei 506-511) ??? es
       gibt dort KEINEN einzigen freien Slot mehr. Einzige echte Luecke im
       GESAMTEN 0xA000-Adressraum: **0-31** (von SysSetSystemFont/
       font_compact_init/Terrain/Sprite/Bar-Shift/Bar-Kanonisch nachweislich
       nie referenziert ??? kein InstallTileSet(...,Offset=0)-Aufruf, kein
       ClearTileRam() im gesamten Code). Tile 0 bewusst frei gelassen
       (verbreitete Konvention: "Blank"-Tile). Map-Update 13.07.2026: der
       Sprite-Pool-Bedarf schrumpfte von 150 auf 135 Slots (siehe SPR_TILE_COUNT-
       Kommentar) ??? die ersten vier Bereiche oben (128-134/137-143/177-242/
       454-501) plus ein Teil von 502-511 decken das jetzt schon komplett ab,
       die Schleife unten (1-12, urspruenglich fuer den Wurm reserviert) wird
       dadurch gar nicht mehr erreicht (die Abbruchbedingung greift vorher) ???
       1-12 UND der Rest von 502-511 sind aktuell einfach ungenutzter Puffer,
       KEIN Code-Aenderungsbedarf (die Zuordnung roher Index -> physischer Slot
       ist rein positionsbasiert ueber spr_raw_remap/g_spr_tile_vram, nicht an
       bestimmte Adressen gebunden).
       **Bei kuenftigen Erweiterungen**: NICHT nur nach "0xA000u +" grep(pen)
       ??? auch nach InstallTileSetAt()/InstallTileSet()-Aufrufen suchen, das
       ist ein eigener, leicht uebersehener Schreibpfad. */
    for (t = 1u; t <= 12u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    /* Map-Update 15.07.2026: SPR_TILE_COUNT 135->166 (11-Waffen-System + Rotset
       1 fuer den Wandwurm) ueberschreitet die bisherigen 150 moeglichen Slots
       (138 feste Zonen + 1-12) -- 13-31 ist laut VRAM-Budget-Audit (Abschnitt 3
       CLAUDE.md) der einzige verbleibende freie Bereich im gesamten 0-511
       Adressraum (0 bleibt bewusst Blank-Tile) und wird hier zusaetzlich
       angehaengt, exakt gleiches Prinzip wie 1-12 oben. */
    for (t = 13u; t <= 31u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    /* 2026-07-18: bar_shift entfernt (MicroDMA-Split) -> dessen 126 Slots frei fuer
       den Sprite-Pool. Reihenfolge egal (Zuordnung rein positionsbasiert). Deckt
       den Mehrbedarf der neuen Map (175 statt 166). */
    for (t = 355u; t <= 453u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    for (t = 162u; t <= 176u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    for (t = 243u; t <= 252u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    for (t = 135u; t <= 136u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    /* 21.07.2026: Terrain-Zone von 171 auf 140 Slots verkleinert (Segment A braucht
       nur noch 134, Segment B 130). Die dadurch frei gewordenen Adressen 330-354
       gehen an den Sprite-Pool. lvl1_vram_init() fuellt wegen `i < LVL1_ZONE_MAX`
       automatisch nur noch bis 329, die dortigen Bereiche bleiben unangetastet. */
    /* 28.07.2026: von 330 auf 327 vorgezogen. Die Terrain-Zone endet mit
       LVL1_ZONE_MAX 147 bei VRAM-Kachel 326 (10 + 59 + 4 + 1 feste Bereiche,
       dann 254..326) - 327-329 lagen dadurch brach, waehrend dem Sprite-Pool
       genau drei Slots fehlten. Die Untergrenze haengt an LVL1_ZONE_MAX: waechst
       die Terrain-Zone weiter, MUSS sie wieder hoch (check_vram_budget.mjs
       meldet die Ueberlappung sofort). */
    /* 28.07.2026: von 327 auf 338 verschoben. Die Terrain-Zone waechst auf 158
       Slots (LVL1_ZONE_MAX) und endet damit bei VRAM-Kachel 337 - noetig fuer
       die Endboss-Tentakel, deren Engpass-Fenster 158 Kacheln braucht. Der
       Sprite-Pool kommt mit 318 aus, gibt die 11 also her.
       DIE BEIDEN ZAHLEN HAENGEN ZUSAMMEN: waechst LVL1_ZONE_MAX weiter, MUSS
       diese Untergrenze mit (tools/check_vram_budget.mjs meldet sonst die
       Ueberlappung, apply_compaction.py bricht ab). */
    /* 29.07.2026 abends: 338 -> 336. Der Export vom 21:46 braucht 321 Pool-
       Kacheln (Belohnungs-Anim S227/S228 + Anemonen-Projektil S262, je 1);
       die Terrain-Zone braucht real nur noch 177 (Abschnitte 132/108/177/177,
       SECS=0,125,163,235) und endet damit bei Adresse 335 - 336/337 gehen an
       den Pool. DIE ZAHLEN HAENGEN ZUSAMMEN: LVL1_ZONE_MAX 177 <-> Untergrenze
       336 (check_vram_budget meldet sonst Ueberlappung, apply_compaction
       bricht ab). */
    /* 30.07.2026: 336-341 sind an die Terrain-Zone gegangen (LVL1_ZONE_MAX
       177 -> 183). Der Pool faengt hier jetzt bei 342 an: 314 Kacheln Kapazitaet,
       genau so viel wie er OHNE Endboss braucht. */
    for (t = 342u; t <= 354u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
    /* 28.07.2026: die letzte feste Font-Zone (32-41) ist aufgeloest. Der
       kompakte 8x8-Font belegte sie dauerhaft, gebraucht wurde er nur auf dem
       Game-Over-Bildschirm - und der zeigt den Text jetzt im 16x16-Alphabet auf
       schwarzem Grund (siehe STATE_OVER). Damit sind 0..41 komplett Sprite-Pool.
       WER PrintStringCustom() WIEDER BENUTZT, muss die Zone zurueckgeben. */
    /* 29.07.2026: nur noch 32-37. Der Pool brauchte aus diesem Bereich mit
       SPR_TILE_COUNT 318 ohnehin nur sechs Slots (Fuellstand davor: 312);
       38-41 und der komplette 52-68-Block gehen an die TERRAIN-Zone
       (lvl1_vram_init) - die kleinen Tuerme und Anemonen heben deren Bedarf
       auf 185. Die Listen sind damit wieder ueberlappungsfrei, die Kapazitaet
       des Pools ist EXAKT 318: waechst SPR_TILE_COUNT, melden
       check_vram_budget.mjs/apply_compaction.py den Konflikt sofort. */
    for (t = 32u; t <= 37u && i < SPR_SEC_ZONE; t++) g_spr_tile_vram[i++] = t;
}

/* ============ Pfad-Zeiger (Optimierung 22.07.2026) ============
   lvl_path_data hat 92906 Eintraege, der Index geht also ueber 65535 - deshalb
   stand in den Bewegungsschleifen ueberall

       off = lvl_path_off[path] + frame;
       x_fix += lvl_path_data[(u32)off * 2u];
       y_fix += lvl_path_data[(u32)off * 2u + 1u];

   also eine 32-BIT-MULTIPLIKATION samt 32-Bit-Adressrechnung, zweimal pro
   Schritt, pro Gegner, pro Frame - und die Schleife macht bei hoeherem Tempo
   mehrere Schritte je Frame.

   Auf ECHTER HARDWARE gemessen (Nutzer 22.07., Profiler-Durchgang): das
   Stilllegen von enemies/worms/wallworms_update brachte 15-20 -> 30 fps,
   waehrend MicroDMA, Zeichnen, Kollisionen und Scrolling nichts ausmachten.
   Kein einzelner der drei war allein verantwortlich - genau das Bild, das eine
   GEMEINSAME teure Operation erzeugt. Im Emulator faellt sie nicht auf, weil
   der die Adressrechnung praktisch geschenkt bekommt.

   Statt jedes Mal neu zu rechnen, merkt sich jedes Objekt einen Zeiger auf
   seinen naechsten Pfadschritt und schiebt ihn um 2 weiter. Aus Multiplikation
   plus 32-Bit-Index wird ein Zeiger-Inkrement. */
#define PATH_PTR(path, frame) \
    (&lvl_path_data[((u32)((u16)lvl_path_off[(path)] + (u16)(frame))) * 2u])

/* Roher lvl_tile_data-Index -> physischer VRAM-Tile (ueber Kompaktierung).
   Bugfix 10.07.2026, Nacht: 0xFFFF ("Fehler/nicht zugeteilt" laut map.h-Doku)
   tauchte real in einem Tool-Export auf (S80 ??? seit dem Re-Export 11.07.2026
   behoben, S80 exportiert jetzt einen echten Wert). Bounds-Check bleibt als
   Dauerschutz stehen: ohne ihn waere raw_idx-SPR_ROM_BASE ein Zugriff weit
   ausserhalb von spr_raw_remap[] (undefiniertes Verhalten/Absturzgefahr statt
   nur einer fehlenden Grafik), falls ein kuenftiger Export erneut den
   Fehler-Sentinel liefert. Fallback: physischer Slot 0. */
static u16 spr_vram(u16 raw_idx) {
    u16 off = (u16)(raw_idx - (u16)SPR_SEC_ROM_BASE);
    /* 30.07.2026: liest den Remap des AKTIVEN Abschnitts statt der globalen
       Tabelle. g_spr_remap wird in spr_sec_select() gesetzt; steht es noch auf
       0 (vor dem ersten Select), faellt der Zugriff auf Slot 0 zurueck - genau
       wie beim Bounds-Check darunter. */
    if (raw_idx == 0xFFFFu || off >= (u16)SPR_SEC_REMAP_SIZE || !g_spr_remap)
        return g_spr_tile_vram[0];
    return g_spr_tile_vram[g_spr_remap[off]];
}

/* 23.07.2026 (Nutzerbericht "nach dem ersten Shop kein Schuss mehr", auch die
   Grundkanone): shop_resume() laedt hier die Sprite-Kacheln NEU - inklusive der
   Schuss-Kachel. Die 317 Kacheln = ~2500 Woerter passen NICHT in ein VBlank-Fenster,
   und der Upload lief bisher ungebremst mitten in den aktiven Bildaufbau. Auf echter
   Hardware rasen diese VRAM-Schreibzugriffe dann gegen den Anzeigebaustein und
   koennen Kacheln VERFAELSCHEN (§7e/§00.VII: "gruen/weisse Sprites", "nach dem Shop
   ok" - dort lief spr_pal_load() bereits VBlank-synchron, der Kachel-Upload aber
   nicht). Trifft es die Schuss-Kachel, ist der Schuss leer/unsichtbar = "kein Schuss".
   Im Emulator nicht sichtbar (er modelliert die Wartezyklen/Display-Kollision nicht).
   FIX wie bei spr_pal_load(): den Upload in VBlank-grosse Haeppchen schreiben - alle
   Zugriffe landen sicher im VBlank, kein Display-Race. Aufrufer (main-Init nach dem
   Titelbildschirm, shop_resume) laufen mit aktivem VBlank-ISR, wait_vblank() ist dort
   also nicht toedlich (spr_pal_load nutzt es an denselben Stellen). */
#define SPR_UPLOAD_CHUNK 16u   /* Kacheln je VBlank (16 x 8 = 128 Woerter, ~2,5x der bewaehrten spr_pal_load-Menge) */
static void wait_vblank(void);   /* Definition weiter unten (C89-Vorwaertsdeklaration) */
static void spr_tiles_upload(void) {
    volatile u16 *dst;
    const u16 *src;
    u16 i, w;
    /* 30.07.2026: laedt den AKTIVEN Abschnitt (g_spr_used) statt des
       Gesamt-Pools. Aufrufer sind unveraendert der Spielstart und shop_resume -
       beide setzen vorher ueber spr_sec_select() den Abschnitt. */
    for (i = 0u; i < (u16)SPR_SEC_ZONE; i++) {
        if ((i % SPR_UPLOAD_CHUNK) == 0u) wait_vblank();   /* frisches VBlank je Haeppchen */
        dst = (volatile u16*)0xA000u + (u16)g_spr_tile_vram[i] * 8u;
        src = &lvl_tile_data[g_spr_used[i]][0];
        for (w = 0u; w < 8u; w++) dst[w] = src[w];
    }
}


/* ===== Abschnittswechsel des SPRITE-Pools (30.07.2026, Schritt 2) =====
   Baugleich zu terr_sec_begin/terr_sec_step/terr_sec_check - siehe die
   ausfuehrliche Begruendung dort. Kurz: beim Wechsel aendern sich bis zu 54 von
   SPR_SEC_ZONE Kacheln; auf einen Frame gelegt zerreisst so viel VRAM-Verkehr
   das Bild (�7e). Deshalb hoechstens SPR_UP_PER_FRAME Slots pro Frame, nur am
   Frame-ENDE, und die Tabellenzeiger erst NACH dem letzten Slot umschalten.
   Waehrend des Uebergangs bleibt der alte Abschnitt aktiv - das ist sicher,
   weil tools/gen_spr_sections.js nachweist, dass gemeinsame Kacheln in beiden
   Abschnitten auf DEMSELBEN Slot liegen ("kein Offset kippt").
   UNTERSCHIED ZUM TERRAIN: dort reicht Vorlauf, weil Kacheln einfach aus dem
   Bild scrollen. Ein Gegner kann eine Grenze aber UEBERLEBEN - deshalb hat der
   Generator auch Nachlauf (BACK), und die Kacheln des alten Abschnitts bleiben
   liegen, solange sie in der Lebendmenge des neuen stehen. */
#define SPR_UP_PER_FRAME 6u    /* wie TERR_UP_PER_FRAME: 54 Kacheln = ~9 Frames */
#define SPR_SEC_LEAD    12u    /* Zeilen Vorlauf; eine Kachelzeile dauert ~24 Frames */

static void spr_sec_select(u8 sec) {
    if (sec >= (u8)SPR_SEC_COUNT) sec = (u8)(SPR_SEC_COUNT - 1u);
    g_spr_sec    = sec;
    g_spr_target = sec;      /* keinen halben Wechsel stehen lassen (siehe lvl1_select_section) */
    g_spr_up_idx = 0u;
    g_spr_used   = spr_sec_used[sec];
    g_spr_remap  = spr_sec_remap[sec];
}

/* Welcher Abschnitt gehoert zu einer Spielerzeile? (Neustart/Respawn/Shop) */
static u8 spr_sec_for_row(u16 row) {
    u8 i, sec = 0u;
    for (i = 0u; i < (u8)SPR_SEC_COUNT; i++) if (row >= spr_sec_row[i]) sec = i;
    return sec;
}

static void spr_sec_begin(u8 target) {
    if (target == g_spr_sec || target >= (u8)SPR_SEC_COUNT) return;
    g_spr_target = target;
    g_spr_up_idx = 0u;
}

/* Am Frame-Ende aufrufen. Liefert 1, solange ein Wechsel laeuft. */
static u8 spr_sec_step(void) {
    volatile u16 *dst;
    const u16 *src;
    u16 n = 0u, w, j, raw;
    if (g_spr_target == g_spr_sec) return 0u;
    while (g_spr_up_idx < (u16)SPR_SEC_ZONE && n < (u16)SPR_UP_PER_FRAME) {
        j   = g_spr_up_idx++;
        raw = spr_sec_used[g_spr_target][j];
        /* Slot unveraendert? Dann nichts hochladen - das ist der Gewinn der
           fortschreibenden Vergabe (typisch bleiben ~200 von 250 stehen). */
        if (raw == spr_sec_used[g_spr_sec][j]) continue;
        dst = (volatile u16*)0xA000u + (u16)g_spr_tile_vram[j] * 8u;
        src = &lvl_tile_data[raw][0];
        for (w = 0u; w < 8u; w++) dst[w] = src[w];
        n++;
    }
    if (g_spr_up_idx >= (u16)SPR_SEC_ZONE) {
        spr_sec_select(g_spr_target);   /* jetzt erst umschalten */
        return 0u;
    }
    return 1u;
}

/* Beim Zeilenwechsel aufrufen, mit der SPIELERzeile (g_scroll_y>>3) - die
   Abschnittsgrenzen des Generators stammen aus lvl_spawn_row, und die meint
   laut map.h ebenfalls die Spielerposition, nicht die einrollende Zeile. */
static void spr_sec_check(u16 row) {
    u8 nxt;
    if (g_spr_target != g_spr_sec) return;
    nxt = (u8)(g_spr_sec + 1u);
    if (nxt >= (u8)SPR_SEC_COUNT) return;
    if (row + (u16)SPR_SEC_LEAD >= spr_sec_row[nxt]) spr_sec_begin(nxt);
}

/* Auf den Beginn des naechsten VBlank warten.
   Hintergrund (Nutzer-HW 22.07.2026): die Sprite-Paletten kamen auf echter
   Hardware VERFAELSCHT an - Schiff und Gegner gruen/rot/gelb, die Map dagegen
   in Ordnung. Nach dem Shop stimmten die Farben, und genau dort laeuft
   spr_pal_load() ein zweites Mal. Der Unterschied: der Erst-Upload in main()
   laeuft mitten im aktiven Bildaufbau, der im Shop bei stehendem Bild mit
   abgeschaltetem Raster-Split. Schreibzugriffe ins Video-RAM waehrend des
   aktiven Bildaufbaus sind in diesem Projekt schon einmal als Ursache
   aufgefallen (??7e, gruen/weisse Sprites). Die 52 Palettenwoerter passen
   bequem in ein VBlank-Fenster - also einfach darauf warten.
   NICHT im ISR verwenden (dort ist Busy-Warten toedlich, siehe ??3). */
static void wait_vblank(void) {
    u8 v = VBCounter;
    while (VBCounter == v) ;
}

/* 23.07.2026, Nutzerwunsch "beim Treffer kurz die Farbe invertieren": reservierter
   Sprite-Paletten-Slot fuer den Treffer-Flash. lvl_pal_sprites belegt nur 0..12,
   also sind 13..15 frei. Warum ein DEDIZIERTER Slot statt echter Farb-Inversion:
   die Sprite-Paletten sind zwischen Schiff und Gegnern GETEILT - eine Inversion
   der Paletten-INHALTE traf darum auch fremde Sprites (genau daran scheiterte der
   alte Invert-Flash, entfernt 18.07.). Die OAM-Palettenzuordnung (0x8C00) ist
   dagegen PRO SPRITE: das getroffene Objekt zeigt kurz auf diesen Slot, kein
   anderes Sprite aendert sich. */
/* ===== TREFFER-FLASH ueber die HARDWARE-INVERTIERUNG (28.07.2026) =====
   Nutzerwunsch: "alle Treffer, ob Schiff, Gegner oder Endboss, immer die
   Invertfunktion - die kostet keine Paletten". Genau so ist es:
   Bit 7 im 2D-Control-Register 0x8012 (NEG) invertiert das FERTIGE BILD -
   jeder Farbanteil c ^= 0x0F, ueber alle Ebenen, Sprites und den Rand hinweg
   (nachgelesen im Emulator-Kern, core/renderer.py Schritt 8).
   Was das spart: die weisse Sprite-Palette 15 (FLASH_PAL) und die
   SCR1-Palette 14 des Boss-Flashs sind frei - beim Kriecher-Export kollidierte
   Slot 15 bereits mit echter Grafik. Dazu entfaellt die Sicher-/Restore-
   Maschinerie des Boss-Body-Flashs (g_boss_save1/2, zusammen 640 Byte RAM).
   Was man wissen muss: NEG ist GLOBAL. Es gibt keinen "nur dieses Sprite"-
   Invert. Wird gleichzeitig irgendetwas getroffen, blinkt das ganze Bild.
   Deshalb setzt jede Trefferstelle nur g_neg_on, und EINE Stelle am Frame-Ende
   schreibt das Register - sonst wuerde es mehrfach je Frame umgeschaltet. */
#define K2GE_2D_CONTROL (*(volatile u8*)0x8012u)
#define K2GE_NEG_BIT    0x80u
static u8 g_neg_on;    /* setzt jede Trefferstelle, gilt fuer den laufenden Frame */
static u8 g_neg_want;  /* was der naechste VBlank ins Register schreiben soll */
/* Am Frame-Ende aufrufen. Schreibt das Register NICHT selbst: der K2GE rendert
   zeilenweise waehrend die CPU laeuft, ein Schreibzugriff mitten im Bild kippt
   nur den REST des Bildes um (im Emulator nachgestellt: obere Haelfte normal,
   untere invertiert). Gesetzt wird deshalb im VBlank, wie die Split-Tabelle. */
static void neg_flush(void) {
    g_neg_want = g_neg_on;
    g_neg_on   = 0u;
}
#define FLASH_PAL 15u   /* nur noch historisch, wird nicht mehr geschrieben */

static void spr_pal_load(void) {
    u8 i;
    u16 *spal = (u16*)0x8200;
    wait_vblank();   /* siehe oben - Palettenschreibzugriffe nur im VBlank */
    for (i = 0u; i < (u8)(sizeof(lvl_pal_sprites) / sizeof(lvl_pal_sprites[0])); i++) {
        spal[(u16)i * 4u + 0u] = 0x0000u;
        spal[(u16)i * 4u + 1u] = lvl_pal_sprites[i][1];
        spal[(u16)i * 4u + 2u] = lvl_pal_sprites[i][2];
        spal[(u16)i * 4u + 3u] = lvl_pal_sprites[i][3];
    }
    /* Flash-Palette: hell/weiss (Index 0 bleibt transparent). Eine Farbe fuer alle
       Objekte, weil ein Slot nicht die Palette JEDES Objekts invertieren kann;
       weiss liest sich als klares Treffer-Feedback. Drei Werte -> leicht tunebar. */
    /* 28.07.2026, 22:20 (Nutzerwunsch "wieder das Blinken wie davor fuer das
       Schiff"): die weisse Flash-Palette ist zurueck. Sie ist der EINZIGE Weg,
       ein EINZELNES Objekt blinken zu lassen - die Hardware-Invertierung (NEG)
       kippt immer das ganze Bild, es gibt kein Invert-Bit im OAM.
       Aufteilung seitdem: Schiff, Gegner, Metasprites und Wandkriecher ueber
       diese Palette, NUR der ENDBOSS ueber NEG (siehe neg_flush()). */
    spal[(u16)FLASH_PAL * 4u + 0u] = 0x0000u;
    spal[(u16)FLASH_PAL * 4u + 1u] = 0x0FFFu;
    spal[(u16)FLASH_PAL * 4u + 2u] = 0x0FFFu;
    spal[(u16)FLASH_PAL * 4u + 3u] = 0x0FFFu;
}

/* ============ Paletten-Selbstpruefung (22.07.2026) ============
   Die grasgruenen/gelben Sprites im ersten Levelabschnitt treten NUR auf echter
   Hardware auf - im Emulator ist alles sauber, wir koennen den Fehler hier also
   nicht nachstellen. Statt weiter Bauen-Flashen-Schauen im Blindflug liest das
   Spiel sein eigenes Paletten-RAM zurueck und vergleicht es mit der Quelle.
   Damit wird aus "die Farben sehen falsch aus" eine ZAHL, die im HUD steht:

     g_pal_bad   Anzahl abweichender Farbwoerter beim letzten Vergleich
     g_pal_worst groesster je gemessener Wert seit Levelstart

   Auswertung auf Hardware:
     bleibt 0            -> das Paletten-RAM ist in Ordnung, die Ursache liegt
                            woanders (Kacheln, OAM-Palettenzuordnung 0x8C00)
     sofort >0 und fest  -> der EINMALIGE Upload in main() kam beschaedigt an
                            (laeuft ohne VBlank-Sync mitten im Bildaufbau)
     waechst mit der Zeit-> die Palette wird im laufenden Betrieb zerschossen
   PAL_SELFHEAL 1 laedt bei Abweichung zusaetzlich neu - damit laesst sich
   pruefen, ob ein Neuladen die Farben tatsaechlich repariert.

   SPARSAM (Nutzerhinweis 22.07.2026): die erste Fassung verglich JEDEN Frame
   alle 48 Farbwoerter - davon die Haelfte Lesezugriffe ins Video-RAM bei 0x8200,
   die auf echter Hardware waehrend des aktiven Bildaufbaus mit dem Anzeige-
   baustein konkurrieren. Im Emulator kostenlos, auf Hardware brach die Bildrate
   dadurch von 30 auf 15-20 ein. Jetzt wird pro Frame nur EINE Palette geprueft
   (3 Vergleiche); ein voller Durchlauf dauert 16 Frames, gut eine halbe Sekunde.
   Das reicht voellig - ein Palettenfehler verschwindet nicht von selbst. */
static u16 g_pal_bad, g_pal_worst;
static u16 g_pal_heals;   /* wie oft musste nachgeladen werden (siehe PAL_SELFHEAL) */
static u8  g_pal_idx, g_pal_acc;
static void spr_pal_check_step(void) {
    const u16 *spal = (const u16*)0x8200;
    u8 k;
    for (k = 1u; k < 4u; k++)
        if (spal[(u16)g_pal_idx * 4u + k] != lvl_pal_sprites[g_pal_idx][k]) g_pal_acc++;
    g_pal_idx++;
    if (g_pal_idx >= (u8)(sizeof(lvl_pal_sprites) / sizeof(lvl_pal_sprites[0]))) {
        g_pal_idx = 0u;
        g_pal_bad = g_pal_acc;                       /* Ergebnis des Durchlaufs */
        if (g_pal_acc > g_pal_worst) g_pal_worst = g_pal_acc;
        g_pal_acc = 0u;
    }
}

/* Ein Streifen-Sprite (a + optionales b-Overlay, Schritt 6) auf zwei
   benachbarte OAM-Slots zeichnen. S-Nummer ist 1-basiert. Fuer Schiff und
   Gegner (koennen ueberlagern); fuer Schuss/Ziffern siehe spr_draw_s_single. */
static void spr_draw_s(u8 oam, u16 s_num, u8 x, u8 y) {
    u16 n = (u16)(s_num - 1u);
    /* Performance 17.07.2026: SpriteControl(FRONT,0) entfernt (redundant nach
       SetSprite, siehe spr_draw_s2). Betrifft u.a. das Schiff (bis 14 Slots/Frame). */
    SetSprite(oam, spr_vram(lvl_sspr_a_idx[n]), 0, x, y, lvl_sspr_a_pal[n]);
    if (lvl_sspr_b_idx[n] != 0u) {
        SetSprite((u8)(oam + 1u), spr_vram(lvl_sspr_b_idx[n]), 0, x, y, lvl_sspr_b_pal[n]);
    } else {
        UnsetSprite((u8)(oam + 1u));
    }
}

/* Wie spr_draw_s, aber mit explizitem zweiten OAM-Slot statt "oam+1" ??? noetig
   fuer die Gegner-Slot-Paare 43/60, 61/62, 63/32 (Nutzerkorrektur
   14.07.2026, kein Flicker mehr), die aus vorher freien/geliehenen,
   NICHT benachbarten Slots zusammengesetzt sind, siehe g_enemy_oam_slot[]. */
static void spr_draw_s2(u8 oam0, u8 oam1, u16 s_num, u8 x, u8 y) {
    u16 n = (u16)(s_num - 1u);
    /* Performance 17.07.2026: kein SpriteControl(FRONT,0) nach SetSprite ???
       SetSprite setzt Byte1 bereits auf Prioritaet FRONT (24) mit geloeschten
       Flip-Bits, der Aufruf schrieb also exakt denselben Wert (nachgerechnet:
       (0x18|tilebit8) & 0x27 | 0x18 == 0x18|tilebit8). Spart pro Voll-Redraw
       einen modeluebergreifenden Aufruf MIT Read-Modify-Write. Nur die
       geflippten Helfer (spr_draw_s_single_flip, Waffen-Schuss) brauchen ihn. */
    SetSprite(oam0, spr_vram(lvl_sspr_a_idx[n]), 0, x, y, lvl_sspr_a_pal[n]);
    if (lvl_sspr_b_idx[n] != 0u) {
        SetSprite(oam1, spr_vram(lvl_sspr_b_idx[n]), 0, x, y, lvl_sspr_b_pal[n]);
    } else {
        UnsetSprite(oam1);
    }
}

/* Fuer Schuss/Ziffern: nie ueberlagert, genau EIN OAM-Slot pro Aufruf
   (spr_draw_s wuerde faelschlich den benachbarten Slot mit-UnsetSprite()n,
   der bei Schuss/Ziffern schon vom naechsten Sprite belegt ist). */
static void spr_draw_s_single(u8 oam, u16 s_num, u8 x, u8 y) {
    u16 n = (u16)(s_num - 1u);
    /* Performance 17.07.2026: SpriteControl(FRONT,0) entfernt ??? SetSprite setzt
       FRONT+kein-Flip bereits, siehe spr_draw_s2. */
    SetSprite(oam, spr_vram(lvl_sspr_a_idx[n]), 0, x, y, lvl_sspr_a_pal[n]);
}

/* Wie spr_draw_s_single, aber optional geflippt (Schubduesen: rechte Seite ist
   die H-gespiegelte Grafik der linken, kein eigenes Tile; seit Map-Update
   13.07.2026 zusaetzlich optional V-geflippt fuer die aus Tool-Sicht fehlende
   Ruecklauf-Animation, siehe SPR_S_THRUST_HEAD-Kommentar). 'flip' ist die
   fertige SPR_HFLIP/SPR_VFLIP-Maske, KEIN Bool mehr. */
static void spr_draw_s_single_flip(u8 oam, u16 s_num, u8 x, u8 y, u8 flip) {
    u16 n = (u16)(s_num - 1u);
    SetSprite(oam, spr_vram(lvl_sspr_a_idx[n]), 0, x, y, lvl_sspr_a_pal[n]);
    SpriteControl(oam, SPR_FRONT, flip);
}

/* Performance-Fix 11.07.2026 (Anregung aus dem Stargunner-Beispielprojekt,
   src/gfx/ngpc_sprite.c: ngpc_sprite_move() statt ngpc_sprite_set(), wenn
   sich nur die Position aendert ??? "much faster on real hardware than
   re-writing full sprite attrs every frame for every object"): das Schiff
   wird JEDEN Frame neu gezeichnet, aber Tile/Palette pro Zelle aendern sich
   nur, wenn sich die Kippstufe aendert (TILT_STEP_FRAMES=12, meistens viele
   Frames unveraendert) ??? die restliche Zeit reicht eine reine Positions-
   Aktualisierung (SetSpritePosition, 2 Byte) statt SetSprite+SpriteControl
   (mehrere Schreibzugriffe + ein Read-Modify-Write PRO Zelle, x2 fuer
   a+b-Overlay). g_ship_last_meta = 0xFF erzwingt einen vollen Redraw (siehe
   ship_hide() ??? nach dem Verstecken muessen Prioritaets-/Flip-Bits, die
   UnsetSprite() geloescht hat, wieder gesetzt werden, reine Positions-
   Updates wuerden das Schiff sonst unsichtbar lassen). */
static u8 g_ship_last_meta = 0xFFu;

/* Nutzerwunsch 14.07.2026: kurzer Paletten-Umkehr-Flash beim Schiffstreffer,
   zusaetzlich zum bestehenden 90-Frame-Blink oben ??? wiederbelebte Idee aus
   dem alten ship_pals_flash/normal (siehe draw_sprites()-Kommentar weiter
   unten), diesmal aber nur ein paar Frames lang UND datengetrieben ueber die
   tatsaechlich vom aktuellen Kippstufen-Metasprite benutzten Paletten
   (lvl_sspr_a_pal/b_pal der 7 Zellen) statt hart auf Palette 1/2 codiert ???
   haelt sich an die Projekt-Konvention "S-Nummern/Paletten nie hart codieren"
   und bleibt auch nach einem Map-Re-Export korrekt. Bekannter Seiteneffekt
   (vom Nutzer explizit akzeptiert): da Paletten mit Schuss/Ziffern/Gegnern
   geteilt sind, blitzen fuer die paar Frames auch andere Sprites mit, die
   zufaellig dieselbe Palette benutzen. Ausgeloest von player_hit(), aber
   erst in hit_flash_update() (nahe score_draw()) tatsaechlich verarbeitet ???
   tilt_by_level/lvl_meta_off sind an dieser Stelle im File (C89, keine
   Vorwaertsdeklaration fuer file-scope Arrays) noch nicht sichtbar. */
#define HIT_FLASH_FRAMES  5u
#define HIT_FLASH_MAX_PAL 4u
static u8  g_hit_flash;          /* 0 = inaktiv, sonst Frames-Restzeit */
static u8  g_hit_flash_pending;  /* von player_hit() gesetzt */
static u8  g_hit_flash_pal[HIT_FLASH_MAX_PAL];
static u8  g_hit_flash_pal_cnt;
static u16 g_hit_flash_orig[HIT_FLASH_MAX_PAL][4];

/* 24.07.2026 Draw-Cache: die Zell-Offsets (lvl_meta_dx/dy) haengen NUR am meta
   (Kippstufe), nicht an x/y. Auf dem Positions-Fastpath (gleiches meta) also aus
   dem RAM-Cache statt 2x 2D-ROM pro Zelle. Gefuellt beim Vollredraw (meta-Wechsel). */
static u8 g_ship_cell_dx[SHIP_CELLS], g_ship_cell_dy[SHIP_CELLS];
/* 25.07.2026 SPRITE-CHAINING (Nutzerwunsch, HW-fps: Zeichnen = halbe Framezeit).
   K2GE-Semantik laut NGPC_HW_QUICKREF s5 (im Emulatorkern k2ge.py/renderer.py
   nachgelesen): ist das Chain-Bit gesetzt, ist das Positionsbyte ein DELTA zur
   effektiven Position des VORHERGEHENDEN OAM-Eintrags - nicht die Doku-Variante
   "Kachel-Indizes verketten", es gibt also keinen Zwang zu aufeinanderfolgenden
   Kacheln. Die Kette laeuft ueber JEDEN OAM-Eintrag, auch versteckte.

   Fuers Schiff heisst das: Slot 0 haelt die absolute Position, die Slots 1..13
   halten konstante Deltas, die sich nur bei einem Kippstufen-Wechsel aendern.
   Bewegung kostet damit EINEN Positionsschreibzugriff statt 14.

   Zwei Fallen, die hier abgefangen sind:
   - Ein verstecktes b-Overlay (Zelle ohne b) ankert die Kette TROTZDEM mit
     seinen Positionsbytes -> es bekommt Delta (0,0) geschrieben und wird ERST
     DANACH versteckt (UnsetSprite laesst Position und Chain-Bits stehen,
     SpriteControl behaelt Maske 0x27 = Bit1/Bit2).
   - Slot 0 darf NIE ein Chain-Bit tragen, sonst haengt das Schiff an Slot 63. */
static void spr_draw_chain_cell_f(u8 oam, u16 s_num, u8 ddx, u8 ddy, u8 chain_a, u8 flip) {
    u16 n = (u16)(s_num - 1u);
    /* chain_a=0 nur fuer die Ankerzelle: die haelt die ABSOLUTE Position.
       Ihr b-Overlay ist trotzdem verkettet (Delta 0,0) - sonst bliebe es auf
       dem Fastpath, der nur den Anker schreibt, an der alten Stelle stehen. */
    SetSprite(oam, spr_vram(lvl_sspr_a_idx[n]), chain_a, ddx, ddy, lvl_sspr_a_pal[n]);
    if (lvl_sspr_b_idx[n] != 0u) {
        SetSprite((u8)(oam + 1u), spr_vram(lvl_sspr_b_idx[n]), 1u, 0u, 0u, lvl_sspr_b_pal[n]);
    } else {
        /* Positionsbytes (0,0) + Chain setzen, dann verstecken - die Kette
           laeuft ueber diesen Slot hinweg weiter. */
        SetSprite((u8)(oam + 1u), 0u, 1u, 0u, 0u, 0u);
        UnsetSprite((u8)(oam + 1u));
    }
    /* 28.07.2026: Spiegeln nur setzen, wenn wirklich gespiegelt wird - SetSprite
       hat die Flip-Bits ohnehin schon geloescht, und SpriteControl behaelt die
       Chain-Bits (Maske 0x27). Bei flip==0 bleibt der Pfad damit unveraendert. */
    if (flip) {
        u8 fm = META_FLIP_MASK(flip);
        SpriteControl(oam, SPR_FRONT, fm);
        if (lvl_sspr_b_idx[n] != 0u) SpriteControl((u8)(oam + 1u), SPR_FRONT, fm);
    }
}
static void spr_draw_chain_cell(u8 oam, u16 s_num, u8 ddx, u8 ddy, u8 chain_a) {
    spr_draw_chain_cell_f(oam, s_num, ddx, ddy, chain_a, 0u);
}
static void ship_draw_meta(u8 meta, u8 x, u8 y) {
    u16 off = lvl_meta_off[meta];
    u8 c;
    u8 same = (u8)(meta == g_ship_last_meta);
    if (same) {
        /* Fastpath: nur der Anker. Alle uebrigen 13 Slots folgen ueber die Kette. */
        SetSpritePosition((u8)SPR_SHIP, (u8)(x + g_ship_cell_dx[0]), (u8)(y + g_ship_cell_dy[0]));
        return;
    }
    for (c = 0u; c < (u8)SHIP_CELLS; c++) {
        u8 dx = (u8)lvl_meta_dx[off + c], dy = (u8)lvl_meta_dy[off + c];
        u8 oam = (u8)(SPR_SHIP + c * 2u);
        g_ship_cell_dx[c] = dx; g_ship_cell_dy[c] = dy;
        if (c == 0u) {
            /* Ankerzelle: absolute Position, KEIN Chain-Bit auf der a-Ebene. */
            spr_draw_chain_cell_f(oam, lvl_meta_num[off + c], (u8)(x + dx), (u8)(y + dy), 0u,
                                   META_FLIP_OF(off + c));
        } else {
            spr_draw_chain_cell_f(oam, lvl_meta_num[off + c],
                                 (u8)(dx - g_ship_cell_dx[c - 1u]),
                                 (u8)(dy - g_ship_cell_dy[c - 1u]), 1u,
                                 META_FLIP_OF(off + c));
        }
    }
    g_ship_last_meta = meta;
}

static void ship_hide(void) {
    u8 c;
    for (c = 0u; c < (u8)(SHIP_CELLS * 2u); c++) UnsetSprite((u8)(SPR_SHIP + c));
    g_ship_last_meta = 0xFFu;   /* naechster ship_draw_meta() MUSS voll neu zeichnen */
}

/* Tilemap-Zell direkt schreiben ??? unterstuetzt H-Flip (Bit 15 im Zell-Wort, NICHT 14 ???
   14 ist V-Flip. Layout laut K2GE-HW: Byte0=TTTTTTTT, Byte1=H V B PPPP T, siehe
   NeoPop-Core Memory-Map-Doku).
   SCR1-Map: 0x9000, SCR2-Map: 0x9800; 32 Zellen pro Zeile (je 2 Bytes). */
static void put_cell(u8 plane, u8 pal, u8 x, u8 y, u16 tile, u8 flip) {
    volatile u16 *map = (volatile u16*)((plane == SCR_1_PLANE) ? 0x9000u : 0x9800u);
    u16 flip_bit = flip ? 0x8000u : 0u;
    map[(u16)y * 32u + (u16)x] = (u16)(flip_bit | ((u16)pal << 9u) | tile);
}

/* Animations-Grid: pro Tilemap-Ringzeile (0-31) x Spalte (0-19) merken, ob dort
   gerade eine animierte Zelle liegt. 0 = keine Animation, sonst (anim_idx+1),
   Bit 0x80 = H-Flip der Zelle. Wird bei JEDEM Zellen-Schreiben mitgepflegt,
   dadurch immer konsistent mit dem aktuellen VRAM-Inhalt (auch nach Scroll-
   Streaming/Ring-Wrap). */
#define ANIM_GRID_ROWS 32u
#define ANIM_GRID_COLS 20u
static u8 g_anim_grid[ANIM_GRID_ROWS][ANIM_GRID_COLS];
static u8 g_anim_tick[LVL_ANIM_COUNT];
static u8 g_anim_frame[LVL_ANIM_COUNT];
/* 25.07.2026 Wurmloch-Skript (Nutzerwunsch): die Loch-Animation (Kopf-Kachel 138)
   laeuft NICHT mehr als Dauerschleife im Auto-Anim (Ausnahme direkt in
   anim_update ueber den Kopf-Wert), sondern wird skriptgesteuert beim
   Wurm-Austritt/-Eintritt abgespielt - siehe g_wormhole_anims. */

/* Pro Animation eine kurze Liste der (row,col)-Zellen, die diese Animation
   gerade zeigen ??? Performance-Fix: anim_update() lief vorher bei JEDEM
   Ausloesen (alle 10 Frames, 4 Animationen) durch das komplette 32x20-Grid
   (640 Zellen), das war ein spuerbarer Ruckler auf echter Hardware, VOELLIG
   unabhaengig vom Scrollen. Diese Liste wird nur dann aktualisiert, wenn
   tatsaechlich eine Zelle beschrieben/geleert wird (viel seltener). */
#define MAX_ANIM_CELLS 16u
static u8 g_anim_cell_row[LVL_ANIM_COUNT][MAX_ANIM_CELLS];
static u8 g_anim_cell_col[LVL_ANIM_COUNT][MAX_ANIM_CELLS];
static u8 g_anim_cell_count[LVL_ANIM_COUNT];

/* Einziger Schreibzugriffspunkt fuer g_anim_grid ??? haelt die obige Liste
   synchron (entfernen aus der alten, eintragen in die neue Animations-Liste). */
static void anim_grid_set(u8 row, u8 col, u8 value) {
    u8 old_idx = (u8)(g_anim_grid[row][col] & 0x7Fu);
    u8 new_idx = (u8)(value & 0x7Fu);
    if (old_idx != 0u) {
        u8 k = (u8)(old_idx - 1u);
        u8 j;
        for (j = 0u; j < g_anim_cell_count[k]; j++) {
            if (g_anim_cell_row[k][j] == row && g_anim_cell_col[k][j] == col) {
                g_anim_cell_count[k]--;
                g_anim_cell_row[k][j] = g_anim_cell_row[k][g_anim_cell_count[k]];
                g_anim_cell_col[k][j] = g_anim_cell_col[k][g_anim_cell_count[k]];
                break;
            }
        }
    }
    if (new_idx != 0u) {
        u8 k = (u8)(new_idx - 1u);
        if (g_anim_cell_count[k] < (u8)MAX_ANIM_CELLS) {
            u8 c = g_anim_cell_count[k];
            g_anim_cell_row[k][c] = row;
            g_anim_cell_col[k][c] = col;
            g_anim_cell_count[k] = (u8)(c + 1u);
        }
    }
    g_anim_grid[row][col] = value;
}

static u8 anim_index_for_head(u16 t) {
    u8 i;
    for (i = 0u; i < (u8)LVL_ANIM_COUNT; i++)
        if (lvl_anim_head[i] == t) return (u8)(i + 1u);
    return 0u;
}

/* ============ Map-Objekte: Zustand + verwelkte Darstellung (??13) ============
   Siehe die ausfuehrliche Erklaerung bei mapobj_hit_test/mapobj_wilt weiter
   unten (dort auch ship_hits_mapobj) ??? dieser Teil muss VOR lvl1_put_row()
   stehen, weil lvl1_put_row() mapobj_apply_row() aufruft (jede (Neu-)
   Zeichnung einer Map-Zeile muss ein bereits verwelktes Objekt weiterhin
   verwelkt zeigen, auch nach Rueckwaerts-Restore). */
#define MAPOBJ_NONE 0xFFu
static u8 g_mapobj_wilted[LVL_MAPOBJ_COUNT];

/* ===== Feuerrate + Taktung der Map-Objekte (28.07.2026) =====
   Das Original feuert bei ALLEN drei Wandelementen ueber einen 8-Bit-
   Akkumulator, nicht am Animationsende (Update 1000:6d22 / 6ac7 / 6bca, Raten
   CS:0x626e/0x6262/0x6268 = 4/5/10 je Tick). Unsere Blume feuerte bisher am
   Ende jedes Animationsdurchlaufs - das war eine Vereinfachung.
   lvl_mapobj_fire_rate == 0 haelt genau diese alte Fassung am Leben, damit
   bestehende Exporte unveraendert weiterlaufen.

   Die ANEMONE (Typ 3) ist zusaetzlich GETAKTET: sie steht auf Bild 0 still,
   bis ihr Akkumulator ueberlaeuft, spielt dann genau EINEN Durchlauf und ist
   danach wieder zu. Ihre Animation darf deshalb nicht zusaetzlich vom
   Auto-Loop in anim_update() getaktet werden - sonst schreiben beide dieselben
   Zellen. Genau dieselbe Trennung wie beim Wurmloch (Kopf 138). */
/* 28.07.2026: der Rueckfall (drei Null-Arrays fuer aeltere Exporte) ist raus -
   die map.h liefert lvl_mapobj_fire_rate/gated/shot_frame jetzt selbst. Ab hier
   braucht der Build eine map.h vom 28.07.2026 oder neuer. Das Praesenz-Define,
   an dem der Rueckfall haengen sollte, schreibt der Exporter gar nicht - der
   #ifndef hat deshalb nicht geschuetzt, sondern eine Doppeldeklaration erzeugt. */
static u8 g_mapobj_acc[LVL_MAPOBJ_COUNT];     /* 8-Bit-Akkumulator je Objekt */
static u8 g_mapobj_run[LVL_MAPOBJ_COUNT];     /* getaktet: 1 = Durchlauf laeuft gerade */
static u8 g_mapobj_frame[LVL_MAPOBJ_COUNT];   /* getaktet: Bildnummer im Durchlauf */
static u8 g_mapobj_tick[LVL_MAPOBJ_COUNT];    /* getaktet: Frames im aktuellen Bild */
/* Animationen, die zu einem getakteten Objekt gehoeren: der Auto-Loop laesst sie
   aus. 1 Byte je Animation, beim Levelstart einmal gefuellt (mapobj_gate_init). */
static u8 g_anim_is_gated[LVL_ANIM_COUNT];

/* Kollisions-Grid (wie g_anim_grid): 0 = keine Map-Objekt-Zelle hier, sonst
   (obj_idx+1). Macht mapobj_hit_test() zu einem O(1)-Lookup statt einer
   linearen Suche ueber alle Objekte x Zellen bei JEDEM Aufruf ??? bei
   LVL_MAPOBJ_COUNT=10 und bis zu 16 Aufrufen/Frame (12 Schiffs-Punkte + 4
   Schuesse) war das ein spuerbarer Ruckler auf echter Hardware (gleiches
   Muster, das anim_update() frueher schon mal hatte, siehe CLAUDE.md ??4). */
static u8 g_mapobj_grid[32][20];

/* Verwelkte Map-Objekt-Zellen sind in lvl_map weiterhin als normales,
   nicht-leeres Tile gespeichert (das ROM-Array aendert sich beim Verwelken
   nicht, nur die VRAM-Grafik) ??? terrain_solid() wuerde sie deshalb OHNE
   diese Markierung fuer immer als Wand behandeln, auch nachdem sie
   abgeschossen wurden ("kann nicht durchfliegen"-Bug, gemeldet 10.07.2026).
   1 = diese Ringzeile/Spalte zeigt gerade die Wilt-Ersatzgrafik (siehe
   mapobj_apply_row) -> terrain_solid soll hier NICHT blockieren. Wird bei
   jeder (Neu-)Zeichnung einer Zelle in lvl1_put_cell zuerst auf 0
   zurueckgesetzt, danach von mapobj_apply_row bei Bedarf wieder gesetzt
   (gleiche Reihenfolge wie g_anim_grid/anim_grid_set). */
/* 28.07.2026 ZUSAMMENGELEGT mit g_mapobj_grid (RAM war am Anschlag, siehe
   CLAUDE.md): ein Byte je Zelle traegt jetzt BEIDES - Bits 0-6 die
   Objektnummer+1 (seit 29.07.2026: 32 Objekte, Bits 0-6 tragen bis 126), Bit 7 "zeigt gerade Wilt-Grafik".
   Spart 640 Byte. Reihenfolge unveraendert: lvl1_put_cell() nullt die Zelle,
   mapobj_grid_update_row() traegt die Objektnummer ein, mapobj_apply_row()
   setzt danach das Wilt-Bit. */
#define MAPOBJ_WILT_BIT 0x80u

/* Physische Ringzeile (0-31), die GERADE die gegebene Map-Zeile zeigt, oder
   MAPOBJ_NONE wenn diese Map-Zeile aktuell nicht im 32-Zeilen-Fenster liegt. */
/* ZURUECKGENOMMEN 22.07.2026: die Umkehrtabelle brachte zwar messbar etwas
   (Messwert 72 -> 63 auf Hardware), zerlegte aber die Wandwuermer - Flackern
   und dauerhaft im Bild haengende Kettenglieder. Der Unterschied liegt in den
   Mehrfachbelegungen: liegt dieselbe Map-Zeile in mehreren Ringzeilen (beim
   Vorbelegen zeigen alle 32 auf LVL_MAP_H), liefert die Suche die ERSTE, die
   Tabelle die ZULETZT geschriebene. Die Wuermer haengen an genau dieser Zuordnung.
   Die eigentliche Verschwendung - 512 Suchdurchlaeufe je dritten Frame - ist
   stattdessen in wallworms_update() abgestellt (siehe dort): dort wird gar
   nicht mehr gesucht, solange kein Loch in Reichweite sein KANN. */
static u8 find_ring_row(u16 map_row) {
    u8 r;
    for (r = 0u; r < 32u; r++)
        if (g_row_map[r] == map_row) return r;
    return MAPOBJ_NONE;
}

/* Traegt fuer eine gerade gezeichnete Map-Zeile alle NOCH INTAKTEN Map-
   Objekt-Zellen ins Kollisions-Grid ein. Zeile zuerst komplett leeren (die
   Ringzeile wird ja wiederverwendet, alte Eintraege muessen weg), dann nur
   die passenden Zellen setzen. Aus lvl1_put_row() aufgerufen ??? bei JEDER
   (Neu-)Zeichnung, damit das Grid immer synchron mit dem sichtbaren VRAM
   bleibt (Streaming, Rueckwaerts-Restore, Prefill). */
static void mapobj_grid_update_row(u16 map_row, u8 ring_row) {
    u8 col, o, i;
    for (col = 0u; col < 20u; col++) g_mapobj_grid[ring_row][col] = 0u;
    for (o = 0u; o < (u8)LVL_MAPOBJ_COUNT; o++) {
        u16 off;
        u8 count;
        /* Nutzerkorrektur 10.07.2026: lvl_mapobj_hitzone_enabled war bisher
           nie geprueft ??? 0 = rein dekoratives Objekt, kompletter Kollisions-
           test uebersprungen (auch wenn Zellen mit "g"-Suffix markiert
           sind). Am einfachsten VOR dem Kollisionstest: gar nicht erst ins
           Grid eintragen, dann findet mapobj_hit_test es nie. */
        if (g_mapobj_wilted[o] || !lvl_mapobj_hitzone_enabled[o]) continue;
        off   = lvl_mapobj_cell_off[o];
        count = lvl_mapobj_cell_count[o];
        for (i = 0u; i < count; i++)
            if (lvl_mapobj_cell_row[off + i] == map_row)
                g_mapobj_grid[ring_row][lvl_mapobj_cell_col[off + i]] = (u8)(o + 1u);
    }
}

/* Zeichnet die "verwelkten" Ersatz-Tiles fuer alle Zellen von Objekt obj_idx,
   die GERADE zur Map-Zeile map_row gehoeren, auf die physische Ringzeile
   ring_row, und nimmt sie aus dem Kollisions-Grid raus (nicht mehr treffbar). */
static void mapobj_apply_row(u16 map_row, u8 ring_row) {
    u8 o, i;
    for (o = 0u; o < (u8)LVL_MAPOBJ_COUNT; o++) {
        u16 off;
        u8 count;
        if (!g_mapobj_wilted[o]) continue;
        off   = lvl_mapobj_cell_off[o];
        count = lvl_mapobj_cell_count[o];
        for (i = 0u; i < count; i++) {
            u8  col, flip, a_p, b_p, a_hw;
            u16 a_i, b_i;
            if (lvl_mapobj_cell_row[off + i] != map_row) continue;
            col  = lvl_mapobj_cell_col[off + i];
            flip = lvl_mapobj_wilt_flip[off + i];
            a_i  = g_lvl_tile_remap[lvl_mapobj_wilt_a_idx[off + i]];
            a_p  = lvl_mapobj_wilt_a_pal[off + i];
            b_i  = g_lvl_tile_remap[lvl_mapobj_wilt_b_idx[off + i]];
            b_p  = lvl_mapobj_wilt_b_pal[off + i];
            a_hw = (a_p < 16u) ? g_scr2_pal_map[a_p] : g_scr2_pal_map[0];
            put_cell(SCR_2_PLANE, a_hw, col, ring_row,
                     g_lvl_vram[a_i], flip);
            put_cell(SCR_1_PLANE, b_p, col, ring_row,
                     g_lvl_vram[b_i], flip);
            anim_grid_set(ring_row, col, 0u);   /* verwelkt animiert nicht mehr */
            /* raus aus der Kollision UND als verwelkt markieren (ein Byte, siehe MAPOBJ_WILT_BIT) */
            g_mapobj_grid[ring_row][col] = (u8)MAPOBJ_WILT_BIT;
        }
    }
}

/* bullet_on_loop (??13, Schritt 13, seit 06.07.2026 4. Runde): Anim-Index der
   Anker-Zelle eines Objekts direkt aus lvl_map nachschlagen (ROM-Daten,
   unabhaengig vom Scroll-Zustand ??? die Zelle aendert sich nie). Liefert 0,
   wenn die Zelle keine (passende) Animation hat, sonst (anim_idx0+1). */
static u8 mapobj_anchor_anim_head_idx(u8 obj) {
    u16 raw = lvl_map[(u16)lvl_mapobj_anchor_row[obj] * (u16)LVL_MAP_W + lvl_mapobj_anchor_col[obj]];
    u16 t   = raw & 0x01FFu;
    if (t == 0u || !(raw & 0x4000u)) return 0u;
    return anim_index_for_head(t);
}

/* Wird aus anim_update() aufgerufen, sobald Animation anim_idx0 (0-basiert)
   einen Loop abgeschlossen hat (Frame auf 0 zurueckgesprungen). Alle aktiven
   (nicht verwelkten) Map-Objekte mit bullet_on_loop UND Anker-Zelle an genau
   dieser Animation feuern jetzt einen Schuss von ihrer aktuellen
   Bildschirmposition ab (nur wenn die Anker-Zeile gerade sichtbar ist). */
/* 30.07.2026 GEISTER-SPRITE, Ursache gefunden (Nutzerbericht "einige Schuesse der
   Tuerme bleiben als Geist im Bild haengen") - und damit auch die Ursache des
   Altpostens vom 25.07.2026 ("einmal in mehreren Durchlaeufen ein Geister-
   Sprite"), fuer den damals nur der Hausputz als Netz gebaut wurde.
   Die Frame-Reihenfolge macht es zwingend:
     1. mapobj_bullets_update() setzt active=0, gibt den OAM-Slot aber NICHT
        zurueck - das macht erst der Zeichendurchlauf.
     2. anim_update() und mapobj_rates_update() laufen DANACH und rufen
        mapobj_fire(), das genau diesen Slot recycelt und dabei oam=OAM_NONE
        schreibt: die Slot-NUMMER ist damit verloren.
     3. Der Zeichendurchlauf sieht active=1 / oam==OAM_NONE und holt einen NEUEN
        Slot - der alte OAM-Eintrag bleibt fuer immer gesetzt (Geist) und fehlt
        dem Pool (Leck, das die Blockvergabe zusaetzlich verengt).
   Nur die Map-Objekt-Schuesse sind betroffen: Gegner- und Kriecherschuesse
   entstehen VOR ebullets_update(), dort kann der Fall nicht auftreten.
   Behebung nach der Projektregel von enemy_take_damage/wcrawl_hit: Slot SOFORT
   beim Deaktivieren zurueck. Die Freigabe im Zeichendurchlauf bleibt als Netz. */
static void mo_bullet_kill(u8 i) {
    if (g_mo_bullets[i].oam != OAM_NONE) {
        UnsetSprite(g_mo_bullets[i].oam);
        oam_pool_free(g_mo_bullets[i].oam);
        g_mo_bullets[i].oam = OAM_NONE;
        g_mo_bullets[i].last_snum = 0u;
    }
    g_mo_bullets[i].active = 0u;
}

/* 28.07.2026 herausgeloest: EINEN Schuss von Objekt o abfeuern. Wird jetzt aus
   zwei Richtungen aufgerufen - vom alten Loop-Ende (Feuerrate 0) und vom
   Akkumulator/Taktgeber in mapobj_rates_update(). */
#if TELEMETRY
static u8 g_dbg_mofire, g_dbg_mocall;   /* Nachweis-Zaehler, siehe probe_mapobj.py */
#endif
static void mapobj_fire(u8 o) {
    u8 i;
    u8 ring_row = find_ring_row((u8)lvl_mapobj_anchor_row[o]);
#if TELEMETRY
    g_dbg_mocall++;   /* C89: erst Deklarationen, dann Anweisungen */
#endif
    if (ring_row == MAPOBJ_NONE) return;   /* Ankerzeile gerade nicht im Bild */
    {
        for (i = 0u; i < (u8)MAX_MAPOBJ_BULLETS; i++) {
            if (!g_mo_bullets[i].active) {
                u8  anemone = lvl_mapobj_gated[o];   /* siehe MAPOBJ_SHOT_* */
                u8  gespiegelt = 0u;
                u8  sx = (u8)(lvl_mapobj_anchor_col[o] * 8u + 4u);
                u8  sy = (u8)((u8)(ring_row << 3) - g_scr1_y + 4u);   /* u8-Wrap gewollt, wie terrain_solid */
                { s16 bdx0 = (s16)lvl_mapobj_bullet_dx[o];
                  if (bdx0 < 0) gespiegelt = 1u; }
                if (anemone) {
                    sy = (u8)(sy - (u8)MAPOBJ_SHOT_Y_UP);
                    /* Spiegelungs-Ausgleich in x, siehe MAPOBJ_SHOT_X_FLIPFIX. */
                    if (gespiegelt) sx = (u8)(sx - (u8)MAPOBJ_SHOT_X_FLIPFIX);
                }
                /* NETZ (30.07.2026, siehe mo_bullet_kill): haengt an diesem Slot
                   noch ein OAM-Eintrag, weil der Schuss NICHT ueber
                   mo_bullet_kill() beendet wurde (Sammel-Deaktivierung bei der
                   Smart Bomb), dann erst zurueckgeben - das oam=OAM_NONE weiter
                   unten wuerde die Slot-Nummer sonst verlieren. */
                if (g_mo_bullets[i].oam != OAM_NONE) {
                    UnsetSprite(g_mo_bullets[i].oam);
                    oam_pool_free(g_mo_bullets[i].oam);
                }
                g_mo_bullets[i].active = 1u;
                /* 25.07.2026 Frueh-Ausstieg: Flag AN DER ERZEUGUNGSSTELLE setzen.
                   Diese Schuesse entstehen in anim_update(), also NACH
                   mapobj_bullets_update() - das Update wuerde das Flag erst im
                   naechsten Frame setzen und der Schuss waere in seinem
                   Entstehungsframe unsichtbar (nachgewiesen: Frame 780/781). */
                g_busy_mobullets = 1u;
                g_mo_bullets[i].x_fix  = (s16)((s16)sx << 4);
                g_mo_bullets[i].y_fix  = (s16)((s16)sy << 4);
                g_mo_bullets[i].dx     = (s16)lvl_mapobj_bullet_dx[o];
                g_mo_bullets[i].dy     = (s16)lvl_mapobj_bullet_dy[o];
                g_mo_bullets[i].spr    = lvl_mapobj_bullet_spr[o] & 0x01FFu;
                /* Spiegelung an der Flugrichtung, nicht am Objektindex: die
                   rechte (gespiegelte) Wand schiesst nach links (gespiegelt,
                   oben aus dem Vorzeichen von bullet_dx bestimmt). */
                g_mo_bullets[i].flip = 0u;
                if (gespiegelt) g_mo_bullets[i].flip = (u8)SPR_HFLIP;
                g_mo_bullets[i].prio = (u8)SPR_FRONT;
                if (anemone) g_mo_bullets[i].prio = (u8)SPR_BEHIND;
                g_mo_bullets[i].oam    = OAM_NONE;
                g_mo_bullets[i].last_snum = 0u;
#if TELEMETRY
                g_dbg_mofire++;
#endif
                break;
            }
        }
    }
}

/* Wird aus anim_update() aufgerufen, sobald Animation anim_idx0 einen Loop
   abgeschlossen hat. NUR noch fuer Objekte mit Feuerrate 0 - das ist die alte
   Fassung, die bestehende Exporte unveraendert weiterlaufen laesst. Alles mit
   Rate feuert stattdessen ueber den Akkumulator (mapobj_rates_update). */
static void mapobj_anim_wrapped(u8 anim_idx0) {
    u8 o;
    for (o = 0u; o < (u8)LVL_MAPOBJ_COUNT; o++) {
        u8 ai;
        if (g_mapobj_wilted[o] || !lvl_mapobj_bullet_on_loop[o]) continue;
        if (lvl_mapobj_fire_rate[o] != 0u || lvl_mapobj_gated[o]) continue;
        ai = mapobj_anchor_anim_head_idx(o);
        if (ai == 0u || (u8)(ai - 1u) != anim_idx0) continue;
        mapobj_fire(o);
    }
}

/* ============ Vorberechnete Tilemap-Woerter (22.07.2026) ============
   lvl1_put_cell() holte sich fuer JEDE Zelle acht Werte aus der ROM: Grafik-
   und Palettenindex fuer Vorder- und Hintergrund, zweimal die Kompaktierungs-
   tabelle, die Palettenzuordnung - und anim_index_for_head() suchte die
   Animationsliste linear durch. Bei 20 Zellen je einrollender Kachelzeile sind
   das rund 160 Modul-Zugriffe in EINEM Frame. Auf echter Hardware kostet jeder
   davon Wartezyklen (CLAUDE.md ??00.XII), und genau solche Spitzen ziehen die
   Anzeige "schlechtester Frame" nach unten.

   put_cell() schreibt am Ende nur ein 16-Bit-Wort: Flip | Palette<<9 | Kachel.
   Das Flip-Bit kommt aus der Zelle, der Rest haengt allein an der Kachelnummer -
   also einmal vorberechnen und im RAM halten. MUSS nach jedem Wechsel des
   Terrain-Abschnitts neu gebaut werden, dabei aendert sich g_lvl_tile_remap. */
/* 28.07.2026, Nutzerwunsch "mit der aktuellen map.h, aber ohne Tentakelbewegung":
   Die drei Tentakel-Animationen der Endboss-Kammer (Koepfe 175/176/177, je 4
   Bilder, jedes Bild gespalten) kosten 22 VRAM-Kacheln. Damit braucht das
   dichteste Fenster 156 von 147 verfuegbaren Slots - die Map passt sonst nicht.
   Hier werden sie NICHT angemeldet: die Zellen zeigen dauerhaft ihr Grundbild,
   die Kammer sieht aus wie vorher, nur ohne Bewegung.

   MUSS mit SKIPANIM= beim Abschnittsgenerator uebereinstimmen
   (tools/gen_terrain_sections.js). Steht dort etwas anderes, bekommen die
   Folgebilder keinen VRAM-Slot, die Zelle zeigt aber trotzdem darauf -> das
   ist genau die Fehlerklasse "remap == 0", also eine schwarze Kachel ohne
   jede Fehlermeldung (CLAUDE.md ??6).
   Sollen die Tentakel wieder laufen, braucht es Platz: entweder weniger
   verschiedene Bilder (3 statt 4, per Bildfolge 1,2,3,2 gespielt) oder Bilder
   mit hoechstens 3 Farben, damit sie nicht gespalten werden. */
/* 28.07.2026, 22:00: WIEDER AN. Der Nutzer hat die Belohnungs-Animationen
   gekuerzt, der Sprite-Pool braucht nur noch 318 von 350 - die 11 frei
   gewordenen Adressen gehen an die Terrain-Zone (147 -> 158), und genau 158
   braucht das Engpass-Fenster mit den Tentakeln. */
#define SKIP_TENTACLE_ANIM 0
static u8 anim_head_skipped(u16 head) {
#if SKIP_TENTACLE_ANIM
    return (u8)((head == 175u || head == 176u || head == 177u) ? 1u : 0u);
#else
    (void)head;
    return 0u;
#endif
}

#define TILE_WORD_N ((u16)(sizeof(lvl_tile_a_idx) / sizeof(lvl_tile_a_idx[0])) + 1u)
static u16 g_tile_word_a[TILE_WORD_N];
static u16 g_tile_word_b[TILE_WORD_N];
static u8  g_tile_anim[TILE_WORD_N];

static void tile_words_build(void) {
    u16 t, a_i, b_i;
    u8  a_p, b_p, a_hw, i;
    for (t = 0u; t < TILE_WORD_N; t++) {
        g_tile_word_a[t] = 0u;
        g_tile_word_b[t] = 0u;
        g_tile_anim[t]   = 0u;
    }
    for (t = 1u; t < TILE_WORD_N; t++) {
        a_i  = g_lvl_tile_remap[lvl_tile_a_idx[t - 1u]];
        a_p  = lvl_tile_a_pal[t - 1u];
        b_i  = g_lvl_tile_remap[lvl_tile_b_idx[t - 1u]];
        b_p  = lvl_tile_b_pal[t - 1u];
        a_hw = (a_p < 16u) ? g_scr2_pal_map[a_p] : g_scr2_pal_map[0];
        g_tile_word_a[t] = (u16)(((u16)a_hw << 9u) | g_lvl_vram[a_i]);
        g_tile_word_b[t] = (u16)(((u16)b_p  << 9u) | g_lvl_vram[b_i]);
    }
    /* Animationsindex je Kopf-Kachel; erster Treffer gewinnt, genau wie die
       abgeloeste lineare Suche. */
    for (i = 0u; i < (u8)LVL_ANIM_COUNT; i++) {
        t = lvl_anim_head[i];
        if (anim_head_skipped(t)) continue;   /* 28.07.2026, siehe dort */
        if (t < TILE_WORD_N && g_tile_anim[t] == 0u) g_tile_anim[t] = (u8)(i + 1u);
    }
}

static void put_cell_word(u8 plane, u8 col, u8 scr_ty, u16 word, u8 flip) {
    volatile u16 *map = (volatile u16*)((plane == SCR_1_PLANE) ? 0x9000u : 0x9800u);
    map[(u16)scr_ty * 32u + (u16)col] = (u16)((flip ? 0x8000u : 0u) | word);
}

static void lvl1_put_cell(u8 col, u8 scr_ty, u16 raw) {
    u8  flip, anim_bit, ai;
    u16 t;
    flip     = (raw & 0x8000u) ? 1u : 0u;
    anim_bit = (raw & 0x4000u) ? 1u : 0u;
    t        = raw & 0x01FFu;   /* Bits 0-8 = Streifen-Tile-Nr (1-basiert) */
    if (t >= TILE_WORD_N) t = 0u;   /* laut Datenpruefung nicht vorhanden, als Schutz */
    /* Leere Zelle: der abgeloeste Code schrieb dort ausdruecklich flip=0. */
    if (t == 0u) flip = 0u;
    put_cell_word(SCR_2_PLANE, col, scr_ty, g_tile_word_a[t], flip);
    put_cell_word(SCR_1_PLANE, col, scr_ty, g_tile_word_b[t], flip);
    ai = (anim_bit && t != 0u) ? g_tile_anim[t] : 0u;
    anim_grid_set(scr_ty, col, ai ? (u8)(ai | (flip ? 0x80u : 0u)) : 0u);
    /* Default: nicht verwelkt. mapobj_apply_row() (aus lvl1_put_row() NACH
       dem Zeilen-Loop aufgerufen) setzt dies fuer tatsaechlich verwelkte
       Zellen das Wilt-Bit wieder ??? siehe MAPOBJ_WILT_BIT oben. */
    g_mapobj_grid[scr_ty][col] = 0u;   /* Objektnummer UND Wilt-Bit; beides setzt lvl1_put_row gleich neu */
}

/* Wandkriecher an die Ringzeile binden (28.07.2026) ??? Definition weiter unten
   bei den uebrigen Kriecher-Funktionen; hier nur die Vorwaertsdeklaration, weil
   lvl1_put_row() die einzige Stelle ist, an der eine Map-Zeile einer Ringzeile
   zugeordnet wird. Genau wie mapobj_grid_update_row() nebenan. */
static void wcrawl_row_in(u16 map_row, u8 ring_row);

/* map_row >= LVL_MAP_H (Level-Ende) -> komplette Zeile leer schreiben (Tunnel-Ende) */
static void lvl1_put_row(u16 map_row, u8 scr_ty) {
    u8 mx;
    if (map_row < (u16)LVL_MAP_H) {
        for (mx = 0u; mx < (u8)LVL_MAP_W; mx++)
            lvl1_put_cell(mx, scr_ty, lvl_map[(u16)map_row * (u16)LVL_MAP_W + mx]);
        mapobj_grid_update_row(map_row, scr_ty);   /* Kollisions-Grid synchronisieren */
        mapobj_apply_row(map_row, scr_ty);   /* bereits verwelkte Objekte ueberschreiben */
    } else {
        for (mx = 0u; mx < (u8)LVL_MAP_W; mx++)
            lvl1_put_cell(mx, scr_ty, 0u);
    }
        wcrawl_row_in(map_row, scr_ty);   /* 28.07.2026: Wandkriecher dieser Zeile scharfstellen */
        g_row_map[scr_ty] = map_row;   /* merken, was hier hingehoert (fuer Bar-Vacate-Restore) */
}

/* Sichtbereich vorbelegen: Map-Zeilen row_base..row_base+18 auf Tilemap-Zeilen 18..0,
   setzt danach g_lvl_row = row_base+19 (naechste einrollende Zeile). Ausgelagert aus
   game_start(), damit shop_resume() (Segment-Umschaltung) dieselbe Logik mit
   row_base=LVL1_SEGB_ROW_BASE statt 0 wiederverwenden kann. */
static void lvl1_prefill(u16 row_base) {
    u8 ty;
    for (ty = 0u; ty < 19u; ty++)
        lvl1_put_row((u16)(row_base + 18u - ty), ty);
    g_lvl_row = (u16)(row_base + 19u);
}

/* Ringzeile `row` auf das Terrain zuruecksetzen, das laut g_row_map dort hingehoert
   (verwendet wenn die Bar/der Separator weiterzieht und die Zeile wieder freigibt). */
static void restore_terrain_row(u8 row) {
    lvl1_put_row(g_row_map[row], row);
}

/* Pro Frame: Animationen takten, betroffene Zellen (laut g_anim_grid) neu schreiben.
   Kein Tiledaten-Umkopieren ??? nur Map-Words, alle Frame-Tiles liegen schon in VRAM. */
static void anim_update(void) {
    u8 i;
    for (i = 0u; i < (u8)LVL_ANIM_COUNT; i++) {
        /* 25.07.2026 (2. Fassung): Wurmloch-Anim (Kopf-Kachel 138) vom Auto-Loop
           ausnehmen - sie laeuft NUR skriptgesteuert (g_wormhole_anims). Bewusst
           OHNE gecachten Index/Sentinel: die Lazy-Init-Fassung (g_wormhole_anim_idx
           = 0xFE als Startwert) griff auf HW nicht - Vergleich direkt ueber den
           Kopf-Wert, zustandslos und narrensicher (5 u16-Vergleiche/Frame). */
        if (lvl_anim_head[i] == 138u) continue;
        /* 28.07.2026: Animationen getakteter Objekte (Anemone) laufen NICHT im
           Auto-Loop - die taktet mapobj_rates_update() selbst. Sonst schreiben
           beide dieselben Zellen und die Anemone zappelt. Gleiche Trennung wie
           beim Wurmloch eine Zeile darueber. */
        if (g_anim_is_gated[i]) continue;
        g_anim_tick[i]++;
#if TEST_ANIM_SLOW
        if (g_anim_tick[i] >= (u8)(lvl_anim_speed[i] * (u8)TEST_ANIM_SLOW)) {   /* Messbuild: langsamer */
#else
        if (g_anim_tick[i] >= lvl_anim_speed[i]) {
#endif
            u8  j, ty, tx, flip2, a_p2, b_p2, a_hw2;
            u16 n, a_i2, b_i2;
            g_anim_tick[i] = 0u;
            g_anim_frame[i]++;
            if (g_anim_frame[i] >= lvl_anim_len[i]) {
                g_anim_frame[i] = 0u;
                mapobj_anim_wrapped(i);   /* ??13 bullet_on_loop: Loop dieser Animation beendet */
            }
            n    = lvl_anim_frames[i][g_anim_frame[i]];
            /* 28.07.2026: Bildnummer 0 = LEERE Zelle. Das Original braucht das bei
               der Anemone (Bild 4 und 17: der Kopf steckt noch/wieder drin, die
               vordere Zelle zeigt nichts). Ohne diesen Zweig liefe n-1 auf 0xFFFF
               und griffe weit hinter die Kacheltabelle. */
            if (n == 0u) { a_i2 = 0u; a_p2 = 0u; b_i2 = 0u; b_p2 = 0u; }
            else {
            a_i2 = g_lvl_tile_remap[lvl_tile_a_idx[n - 1u]];
            a_p2 = lvl_tile_a_pal[n - 1u];
            b_i2 = g_lvl_tile_remap[lvl_tile_b_idx[n - 1u]];
            b_p2 = lvl_tile_b_pal[n - 1u];
            }
            a_hw2 = (a_p2 < 16u) ? g_scr2_pal_map[a_p2] : g_scr2_pal_map[0];
            /* Nur die tatsaechlich registrierten Zellen (g_anim_cell_*), nicht
               mehr das komplette 32x20-Grid absuchen ??? siehe anim_grid_set(). */
            for (j = 0u; j < g_anim_cell_count[i]; j++) {
                ty = g_anim_cell_row[i][j];
                tx = g_anim_cell_col[i][j];
                /* 30.07.2026 (Nutzerwunsch: "die Animation darf erst laufen, wenn
                   sie auch im Bild ist"): Zellen ausserhalb des sichtbaren
                   Bereichs nicht schreiben. Der 32-Zeilen-Ring ist 256 px hoch,
                   sichtbar sind 136 - bis zu 40 % der registrierten Zellen lagen
                   also dauerhaft ausserhalb und wurden trotzdem jeden Bildwechsel
                   beschrieben. Gleiche Rechnung wie bei den Rissen weiter unten
                   (ring<<3 - g_scr1_y, u8-Wrap gewollt).
                   ACHTUNG: der Zaehler (g_anim_tick/g_anim_frame) laeuft bewusst
                   WEITER - er gilt fuer ALLE Zellen dieser Animation ueber die
                   ganze Map, und an ihm haengt ueber mapobj_anim_wrapped() das
                   Feuern. Ihn anzuhalten wuerde das Schussverhalten aendern.
                   MERKE: am 29.07. wurde genau dieser Sprung im TURMBAND gemessen
                   und brachte +-0 - dort ist fast alles sichtbar. In leeren
                   Szenen mit weit verteilten Anim-Zellen kann es anders sein;
                   deshalb steht er hier zur Messung, nicht als Gewissheit. */
                if ((u8)((u8)(ty << 3) - g_scr1_y) >= (u8)CLIP_Y) continue;
                flip2 = (g_anim_grid[ty][tx] & 0x80u) ? 1u : 0u;
                put_cell(SCR_2_PLANE, a_hw2, tx, ty,
                         g_lvl_vram[a_i2], flip2);
                put_cell(SCR_1_PLANE, b_p2,  tx, ty,
                         g_lvl_vram[b_i2], flip2);
            }
        }
    }
}

/* ===== Getaktete Map-Objekte + Akkumulator-Feuern (28.07.2026) =====
   Siehe Kommentar bei g_mapobj_acc. mapobj_gate_init() muss VOR dem ersten
   anim_update() laufen (game_start/shop_resume/respawn), sonst taktet der
   Auto-Loop die Anemone einmal mit. */
static void mapobj_gate_init(void) {
    u8 i, o;
    for (i = 0u; i < (u8)LVL_ANIM_COUNT; i++) g_anim_is_gated[i] = 0u;
    for (o = 0u; o < (u8)LVL_MAPOBJ_COUNT; o++) {
        g_mapobj_acc[o]   = (u8)(QRandom() & 0x3Fu);   /* zufaellig vorgespannt wie im Original (Reseed rand & 0x3f) */
        g_mapobj_run[o]   = 0u;
        g_mapobj_frame[o] = 0u;
        g_mapobj_tick[o]  = 0u;
        if (!lvl_mapobj_gated[o]) continue;
        /* 29.07.2026: ALLE Zell-Animationen des Objekts gaten, nicht nur die
           Anker-Zelle. Die Anemone besteht aus ZWEI Animationen (Deckel Kopf 79,
           Fuehler Kopf 80) - nur den Anker zu gaten liesse den Auto-Loop in
           anim_update() die zweite Zelle frei weiterdrehen, waehrend
           mapobj_gated_draw() dieselbe Zelle taktet (Doppel-Schreiber, exakt
           die Fehlerklasse aus dem Kommentar oben). */
        { u16 off = lvl_mapobj_cell_off[o];
          u8  cn  = lvl_mapobj_cell_count[o], k;
          for (k = 0u; k < cn; k++) {
              u16 raw = lvl_map[(u16)lvl_mapobj_cell_row[off + k] * (u16)LVL_MAP_W
                                + lvl_mapobj_cell_col[off + k]];
              if (raw & 0x4000u) {
                  u8 ai = anim_index_for_head(raw & 0x01FFu);
                  if (ai != 0u) g_anim_is_gated[(u8)(ai - 1u)] = 1u;
              }
          } }
    }
}

/* Eine Bildnummer auf ALLE animierten Zellen des Objekts schreiben - genau wie
   anim_update() es tut, nur objektweise statt animationsweise. Die Animation
   je Zelle steht schon im g_anim_grid (der Auto-Loop nutzt dieselbe Quelle),
   deshalb braucht es hier keine eigene Zellentabelle. */
static void mapobj_gated_draw(u8 o, u8 frame) {
    u16 off = lvl_mapobj_cell_off[o];
    u8  cnt = lvl_mapobj_cell_count[o], k;
    for (k = 0u; k < cnt; k++) {
        u8  ring = find_ring_row(lvl_mapobj_cell_row[off + k]);
        u8  tx   = lvl_mapobj_cell_col[off + k];
        u8  gv, ai, flip2, a_p2, b_p2, a_hw2, f;
        u16 n, a_i2, b_i2;
        if (ring == MAPOBJ_NONE) continue;
        /* 30.07.2026, gleicher Nutzerwunsch wie in anim_update(): nur zeichnen,
           was im Bild ist. "Im Ring" heisst noch lange nicht "sichtbar" - der
           Ring ist 256 px hoch, das Bild 136. */
        if ((u8)((u8)(ring << 3) - g_scr1_y) >= (u8)CLIP_Y) continue;
        gv = g_anim_grid[ring][tx];
        if ((gv & 0x7Fu) == 0u) continue;          /* diese Zelle ist nicht animiert */
        ai    = (u8)((gv & 0x7Fu) - 1u);
        flip2 = (gv & 0x80u) ? 1u : 0u;
        f     = frame;
        if (f >= lvl_anim_len[ai]) f = (u8)(lvl_anim_len[ai] - 1u);
        n     = lvl_anim_frames[ai][f];
        if (n == 0u) { a_i2 = 0u; a_p2 = 0u; b_i2 = 0u; b_p2 = 0u; }   /* leeres Bild, siehe anim_update */
        else {
        a_i2  = g_lvl_tile_remap[lvl_tile_a_idx[n - 1u]];
        a_p2  = lvl_tile_a_pal[n - 1u];
        b_i2  = g_lvl_tile_remap[lvl_tile_b_idx[n - 1u]];
        b_p2  = lvl_tile_b_pal[n - 1u];
        }
        a_hw2 = (a_p2 < 16u) ? g_scr2_pal_map[a_p2] : g_scr2_pal_map[0];
        put_cell(SCR_2_PLANE, a_hw2, tx, ring, g_lvl_vram[a_i2], flip2);
        put_cell(SCR_1_PLANE, b_p2,  tx, ring, g_lvl_vram[b_i2], flip2);
    }
}

/* Laenge des Durchlaufs = Bildzahl der Anker-Animation (alle Zellen eines
   Objekts teilen sich dieselbe Bildzahl; bei ungleichen klemmt der Draw). */
static u8 mapobj_gate_len(u8 o) {
    u8 ai = mapobj_anchor_anim_head_idx(o);
    return (ai == 0u) ? 1u : lvl_anim_len[(u8)(ai - 1u)];
}
static u8 mapobj_gate_speed(u8 o) {
    u8 ai = mapobj_anchor_anim_head_idx(o);
    return (ai == 0u) ? 1u : lvl_anim_speed[(u8)(ai - 1u)];
}

/* Pro Frame, NACH anim_update(). Deckt beide neuen Faelle ab:
   - rate>0, nicht getaktet: Akkumulator -> Schuss bei Ueberlauf (Turm 1+2)
   - getaktet: Akkumulator startet einen Durchlauf, Schuss beim eingestellten
     Bild, danach wieder still (Anemone) */
static void mapobj_rates_update(void) {
    u8 o;
    for (o = 0u; o < (u8)LVL_MAPOBJ_COUNT; o++) {
        u8 rate = lvl_mapobj_fire_rate[o];
        if (g_mapobj_wilted[o] || !lvl_mapobj_bullet_on_loop[o]) continue;
        if (lvl_mapobj_gated[o]) {
            if (!g_mapobj_run[o]) {
                /* zu: nur der Akkumulator laeuft. Bild 0 steht schon in der Map
                   (Grundkachel), es muss nichts gezeichnet werden. */
                u8 before = g_mapobj_acc[o];
                if (rate == 0u) continue;                  /* ohne Rate startet nie ein Durchlauf */
                g_mapobj_acc[o] = (u8)(g_mapobj_acc[o] + rate);
                if (g_mapobj_acc[o] >= before) continue;    /* kein Ueberlauf */
                g_mapobj_acc[o] = (u8)(QRandom() & 0x3Fu);  /* wie im Original neu vorspannen */
                g_mapobj_run[o] = 1u;
                g_mapobj_frame[o] = 0u;
                g_mapobj_tick[o]  = 0u;
            }
            /* laeuft: Bild takten, zeichnen, beim Schussbild feuern */
            { u8 len = mapobj_gate_len(o), spd = mapobj_gate_speed(o);
              mapobj_gated_draw(o, g_mapobj_frame[o]);
              g_mapobj_tick[o]++;
              if (g_mapobj_tick[o] >= spd) {
                  g_mapobj_tick[o] = 0u;
                  if (g_mapobj_frame[o] == lvl_mapobj_shot_frame[o]) mapobj_fire(o);
                  g_mapobj_frame[o]++;
                  if (g_mapobj_frame[o] >= len) {          /* Durchlauf zu Ende -> wieder zu */
                      g_mapobj_frame[o] = 0u;
                      g_mapobj_run[o]   = 0u;
                      mapobj_gated_draw(o, 0u);            /* Ruhelage zeichnen */
                  }
              } }
            continue;
        }
        if (rate == 0u) continue;   /* alte Fassung: feuert in mapobj_anim_wrapped */
        { u8 before = g_mapobj_acc[o];
          g_mapobj_acc[o] = (u8)(g_mapobj_acc[o] + rate);
          if (g_mapobj_acc[o] < before) {                   /* 8-Bit-Ueberlauf = Schuss */
              g_mapobj_acc[o] = (u8)(QRandom() & 0x3Fu);
              mapobj_fire(o);
          } }
    }
}

// --- Hintergrund aufbauen --
static void build_bg(void) {
    /* TILE_TEST: Sterne ausgeblendet ??? nur Terrain sichtbar */
    (void)0;
}

/* Bar/Separator an physischer Ringzeile `row` zeichnen. SCR2 traegt die Grafik,
   SCR1 (vordere Ebene) muss dort geleert werden, sonst scheint Terrain durch. */
static void bar_draw_at(u8 row) {
    volatile u16 *m1 = (volatile u16*)(0x9000u + (u16)row * 64u);
    u8 tx, bi;
    for (tx = 0u; tx < 20u; tx++) m1[tx] = 0u;
    for (tx = 0u; tx < 20u; tx++) {
        bi = g_bar_col[tx];
        /* Spalte 19: Design 10 gespiegelt (ersetzt das fruehere eigene Design 13) */
        put_cell(SCR_2_PLANE, barPal[bi], tx, row,
                 (u16)(TILE_BAR_BASE + bi), (tx == 19u) ? 1u : 0u);
        anim_grid_set(row, tx, 0u);  /* Bar ist nie animiert ??? Altlast aus Terrain-Vorbelegung loeschen */
    }
    /* ??5.4: Bar-Zeile wurde (neu) gezeichnet -> Ziffern-Cache invalidieren, damit
       score_draw() die Score-Tiles (Spalten 2-8) wieder auf diese Zeile setzt. */
    g_score_last_shown = 0xFFFFu;
}
// --- HUD-Bar: Paletten + Tilesets installieren (einmalig pro game_start) --
static void build_bar_assets(void) {
    SetPalette(SCR_2_PLANE, 1, 0x0000, 0x0A77, 0x0422, 0x0644); /* cool  */
    SetPalette(SCR_2_PLANE, 2, 0x0000, 0x0249, 0x0028, 0x0004); /* warm  */
    SetPalette(SCR_2_PLANE, 3, 0x0000, 0x0A77, 0x0422, 0x009D); /* life  */
    /* Palette 4: Salmon + cool-Farben fuer Bar+Salmon-Kombinationstiles */
    SetPalette(SCR_2_PLANE, 4, 0x0000, 0x0ECE, 0x0644, 0x0A77);
    InstallTileSetAt(gfxBar, (u16)(14*8), TILE_BAR_BASE);
}

/* ============ Bar-Sub-Pixel-Verschiebung ============
   Damit die Bar bildschirmfest wirkt statt bei jeder 8px-Ringzeilen-Grenze zu
   "huepfen", werden pro Sub-Pixel-Versatz dy (1-7) vorgerechnete Tile-Varianten
   verwendet ??? keine Character-RAM-Schreibzugriffe zur Laufzeit im Frame-Loop
   (nur Map-Word-Writes, wie ueberall sonst auch).
   Warum ZWEI Varianten pro Design (nicht nur 7 St??ck "0..7 verschoben"): bei
   dy>0 liegt das 8-Zeilen-Fenster der Bar auf ZWEI physischen Ringzeilen
   gleichzeitig ??? die "verlassende" Zeile zeigt noch die unteren (8-dy) Zeilen
   des Bildes, die "ankommende" Zeile zeigt die oberen dy Zeilen. Ein einzelner
   Satz wuerde nur eine Haelfte davon abdecken.
   Nur 9 echte Designs noetig, nicht 10: Design 13 ("H-Flip+Salmon") ist Design
   10 zeilenweise gespiegelt (Pixel-Daten verifiziert) ??? Spalte 19 nutzt daher
   Design 10 mit Hardware-Flip (put_cell) statt einem eigenen Tile-Satz.
   Ziffern sind KEINE BG-Tiles mehr, sondern Sprites (score_draw) ??? ihre 140
   Shift-Varianten sind entfallen, nur die 126 Rahmen-Varianten bleiben.
   VRAM-Quellen (in dieser Reihenfolge aufgefuellt):
   135-136, 162-176, 243-252 (regulaer frei ??? NICHT 162-232 komplett, ab 177
   gehoert es den Schiff-Tilt-Sprites L1-R2, siehe ship.h!), dann ab
   LVL1_VRAM_BASE+LVL1_FIXED_ZONE_COUNT (254+101=355) ??? dieser Startpunkt ist
   FEST, unabhaengig davon, wie gross das aktuell geladene Terrain-Segment
   wirklich ist (siehe LVL1_FIXED_ZONE_COUNT-Kommentar oben). Fuellstand:
   27 + 99 = 126 -> belegt bis Tile 453. Der Sprite-Tile-Pool (siehe
   spr_tile_vram_init) liegt danach ab 454 ??? bleibt mit Marge entfernt.
   Font-Zone 69-127 (durch font_compact_init() zurueckgewonnen) gehoert seit
   dem 2-Segment-Umbau dem Terrain (g_lvl_vram), nicht mehr der Bar. */
/* bar_shift-Subsystem entfernt 2026-07-19 (Code-Leiche): am 18.07. durch den
   MicroDMA-Raster-Split ersetzt, seither kein Live-Aufruf mehr. */

/* ============ Terrain-Kollision (Xenon-2-Stil: Waende blockieren) ============
   Punkt-Test in Screen-Koordinaten: liegt an (x,y) ein solides Map-Element?
   Der u8-Wrap von (y + g_scr1_y) ist beabsichtigt: 32 Ringzeilen x 8px = 256.
   Getestet wird gegen die MAP-DATEN (lvl_map via g_row_map), nicht gegen den
   VRAM-Inhalt ??? dadurch zaehlen auch animierte Zellen automatisch als solide.
   Ausnahme: verwelkte Map-Objekt-Zellen (Bit 7 in g_mapobj_grid) ??? lvl_map
   selbst aendert sich beim Verwelken NICHT (ROM-Daten), ohne diese Ausnahme
   waeren abgeschossene Blumen fuer immer eine unsichtbare Wand ("kann nicht
   durchfliegen"-Bug, gemeldet 10.07.2026).
   Nutzerkorrektur 10.07.2026 (2. Runde): lvl_tile_solid[] (1=Wand,
   0=ueberfliegbar, je Streifen-Tile-Nummer) lieferte lange nur Nullen (Tool
   hatte kein Wand-Markieren) ??? Fallback war "jede nicht-leere Zelle solide".
   Der Export enthaelt jetzt erstmals einen echten 1er-Wert -> das Tool hat
   das Feature, zurueck auf die eigentliche Regel: NUR was lvl_tile_solid
   explizit als Wand markiert, blockiert. */
static u8 terrain_solid(u8 x, u8 y) {
    u8 row, col;
    u16 map_row, n;
    row = (u8)((u8)(y + g_scr1_y) >> 3);
    if (row == g_bar_vrow) return 0u;   /* HUD-Zeile: Terrain dort ist ausgeblendet */
    map_row = g_row_map[row];
    if (map_row >= (u16)LVL_MAP_H) return 0u;
    col = (u8)(x >> 3);
    if (col >= (u8)LVL_MAP_W) return 0u;
    if (g_mapobj_grid[row][col] & MAPOBJ_WILT_BIT) return 0u;
    n = lvl_map[(u16)map_row * (u16)LVL_MAP_W + col] & 0x01FFu;
    if (n == 0u) return 0u;
    return lvl_tile_solid[n - 1u];
}

/* Schiff-Hitbox relativ zum 24x24-Sprite-Ursprung: x+4..x+19, y+2..y+21.
   Punktraster mit <=8px Abstand ??? kann kein 8px-Tile ueberspringen. */
static const u8 ship_hit_x[3] = { 4u, 12u, 19u };
static const u8 ship_hit_y[4] = { 2u, 9u, 16u, 21u };

static u8 ship_hits_terrain(u8 x, u8 y) {
    u8 i, j;
    for (j = 0u; j < 4u; j++)
        for (i = 0u; i < 3u; i++)
            if (terrain_solid((u8)(x + ship_hit_x[i]), (u8)(y + ship_hit_y[j])))
                return 1u;
    return 0u;
}

/* ============ Map-Objekte: Kollisions-Tests + Treffer-Reaktion (??13) ============
   Neu seit dem Map-Update 06.07.2026 (ANWEISUNG_Export_Integration.md Schritt 13).
   Anders als normales Terrain (das Schuesse jetzt ungehindert passieren, siehe
   bullets_update) sind Map-Objekte ("Blume" & Co.) die einzigen abschiessbaren
   Map-Tiles: ein Treffer (Schuss ODER Schiff) ersetzt dauerhaft alle Zellen
   des Objekts durch die vom Tile Analyzer gelieferte "verwelkte" Grafik
   (mapobj_apply_row, siehe weiter oben) und deaktiviert es (keine Kollision/
   Schaden mehr). Normale Darstellung + Animation laufen weiter ueber das
   bestehende Map-Animations-System (anim_update/lvl_anim_*) ??? dafuer ist
   kein Extra-Code noetig, das Objekt ist einfach eine ganz normale animierte
   Zelle in lvl_map, bis sie "verwelkt" wird.
   Bullet_on_loop (Objekt schiesst selbst, S99) ist NICHT implementiert.
   Gruppen-Belohnungen (Schritt 14) sind seit dem Belohnungssystem-Update
   18.07.2026 AKTIV: das Tool definiert Belohnungen fuer die Gruppen 1..141
   (lvl_group_reward_kind/value; kind 0=Punkte, 2=Power, 3=Waffe; spr weiterhin
   0x0000 = kein Belohnungs-Icon), Code laeuft ueber spawn_killed ->
   pickup_spawn_for_group -> apply_pickup (GROUP_COUNT_SLOTS auf 142 angehoben,
   sonst Overflow bei Gruppe >31). */

/* Testet einen Bildschirmpunkt gegen alle NOCH INTAKTEN Map-Objekte ??? reiner
   O(1)-Grid-Lookup (g_mapobj_grid, siehe oben), Ringzeile wie terrain_solid.
   Liefert den Objekt-Index oder MAPOBJ_NONE. */
static u8 mapobj_hit_test(u8 x, u8 y) {
    u8 row, col, g;
    row = (u8)((u8)(y + g_scr1_y) >> 3);
    if (row == g_bar_vrow) return MAPOBJ_NONE;
    col = (u8)(x >> 3);
    if (col >= (u8)LVL_MAP_W) return MAPOBJ_NONE;
    g = (u8)(g_mapobj_grid[row][col] & (u8)~MAPOBJ_WILT_BIT);
    return g ? (u8)(g - 1u) : MAPOBJ_NONE;
}

/* Schiff-Punktraster (wie ship_hits_terrain) gegen Map-Objekte. */
static u8 ship_hits_mapobj(u8 x, u8 y) {
    u8 i, j, obj;
    for (j = 0u; j < 4u; j++)
        for (i = 0u; i < 3u; i++) {
            obj = mapobj_hit_test((u8)(x + ship_hit_x[i]), (u8)(y + ship_hit_y[j]));
            if (obj != MAPOBJ_NONE) return obj;
        }
    return MAPOBJ_NONE;
}

/* Objekt "verwelken": einmalig, deaktiviert Kollision/Schaden dauerhaft und
   zeichnet alle gerade sichtbaren Zellen sofort um (unsichtbare Zellen holen
   sich die Wilt-Grafik automatisch beim naechsten lvl1_put_row-Aufruf ab,
   z.B. wenn die Zeile durch Rueckwaertsflug wieder ins Bild kommt). */
static void mapobj_wilt(u8 obj_idx) {
    u16 off;
    u8 count, i;
    if (g_mapobj_wilted[obj_idx]) return;
    g_mapobj_wilted[obj_idx] = 1u;
    off   = lvl_mapobj_cell_off[obj_idx];
    count = lvl_mapobj_cell_count[obj_idx];
    for (i = 0u; i < count; i++) {
        u8 map_row  = (u8)lvl_mapobj_cell_row[off + i];
        u8 ring_row = find_ring_row(map_row);
        if (ring_row != MAPOBJ_NONE) mapobj_apply_row(map_row, ring_row);
    }
}

/* ============ Energie und Schaden (22.07.2026) ============
   Bis hierher kostete JEDER Treffer sofort ein ganzes Leben - im Original hat das
   Schiff dagegen einen Energievorrat von 39, ein Gegnerschuss zieht 4 ab, ein
   Zusammenstoss 8 (16 bei den zaehen Typen), und erst bei 0 geht ein Leben
   verloren (damage.md ??1/??2/??3). Man uebersteht dort also rund neun Schuesse pro
   Leben statt einen. Werte 1:1 aus map.h (LVL_SHIP_START_ENERGY/
   LVL_ENEMY_SHOT_DAMAGE/LVL_ENEMY_CONTACT_DAMAGE), die dort schon exportiert
   waren und bis heute niemand gelesen hat.

   Zum Unverwundbarkeitsfenster: das Original hat KEINES. Die bisherigen 90 Frames
   waren die stillschweigende Kompensation dafuer, dass ein Treffer ein ganzes
   Leben kostete. Mit echter Energie wird daraus ein Problem - 3 s Immunitaet pro
   4 Schaden macht das Schiff nahezu unverwundbar. Deshalb jetzt zweigeteilt:
   ein kurzes Fenster gegen Mehrfachschaden im selben Moment (PLAYER_HIT_IFRAMES),
   und die vollen 90 Frames nur noch beim tatsaechlichen Lebensverlust, wo sie den
   Wiedereinstieg abdecken. */
#define PLAYER_MAX_ENERGY     39u   /* LVL_SHIP_START_ENERGY */
#define DMG_ENEMY_SHOT         4u   /* LVL_ENEMY_SHOT_DAMAGE */
#define DMG_ENEMY_CONTACT      8u   /* LVL_ENEMY_CONTACT_DAMAGE */
#define DMG_ENEMY_CONTACT_TOUGH 16u /* dito, zaehe Gegner (lvl_spawn_health >= 2) */
#define PLAYER_HIT_IFRAMES    12u   /* kurz: verhindert nur Mehrfachschaden im selben Moment */

/* 22.07.2026: letzte Schadensquelle, nur fuer die Telemetrie/Diagnose.
   1=Gegnerschuss 2=Map-Objekt-Schuss 3=Gegner-Kontakt 4=Meta-Kontakt
   5=Wurm-Kontakt 6=Wandwurm-Kontakt 7=Map-Gewaechs-Kontakt */
static u8 g_dmg_src;
/* Der gelbe Balken in der HUD-Leiste (Zellen 10-14) zeigte bisher die LEBEN -
   eine Zelle je Leben. Mit dem Energiesystem vom 22.07. erwartet man dort aber
   die ENERGIE (Nutzer: "die gelbe Lebensleiste zeigt die Energie nicht richtig
   an"), und ein Balken sieht auch danach aus. Fuenf Zellen, Zelle i leuchtet,
   solange die Energie ueber i*8 liegt - bei voll 39 alle fuenf, bei 8 noch eine.
   Gezeichnet wird nur angefordert; bar_redraw_flush() macht es am Frame-Ende
   (HUD-Schreibzugriffe nie mitten im Frame, siehe ??7d). */
/* Laufzeit-Unsterblichkeit, mit OPTION umgeschaltet (Nutzerwunsch 22.07.2026).
   Bewusst eine echte Variable statt eines Defines: so laesst sich derselbe Build
   mit und ohne Unsterblichkeit testen - besonders fuer die Checkpoints, die sich
   mit gesetztem GOD_MODE ueberhaupt nicht ausloesen lassen. */
static u8 g_god;
static u16 g_dbg_shots;   /* Zaehler abgefeuerter Hauptkanonen-Schuesse (nur Telemetrie) */
static u16 g_dbg_area;    /* Zaehler Flaechenschaden-Treffer (Bomb/Mine, nur Telemetrie) */

static void bar_set_energy(void) {
    u8 c, lit;
    for (c = 0u; c < 5u; c++) {
        lit = (u8)(g_player.energy > (u8)(c * 8u));
        g_bar_col[(u8)(10u + c)] = (u8)(lit ? 9u : 2u);
    }
    if (g_bar_redraw == 0u) g_bar_redraw = 1u;
}

static void player_damage(u8 dmg) {
#if WORM_TEST_MODE || GOD_MODE
    (void)dmg;
    return;   /* Schiff unsterblich (WORM_TEST_MODE oder GOD_MODE) */
#endif
    /* 22.07.2026, Nutzerwunsch: OPTION schaltet die Unsterblichkeit im laufenden
       Spiel um (Testhilfe) - kein Neubau mit gesetztem GOD_MODE mehr noetig.
       Umgeschaltet wird in der Hauptschleife, siehe g_god. */
    if (g_god) return;
    /* 23.07.2026: Der Boss-Cash-Regen macht den Spieler unverwundbar, damit er
       das Belohnungsfenster gefahrlos abraeumen kann (Original 1000:3c56). */
    if (g_boss_rain) return;
    if (g_player.inv_cd > 0) return;
    if (g_player.energy > dmg) {
        /* Treffer eingesteckt, Leben bleibt */
        g_player.energy = (u8)(g_player.energy - dmg);
        g_player.inv_cd = (u8)PLAYER_HIT_IFRAMES;
        g_hit_flash = (u8)HIT_FLASH_FRAMES;   /* 23.07.2026: Treffer-Flash Schiff+Waffen, siehe FLASH_PAL / Schiff-Flash im Draw */
        bar_set_energy();   /* 22.07.2026: Balken zeigt die Energie */
        return;
    }
    /* Energie aufgebraucht -> Leben verloren */
    g_player.energy = (u8)PLAYER_MAX_ENERGY;
    bar_set_energy();
    if (g_player.lives > 0) {
        g_player.lives--;
        /* BUGFIX 22.07.2026 (Nutzerbericht "die Lebensanzeige passt nicht zum
           echten Leben"): hier stand ein direktes bar_draw_at(). Das schreibt die
           HUD-Tilemap MITTEN IM FRAME - genau das, was CLAUDE.md ??7d als Ursache
           des alten Bar-Flackerns festhaelt ("HUD-Schreibzugriffe NUR am
           Frame-Ende, nie mitten im Frame"). Auf echter Hardware kann der Beam
           die Zeile dabei schon lesen, und wenn sich g_bar_vrow im selben Frame
           noch verschiebt, landet die Aenderung auf der falschen Ringzeile - die
           Anzeige zeigt dann einen anderen Stand als g_player.lives.
           Jetzt nur noch anfordern; bar_redraw_flush() zeichnet am Frame-Ende. */
        if (g_bar_redraw == 0u) g_bar_redraw = 1u;
    }
    g_player.inv_cd = 90;
    g_hit_flash = (u8)HIT_FLASH_FRAMES;   /* 23.07.2026: Treffer-Flash (siehe oben) */
    /* Punkt 4 (21.07.2026): frueher flog das Schiff nach einem Lebensverlust einfach
       weiter - es gab ueberhaupt keinen Wiedereinstieg. Jetzt zurueck zum zuletzt
       passierten Checkpoint. Nur das Flag setzen; der eigentliche Aufbau laeuft am
       FRAME-ENDE (respawn_do), weil er Terrain neu hochlaedt. */
    if (g_player.lives > 0) g_respawn_pending = 1u;
}

/* Kontaktschaden fuer einen Gegner dieser Welle: die zaehen Typen (doppelte HP
   im Tool) mahlen im Original 16 statt 8 (damage.md ??3). */
static u8 contact_damage_for(u8 spawn_idx) {
    return (u8)((lvl_spawn_health[spawn_idx] >= 2u) ? DMG_ENEMY_CONTACT_TOUGH
                                                    : DMG_ENEMY_CONTACT);
}

/* Altname beibehalten, damit die Aufrufstellen ohne eigene Schadensangabe
   (Wandwuermer, Map-Gewaechse) unveraendert bleiben: Standard = Kontakt. */
static void player_hit(void) { player_damage((u8)DMG_ENEMY_CONTACT); }

/* Schritt 4 (ANWEISUNG_Export_Integration.md): Zeilenbereiche, in denen das
   normale Rueckwaerts-Limit (g_back_budget/g_back_px) ausgesetzt ist ??? z.B.
   ein Shop-Abschnitt, in dem frei hin- und zurueckgeflogen werden darf.
   row ist ein Naeherungswert der aktuellen Zeile (g_scroll_y>>3, reiner
   Vorwaerts-Distanz-Zaehler seit Levelstart, 8px/Zeile). WICHTIG: die
   Ring-Historie haelt trotzdem nur ~12 Zeilen echtes Terrain vor (siehe
   Kommentar bei scroll_back_px) ??? bei den beiden 19 Zeilen breiten Zonen
   (86-104, 274-292) kann ganz an den Zonenrand zurueckfliegen daher
   trotzdem kurz Muell zeigen, das ist eine Ring-Puffer-Grenze, keine
   Free-Zone-Grenze. */
static u8 in_free_zone(u16 row) {
    u8 i;
    for (i = 0u; i < (u8)LVL_FREE_ZONE_COUNT; i++)
        if (row >= lvl_free_zone_row_from[i] && row <= lvl_free_zone_row_to[i]) return 1u;
    return 0u;
}

/* Einen Pixel rueckwaerts scrollen (Level laeuft zurueck, Inhalt wandert
   hoch). Die Ringzeilen unterhalb der Bar enthalten noch das Terrain der
   letzten ~13 Zeilen ??? beim Wieder-Auftauchen laeuft jede Zeile durch den
   Bar-Relocate (restore_terrain_row), daher ist kein eigenes Rueckwaerts-
   Streaming noetig. Nur der Vorwaerts-Streaming-Zeiger g_lvl_row muss pro
   ueberquerter Tile-Grenze zurueckgedreht werden, damit beim naechsten
   Vorwaertsflug dieselben Zeilen erneut geladen werden. */
static void scroll_back_px(void) {
    u8 new_bar_vrow;
    u8 in_free = in_free_zone((u16)(g_scroll_y >> 3));
    if (!in_free && g_back_px == 0u) return;   /* Ring-Historie erschoepft bzw. Levelanfang */
    g_scr1_y++;
    /* Wand naehert sich von unten? -> blockiert, Schritt zuruecknehmen */
    if (ship_hits_terrain(g_player.x, g_player.y)) { g_scr1_y--; return; }
    if ((g_scr1_y & 7u) == 0u) {
        if (g_lvl_row > 19u) {
            g_lvl_row--;   /* oberste Zeile ist rausgescrollt */
            /* RUECKWAERTS-STREAMING (2026-07-19, Nutzerbericht "Map weg beim
               Zurueckfliegen"): symmetrisch zum Vorwaerts-Fall (scroll_update
               laedt oben g_lvl_row an next_top). Beim Zurueckfliegen rollt unten
               ??? direkt ueber dem Bar-Frozen-Strip (Screen ~136) ??? die Zeile
               g_lvl_row-18 wieder ein. Ohne Nachladen blieb dort das alte/leere
               Terrain der laengst ueberschriebenen Ringzeile stehen. Ringposition
               wie das Terrain zeichnet: ((g_scr1_y>>3)+17)&31. lvl1_put_row liest
               aus den vollen Map-Daten (lvl_map), aktualisiert g_row_map + Kollision. */
            lvl1_put_row((u16)(g_lvl_row - 18u), (u8)(((g_scr1_y >> 3) + 17u) & 31u));
        }
        else { g_scr1_y--; return; }        /* Levelanfang: nicht weiter zurueck */
    }
    if (!in_free) g_back_px--;   /* in der Free-Zone (Schritt 4) nicht verbrauchen */
    if (g_scroll_y > 0u) g_scroll_y--;
    new_bar_vrow = (u8)((((((u16)144u + (u16)g_scr1_y) >> 3) + 1u)) & 31u);   /* +1: Bar-Ringzeile eine tiefer (off-screen im Terrain-Scroll) -> kein Ghost bei festem Split */
    if (new_bar_vrow != g_bar_vrow) {
        /* NICHT mehr hier (mitten im Frame) zeichnen ??? identisch zum Vorwaerts-Pfad
           in scroll_update(). Bis 20.07.2026 stand hier restore_terrain_row() +
           bar_draw_at() + Strip-Clear DIREKT: die DMA-Split-Tabelle des laufenden
           Frames zeigt aber noch auf die ALTE Bar-Ringzeile, und restore_terrain_row
           schreibt genau dort Terrain hinein, waehrend der Beam sie liest -> Bar fuer
           1-2 Frames komplett weg (Emulator: 1x pro 8px-Zeilenwechsel, exakt 64 Frames
           Abstand; auf echter HW als Dauerflackern sichtbar, Nutzer 20.07.).
           Jetzt nur Flag setzen, gezeichnet wird am FRAME-ENDE in bar_redraw_flush()
           (Fall 2 = rueckwaerts: restore_terrain_row(old) + bar_draw_at(neu)).
           Der Strip-Clear entfaellt ersatzlos ??? beim festen Split ist g_bar_vrow-1
           sichtbares Terrain bei y=143 und darf nicht geleert werden (siehe
           Kommentar in bar_redraw_flush); ausserdem loeschte er genau die Zeile,
           die restore_terrain_row eine Zeile vorher wiederhergestellt hatte. */
        g_bar_redraw_old = g_bar_vrow;
        g_bar_vrow       = new_bar_vrow;
        g_bar_redraw     = 2u;
    }
    /* kein per-Tick bar_shift_update mehr ??? Bar nur bei Zeilenwechsel gezeichnet */
}

// --- Hintergrund scrollen --
static void scroll_update(void) {
    u8 next_top;
#if TEST_MANUAL_SCROLL
    u8 fwd = (g_pad & J_UP)   ? 1u : 0u;
    u8 bwd = (!fwd && (g_pad & J_DOWN)) ? 1u : 0u;

    if (!fwd && !bwd) return;   /* TEST-Modus: ohne Eingabe steht die Map still */
    g_scroll_tick++;
    if (g_scroll_tick >= 2u) {   /* zuegiges Tempo zum Abfahren */
        u8 new_bar_vrow;

        g_scroll_tick = 0u;
        if (fwd) { g_scr1_y--; g_scroll_y++; }
        else     { g_scr1_y++; }   /* Rueckwaerts: nur begrenzt korrekt, siehe Hinweis unten */

        /* Neue Tile-Reihe einladen wenn Tile-Grenze ueberquert (nur vorwaerts;
           rueckwaerts verlaesst man sich auf die noch im Ring vorhandenen,
           zuletzt geschriebenen Zeilen ??? reicht fuer ein kurzes Stueck zurueck,
           nicht fuer das ganze Level). */
        if (fwd && (g_scr1_y & 7u) == 7u) {
#else
    /* ---- Rueckwaertsflug: RUNTER am unteren Limit gedrueckt + Budget da ----
       Traeger Gegenschub: Tempo beschleunigt langsam von 0 auf BACK_SPEED_MAX
       (alle BACK_ACCEL_FRAMES Frames +1/32 px/f) ??? als wuerde man gegen den
       Scroll-Strom arbeiten. Loslassen setzt den Anlauf zurueck. Waehrenddessen
       pausiert der Vorwaertsscroll (fruehes return, Tick-Zaehler friert ein).
       Budget laedt NUR auf, wenn der Trigger nicht anliegt ??? sonst wuerde
       Runter-Halten mit leerem Budget in einen Restgeschwindigkeits-Dauerflug
       kippen. */
    {
        u8 back_req = (u8)((g_pad & J_DOWN) && !(g_pad & J_UP) &&
                           g_player.y >= (u8)PLAYER_Y_MAX);
        /* Schritt 4: in einer Free-Zone (lvl_free_zone_row_*) ist das
           Zeitbudget ausgesetzt ??? back_req reicht allein, kein Verbrauch. */
        u8 in_free  = in_free_zone((u16)(g_scroll_y >> 3));
        if (back_req && (in_free || g_back_budget > 0u)) {
            if (!in_free) g_back_budget--;
            if (g_back_speed < (u8)BACK_SPEED_MAX) {
                g_back_accel_tick++;
                if (g_back_accel_tick >= (u8)BACK_ACCEL_FRAMES) {
                    g_back_accel_tick = 0u;
                    g_back_speed++;
                }
            }
            g_back_acc = (u8)(g_back_acc + g_back_speed);
            if (g_back_acc >= 32u) {
                g_back_acc -= 32u;
                scroll_back_px();
            }
            return;
        }
        g_back_speed      = 0u;   /* Anlauf beginnt beim naechsten Mal von vorn */
        g_back_accel_tick = 0u;
        g_back_acc        = 0u;
        if (!back_req && g_back_budget < (u8)BACK_BUDGET_MAX) g_back_budget++;
    }

    g_scroll_tick++;
    if (g_scroll_tick >= g_tune_scroll) {
        u8 new_bar_vrow;
        u8 drifted;

        g_scroll_tick = 0u;
        g_scr1_y--;

        /* Schritt 4: ab LVL_SCROLL_END_ROW keine weitere Vorwaerts-Bewegung
           mehr ??? Kamera haelt hart an (z.B. Boss-Arena/Levelende), statt wie
           bisher einfach in leere Zeilen weiterzuscrollen.
           Nutzerkorrektur 10.07.2026 (2. Runde): urspruenglich gegen
           g_lvl_row geprueft (naechste NEUE Zeile, die oben einrollt) ??? der
           liegt aber immer ~19 Zeilen (Bildschirmhoehe) VOR der Zeile, die
           unten beim Schiff sichtbar ist, UND plateauiert bei LVL_MAP_H
           sobald durchgestreamt (wuerde die Bedingung nie mehr erreichen).
           Stattdessen gegen g_scroll_y (reiner Vorwaerts-Distanz-Zaehler in
           Pixeln seit Levelstart) pruefen: friert erst ein, wenn die
           Endzeile tatsaechlich die Bildschirmhoehe durchlaufen hat (unten
           in Spielposition angekommen ist), nicht schon beim ersten
           Einrollen oben. */
        /* KORREKTUR 22.07.2026 (Nutzerhinweis): LVL_SCROLL_END_ROW ist die LETZTE
           Kachelzeile der Map (hier 292 bei LVL_MAP_H 293) und muss als OBERSTE
           Bildschirmzeile stehenbleiben - sie ist ja die letzte, die oben
           einrollt. g_scroll_y zaehlt aber die UNTERSTE sichtbare Zeile (x8,
           siehe lvl1_prefill: row_base liegt unten auf ty=18, row_base+18 oben
           auf ty=0). Der Test verglich beides direkt und deckelte deshalb erst,
           wenn Zeile 292 unten angekommen war - also 18 Zeilen zu spaet, mit
           komplett leerem Bild und einem Boss weit unter dem Rand. */
#if BENCH_NOKILL
        /* Benchmark: im Wurm-Band stehen bleiben (wie der End-Stop unten). */
        if (g_scroll_y >= (u16)((u16)BENCH_HOLD_ROW * 8u)) {
            g_scr1_y++; g_scroll_tick = g_tune_scroll; return;
        }
#endif
        if (g_scroll_y >= (u16)((u16)LVL_SCROLL_END_TOP_ROW * 8u)) {
            /* REGRESSION 21.07.2026, korrigiert 22.07.2026 (im Emulator an der
               Boss-Arena nachgestellt): hier stand urspruenglich g_scr1_y++, das
               wurde als "zaehlt das Scrollregister weiter hoch" gelesen und
               entfernt. Genau umgekehrt: der Vorwaerts-Scroll DEKREMENTIERT
               (g_scr1_y-- weiter oben, noch vor diesem Test), das ++ war dessen
               RUECKNAHME - dasselbe Idiom wie im Terrain-blockiert-Zweig gleich
               darunter. Ohne es lief das Hardware-Register weiter, waehrend
               Zeilen-Streaming und Bar-Tracking (beide hinter dem return)
               uebersprungen wurden: der Boss scrollte unten aus dem Bild, der
               32-Zeilen-Ring wickelte sich um und die Bar-Ringzeile erschien
               dabei wieder und wieder ueber den ganzen Schirm. */
            g_scr1_y++;
            g_scroll_tick = g_tune_scroll;
            return;
        }

        /* Drift ohne Tasteneingabe (gleiche Rate wie Scroll) */
        drifted = 0u;
        if (!(g_pad & (J_UP | J_DOWN | J_LEFT | J_RIGHT))) {
            if (g_player.y < PLAYER_Y_MAX) {
                g_player.y++;
                drifted = 1u;
            }
        }

        /* Wand im Weg? Erst versuchen, das Schiff 1px MIT nach unten zu
           schieben ??? wer an einer Wand haengt (z.B. Hoch gedrueckt unter
           einem Vorsprung), wird vom Scroll einfach mitgenommen, das Level
           laeuft weiter. NUR wenn auch unten kein Platz ist (eingeklemmt
           zwischen Wand und Bewegungslimit/Boden), haelt der Scroll an ???
           ohne Schaden ??? und probiert es jeden Frame erneut. */
        if (ship_hits_terrain(g_player.x, g_player.y)) {
            u8 pushed = 0u;
            if (g_player.y < PLAYER_Y_MAX) {
                g_player.y++;
                pushed = 1u;
                if (ship_hits_terrain(g_player.x, g_player.y)) {
                    g_player.y--;
                    pushed = 0u;
                }
            }
            if (!pushed) {
                g_scr1_y++;
                if (drifted) g_player.y--;
                g_scroll_tick = g_tune_scroll;
                return;
            }
        }
        g_scroll_y++;
        if (g_back_px < (u8)BACK_PX_MAX) g_back_px++;   /* Rueckwaerts-Historie waechst mit */

        /* Neue Tile-Reihe einladen wenn Tile-Grenze ueberquert */
        if ((g_scr1_y & 7u) == 7u) {
#endif
            /* Ringzeile, die JETZT am oberen Bildschirmrand (Scanline 0) erscheint:
               (g_scr1_y+0)>>3 & 31 ??? OHNE "-1". Die alte Formel hatte einen Off-by-
               one: sie schrieb die neue Zeile eine Ringzeile zu frueh, wodurch genau
               die Ringzeile, die als naechstes wirklich oben einrollt, leer blieb ???
               sichtbar als Luecke zwischen Map-Zeile 18 und 19. */
            next_top = (u8)((u8)(g_scr1_y >> 3) & 31u);
            /* Segment-A-Budget reicht nicht fuer den Rest der Map ??? bei Zeile
               LVL1_SEG_SWITCH_ROW pausieren statt weiterzustreamen, siehe
               shop_enter()/shop_resume(). Nur beim ERSTEN Durchlauf (Segment
               A), Segment B laeuft bis LVL_MAP_H frei durch. */
            /* Shop-Ausloeser (21.07.2026 entkoppelt): hing fest an
               LVL1_SEG_SWITCH_ROW, weil das Terrain-Budget den Segmentwechsel nur
               dort erlaubte und der Shop ihn gleich miterledigte. Seit dem
               abschnittsweisen Nachladen ist der Shop frei platzierbar - die Zeile
               kommt jetzt aus den Tool-Daten (lvl_shop_trigger_row[0]; das Original
               liegt auf 125, aus XENON2.EXE 1000:656e bestaetigt). Das Terrain muss
               dafuer nicht mehr anhalten. */
            /* 22.07.2026: nur noch scharf schalten - der Wechsel nach STATE_SHOP
               passiert in shop_delay_tick() am Frame-Ende (shop_enter() schreibt
               VRAM). Damit greifen ALLE Ausloeser aus der Tabelle, nicht nur der
               erste, und die Verzoegerung wird beachtet. */
            /* VERSATZ-KORREKTUR 22.07.2026 (Nutzerfrage "kommt der Shop nicht zu
               frueh?"): hier stand g_lvl_row - das ist die Zeile, die gerade OBEN
               einrollt, und die laeuft dem Schiff immer 19 Zeilen voraus. Der Shop
               feuerte damit schon, wenn der Spieler erst bei Zeile 106 war, also
               gut 15 s zu frueh. Dieselbe Verwechslung wie bei LVL_SCROLL_END_ROW.
               Die Ausloesezeile aus dem Tool meint die SPIELERposition, und die
               ist g_scroll_y>>3 (unterste sichtbare Zeile). */
            shop_trigger_check(0u, (u16)(g_scroll_y >> 3));
            spr_sec_check((u16)(g_scroll_y >> 3));   /* 30.07.2026: Sprite-Abschnitt vorbereiten */
            if (g_lvl_row < (u16)LVL_MAP_H) {
                lvl1_put_row(g_lvl_row, next_top);
                g_lvl_row++;
                terr_sec_check(g_lvl_row);   /* Abschnittswechsel rechtzeitig anstossen */
                {   /* Punkt 4: zuletzt passierten Checkpoint merken.
                       VERSATZ-KORREKTUR 22.07.2026 (sechste Fundstelle, siehe den
                       Block am Dateikopf): hier stand g_lvl_row - die oben
                       einrollende Zeile, 19 vor dem Schiff. Ein Checkpoint galt
                       dadurch als passiert, bevor der Spieler ihn erreicht hatte,
                       und der Wiedereinstieg konnte bis zu 19 Zeilen VORWAERTS
                       springen - unter Umstaenden mitten in die Welle, an der man
                       gerade gestorben ist. Checkpoints sind ein Spiel-Ereignis,
                       also gegen die Spielerzeile pruefen. */
                    u8 cp, newcp = g_checkpoint;
                    u16 prow = (u16)(g_scroll_y >> 3);
                    for (cp = 0u; cp < (u8)LVL1_CHECKPOINT_COUNT; cp++)
                        if (prow >= lvl1_checkpoint_row[cp]) newcp = cp;
                    /* prow ist ein reiner Vorwaerts-Distanzzaehler -> newcp kann
                       nie zurueckfallen. Nur beim ERSTEN Erreichen eines neuen
                       Checkpoints (oder ueberhaupt des ersten) den Loadout-/Geld-
                       Stand sichern (s0.4e). */
                    if (!g_checkpoint_seen || newcp != g_checkpoint) {
                        g_checkpoint = newcp; g_checkpoint_seen = 1u;
                        g_cp_cash    = g_cash;
                        /* 23.07.2026: waehrend Nashwan das Loadout NICHT sichern
                           (DOS 1000:35eb) - sonst wuerde das temporaere Kit als
                           Checkpoint-Stand haengenbleiben. */
                        if (g_nashwan_timer == 0u) {
                            g_cp_weapons = g_player.weapons_active;
                            g_cp_power   = g_player.power_stage;
                        }
                    }
                }
            } else {
                lvl1_put_row((u16)LVL_MAP_H, next_top);   /* Level-Ende: leere Zeile */
            }
        }

        /* Bar-Tracking: unabhaengig von Scroll-Richtung UND unabhaengig vom
           obigen Boundary-Check (der ist nur fuer next_top/vorwaerts gedacht) ???
           einfach jeden Tick neu berechnen und vergleichen. Frueher hing das mit
           im "fwd &&"-Zweig, wodurch g_bar_vrow beim Rueckwaertsfliegen (TEST-Modus)
           nie aktualisiert wurde und die Bar ueber den Bildschirm schmierte.
           Kein Separator mehr (entfernt) ??? die Zeile darueber ist jetzt einfach
           normales, unbehandeltes Terrain. */
        new_bar_vrow = (u8)((((((u16)144u + (u16)g_scr1_y) >> 3) + 1u)) & 31u);   /* +1: Bar-Ringzeile eine tiefer (off-screen im Terrain-Scroll) -> kein Ghost bei festem Split */
        if (new_bar_vrow != g_bar_vrow) {
            u8 old_row = g_bar_vrow;
            g_bar_vrow = new_bar_vrow;
            /* NICHT mehr hier (mitten im Frame) zeichnen ??? das war die Rest-Ursache
               des "zweite Bar blitzt auf": bar_draw_at/Strip-Clear schrieben die
               Bar-VRAM waehrend der Beam sie las. Stattdessen Flag setzen; das
               eigentliche Zeichnen macht bar_redraw_flush() am FRAME-ENDE (gleiches
               sichere Timing wie die Ziffern, die nie flackern). Vorwaerts (new =
               old-1) -> Strip g_bar_vrow-1 leeren; Rueckwaerts -> alte Zeile
               Terrain-Restore. */
            if (old_row == (u8)((new_bar_vrow + 1u) & 31u)) {
                g_bar_redraw = 1u;
            } else {
                g_bar_redraw = 2u;
                g_bar_redraw_old = old_row;
            }
        }
        /* KEIN per-Tick bar_shift_update mehr: mit dy=0 aendern sich die Bar-Kacheln
           nicht, das Neuzeichnen JEDEN Frame schrieb nur in die sichtbare Bar-VRAM
           waehrend der Beam sie las -> Flackern beim Scrollen (Nutzer-Diagnose
           17.07.2026: steht das Bild still, flackert nichts). Bar wird jetzt nur bei
           Zeilenwechsel (oben) bzw. Inhaltsaenderung (player_hit) via bar_draw_at
           gezeichnet. */
    }
}

// --- Spieler initialisieren --
static void player_init(void) {
    u8 w;
    g_player.x       = SCR_W / 2;
    g_player.y       = SCR_H - 36;
    g_player.fire_cd = 0;
    g_player.lives   = 5;
    g_player.energy  = (u8)PLAYER_MAX_ENERGY;   /* 22.07.2026 */
    g_player.inv_cd  = 0;
    g_player.firerate_stage = 0u;
    g_player.power_stage    = 0u;
    g_player.speed_stage    = 0u;
    g_player.weapons_active = 0u;
    for (w = 0u; w < (u8)LVL_WEAPON_COUNT; w++) g_player.weapon_cooldown[w] = 0u;
}

/* Tilt-Level (-2..+2) -> Index in lvl_meta_* (Reihenfolge im Export:
   0=Ship_L1 1=Ship_L2 2=Ship_M 3=Ship_R1 4=Ship_R2) */
static const u8 tilt_by_level[5] = {
    SHIP_META_L2, SHIP_META_L1, SHIP_META_M, SHIP_META_R1, SHIP_META_R2
};

/* Performance-Fix 11.07.2026 (Nutzerbericht: weiterhin Ruckler trotz
   Hitzonen-Caching): die Schiffs-Hitzone wurde zwar schon auf "einmal pro
   Kollisions-Durchlauf" reduziert (statt einmal je Gegner), aber jeder
   dieser Aufrufe loeste IMMER NOCH per linearer 17-Eintraege-Suche auf
   (hitzone_rect/hitzone_resolve) ??? zwei Aufrufe pro Frame (check_collisions
   + mapobj_bullets_update), 60x/Sekunde, macht 120 Suchen/Sekunde fuer nur
   5 MOEGLICHE Werte (eine Kippstufe 0-4, siehe tilt_by_level). Deshalb: alle
   5 Schiffs-Hitzonen EINMALIG bei game_start() vorausberechnen (siehe
   ship_hitzone_cache_init) und hier nur noch per Kippstufe indizieren ???
   keine Suche mehr im Hot Path, nur noch Array-Zugriff O(1). */
static s16 g_ship_hz_dx[5], g_ship_hz_dy[5];
static u8  g_ship_hz_w[5],  g_ship_hz_h[5];

static void ship_hitzone_cache_init(void) {
    u8 meta;
    for (meta = 0u; meta < 5u; meta++)
        hitzone_resolve((u16)(0x1000u | meta), SHIP_HIT_FALLBACK_W, SHIP_HIT_FALLBACK_H,
                         &g_ship_hz_dx[meta], &g_ship_hz_dy[meta],
                         &g_ship_hz_w[meta], &g_ship_hz_h[meta]);
}

/* Ersetzt ship_hitzone_spr()+hitzone_rect(): direkte O(1)-Tabellenabfrage
   statt linearer Suche, siehe Kommentar oben. */
static void ship_hitzone_rect(s16 *rx, s16 *ry, u8 *rw, u8 *rh) {
    u8 meta = tilt_by_level[g_tilt_level + 2];
    *rx = (s16)((s16)g_player.x + g_ship_hz_dx[meta]);
    *ry = (s16)((s16)g_player.y + g_ship_hz_dy[meta]);
    *rw = g_ship_hz_w[meta];
    *rh = g_ship_hz_h[meta];
}

// --- Spieler aktualisieren --
static void player_update(void) {
    u8 i, nx, ny, stuck;
    s8 tgt;

    /* Schiffstempo-Akku: 16-Basis (Nutzerkorrektur 11.07.2026, siehe
       PLAYER_SPEED_STEP-Kommentar), PLAYER_SPEED_STEP/16 Pixel/Frame als
       0/1-px-Schritte verteilt. g_move_step gilt fuer beide Achsen (der
       Rueckwaertsflug hat einen eigenen Akku mit halbem Tempo, siehe
       scroll_update). */
    g_move_acc  = (u8)(g_move_acc + g_tune_speed);
    g_move_step = (u8)(g_move_acc >> 4);
    g_move_acc &= 15u;

    /* Steckt das Schiff bereits in einer Wand (Ausnahmefall)?
       Dann Bewegung NICHT blockieren, damit man rausfliegen kann. */
    stuck = ship_hits_terrain(g_player.x, g_player.y);

    /* X-Bewegung, Waende blockieren, bis zu g_move_step Pixel pro Frame
       (Nutzerkorrektur 11.07.2026: 90px/s braucht g_move_step bis zu 2, siehe
       PLAYER_SPEED_STEP-Kommentar ??? deshalb geklemmte Addition/Subtraktion
       statt der alten reinen 1px-Schritte). Auslaufen (Nutzerwunsch
       11.07.2026): wird KEINE der beiden Richtungen gehalten, aber noch
       Auslauf-Budget uebrig, faehrt das Schiff in der zuletzt gehaltenen
       Richtung weiter, bis das Budget verbraucht ist ??? sofortiges Stehen-
       bleiben faellt so weniger abrupt aus. */
    nx = g_player.x;
    if (g_pad & J_LEFT) {
        g_last_dir_x = -1; g_coast_x = g_tune_coast;
        nx = (u8)((nx > (u8)(4u + g_move_step)) ? (nx - g_move_step) : 4u);
    }
    if (g_pad & J_RIGHT) {
        g_last_dir_x = 1; g_coast_x = g_tune_coast;
        nx = (u8)((nx < (u8)(SCR_W - 20u - g_move_step)) ? (nx + g_move_step) : (u8)(SCR_W - 20u));
    }
    if (!(g_pad & (J_LEFT | J_RIGHT)) && g_coast_x > 0) {
        g_coast_x--;
        if (g_last_dir_x < 0)
            nx = (u8)((nx > (u8)(4u + g_move_step)) ? (nx - g_move_step) : 4u);
        else if (g_last_dir_x > 0)
            nx = (u8)((nx < (u8)(SCR_W - 20u - g_move_step)) ? (nx + g_move_step) : (u8)(SCR_W - 20u));
    }
    if (nx != g_player.x && !stuck && ship_hits_terrain(nx, g_player.y))
        nx = g_player.x;
    g_player.x = nx;

    /* Y-Bewegung, Waende blockieren ??? Auslaufen/Mehrpixel-Schritte analog
       zur X-Achse. Obere Grenze der Y-Bewegung ist BAR_Y-24 (Schiffshoehe). */
    ny = g_player.y;
    if (g_pad & J_UP) {
        g_last_dir_y = -1; g_coast_y = g_tune_coast;
        ny = (u8)((ny > (u8)(4u + g_move_step)) ? (ny - g_move_step) : 4u);
    }
    if (g_pad & J_DOWN) {
        g_last_dir_y = 1; g_coast_y = g_tune_coast;
        ny = (u8)((ny < (u8)(BAR_Y - 24u - g_move_step)) ? (ny + g_move_step) : (u8)(BAR_Y - 24u - 1u));
    }
    if (!(g_pad & (J_UP | J_DOWN)) && g_coast_y > 0) {
        g_coast_y--;
        if (g_last_dir_y < 0)
            ny = (u8)((ny > (u8)(4u + g_move_step)) ? (ny - g_move_step) : 4u);
        else if (g_last_dir_y > 0)
            ny = (u8)((ny < (u8)(BAR_Y - 24u - g_move_step)) ? (ny + g_move_step) : (u8)(BAR_Y - 24u - 1u));
    }
    if (ny != g_player.y && !stuck && ship_hits_terrain(g_player.x, ny))
        ny = g_player.y;
    g_player.y = ny;

    /* Tilt: alle TILT_STEP_FRAMES eine Stufe Richtung Ziel. Ziel = Mitte,
       wenn KEINE oder BEIDE Richtungen gedrueckt sind. Der Rueckweg laeuft
       damit als Animation ueber die Zwischenstufe (L2->L1->M), gleiches
       Tempo wie vorwaerts. Solange das Level dem Ziel entspricht, bleibt
       der Timer "geprimed" ??? der erste Stufenwechsel nach einem
       Richtungswechsel kommt dadurch sofort (responsives Ansprechen). */
    tgt = 0;
    if ((g_pad & (J_LEFT | J_RIGHT)) == J_LEFT)       tgt = -2;
    else if ((g_pad & (J_LEFT | J_RIGHT)) == J_RIGHT) tgt = 2;

    if (g_tilt_level == tgt) {
        g_tilt_timer = g_tune_tilt;
    } else {
        g_tilt_timer++;
        if (g_tilt_timer >= g_tune_tilt) {
            g_tilt_timer = 0u;
            if (tgt > g_tilt_level) g_tilt_level++; else g_tilt_level--;
        }
    }

    if (g_player.fire_cd > 0) g_player.fire_cd--;

    /* Schnelles Tippen = schnelleres Feuern (17.07.2026, Nutzerwunsch).
       Eine frische Tastenflanke (g_pad_pressed) darf den Cooldown ueberspringen;
       Halten feuert weiter im normalen Takt (lvl_firerate_stage). Der Schuss-Pool
       (MAX_BULLETS=4) begrenzt das Mashen von selbst ??? mehr als 4 gleichzeitig
       geht nicht, es entsteht also keine Dauerfeuer-Luecke.
       ABWEICHUNG VOM ORIGINAL, bewusst: dort ist Tippen sogar SCHLECHTER. Laut
       timing.md pegt der Autofire-Zaehler bei losgelassener Taste auf die volle
       Periode und wird erst beim Halten heruntergezaehlt ??? "tapping fire with
       autofire 1 releases nothing". Wir machen hier absichtlich das Gegenteil. */
    if (((g_pad & J_A) && g_player.fire_cd == 0) || (g_pad_pressed & J_A)) {
        for (i = 0; i < MAX_BULLETS; i++) {
            if (!g_bullets[i].active) {
                g_bullets[i].active = 1;
                g_busy_bullets = 1u;   /* Frueh-Ausstieg: Flag an der Erzeugungsstelle */
                g_bullets[i].x      = (u8)(g_player.x + 8);
                g_bullets[i].y      = g_player.y;
                /* Feuerrate-Pickup (Schritt 15): Index 0 = Basis (10 Frames,
                   siehe lvl_firerate_stage) ??? 4 Bullet-Slots reichen weiterhin,
                   auch auf der schnellsten Stufe (4 Frames). */
                /* 30-fps-Cap 17.07.: Feuer-Cooldown halbieren ??? lvl_firerate_stage
                   ist in ROM (Frames), bei 30 statt 60 fps sonst halbe Schussrate.
                   Sauberer waere die Korrektur im Tool-Export; >>1 kompensiert es
                   hier ohne Re-Export. */
                g_player.fire_cd    = (u8)(lvl_firerate_stage[g_player.firerate_stage] >> 1);
                g_dbg_shots++;   /* Diagnose: trennt "kein Schuss entsteht" von "Schuss verschwindet sofort" */
                /* 25.07.2026 SCHUSS-SOUND. Im Original haben ALLE Waffen denselben
                   Schuss-Klang (SFX-ID 0, "shared shoot sfx", weapons.md:220) - die
                   waffenspezifischen Geraeusche des Remasters sind dort ausdruecklich
                   "port-only". Also EIN Sound fuer alles, hier an der Hauptkanone
                   ausgeloest (Modulschuesse fallen zeitlich zusammen). Kurzer
                   abfallender Sweep auf Kanal 2 (Harmonie - am wenigsten vermisst,
                   die Musik nimmt den Kanal danach wieder). */
                /* 25.07.2026 (Nutzer: "zu hoch im Klang und zu leise"): eine ganze
                   OKTAVE tiefer und lauter. divider ist die Tonperiode
                   (F = 96000/n), doppelte Periode = halbe Frequenz: Start
                   90 -> 180 (1067 Hz -> 533 Hz), Sweep-Ende 210 -> 420
                   (457 -> 229 Hz). sw_step 20 -> 40, damit der Abfall dieselben
                   sechs Schritte braucht - gleicher Rhythmus, nur tiefer.
                   attn 3 -> 1 (0 = lauteste Stufe) = rund 4 dB mehr. */
                Sfx_PlayToneEx(2u, 180u, 1u, 6u, 420u, 40, 1u, 0u, 1u, 1u, 3u, 1u);
                break;
            }
        }
    }

    if (g_player.inv_cd > 0) g_player.inv_cd--;
}

// --- Schuesse aktualisieren --
/* Schuesse fliegen ueber normales Terrain hinweg (Nutzerwunsch 06.07.2026 ???
   Sprites sollen generell ueber Map-Tiles fliegen); nur Map-Objekte (??13,
   "Blume" & Co., siehe mapobj_hit_test) sind abschiessbar und stoppen/
   zerstoeren den Schuss. */
/* Kein UnsetSprite() hier mehr: seit dem Schuss-Multiplexing (siehe
   SPR_BULLET_0-Kommentar) teilen sich mehrere logische Schuesse einen
   physischen Slot je nach g_flicker-Phase ??? welcher Slot bei Despawn
   "richtig" waere, haengt vom aktuellen Frame ab. draw_sprites() zeichnet
   ohnehin JEDEN Frame alle physischen Slots aus dem aktuellen .active-Stand
   neu (bzw. unsettet sie), das hier waere nur redundant und bei falscher
   Adressierung sogar irrefuehrend. */
/* Vorwaertsdeklarationen: der Endboss-Block steht weiter unten (er braucht
   enemy_fire), die Schusskollision hier oben muss ihn aber schon fragen. */
static u8   boss_seg_hit(u8 bx, u8 by);
static u8   boss_eye_hit(u8 bx, u8 by);
static void boss_take_damage(u8 dmg);
static u8   main_gun_damage(void);   /* steht weiter unten, wird hier schon gebraucht */

static void bullets_update(void) {
    u8 i;
    for (i = 0; i < MAX_BULLETS; i++) {
        s16 ny;
        if (!g_bullets[i].active) continue;
        g_busy_bullets = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
        /* Bugfix 17.07.2026: die alte Pruefung (y < 5 VOR der Bewegung) war nur bei
           4px/Frame sicher ??? bei 8px/Frame sprang der Schuss von y=7 auf -1, wrappte
           als u8 auf 255 (unterer Rand) und kreiste endlos ("Schuesse aus dem unteren
           Rand, feuern von selbst"). Jetzt vorzeichenbehaftet rechnen und ERST
           deaktivieren, dann setzen ??? schrittunabhaengig korrekt. */
#if TEST_BULLET_UPDATE_HALF
        ny = (s16)((s16)g_bullets[i].y - 16);   /* 2-Frame-Takt: doppelter Schritt */
#else
        ny = (s16)((s16)g_bullets[i].y - 8);
#endif
        if (ny < 5) {
            g_bullets[i].active = 0;
        } else {
            u8 obj;
            g_bullets[i].y = (u8)ny;
            obj = mapobj_hit_test((u8)(g_bullets[i].x + 4u), (u8)(g_bullets[i].y + 2u));
#if TEST_BULLET_UPDATE_HALF
            /* 16px-Doppelschritt: ZUSAETZLICH den Mittelpunkt der Strecke pruefen -
               Map-Objekt-Zellen sind nur 8 px hoch, ein reiner Endpunkt-Test
               wuerde jede zweite Zellzeile ueberspringen. */
            if (obj == MAPOBJ_NONE)
                obj = mapobj_hit_test((u8)(g_bullets[i].x + 4u), (u8)(g_bullets[i].y + 10u));
#endif
            if (obj != MAPOBJ_NONE) {
                mapobj_wilt(obj);
                g_bullets[i].active = 0;
            } else {
                /* Endboss (Schritt 20): die Tentakelglieder SCHLUCKEN den Schuss
                   (death 0x3a99 - Schuss weg, kein Schaden), verwundbar ist nur das
                   Augenfeld. Koerpertreffer fliegen glatt hindurch. */
                u8 bx = (u8)(g_bullets[i].x + 4u), by = (u8)(g_bullets[i].y + 2u);
#if TEST_BULLET_UPDATE_HALF
                /* Mittelpunkt der Strecke mitpruefen (Augenfeld ist 16 px hoch,
                   der 16er-Sprung koennte sonst exakt darueber hinwegsetzen). */
                if (boss_seg_hit(bx, by) || boss_seg_hit(bx, (u8)(by + 8u))) {
                    g_bullets[i].active = 0;
                } else if (boss_eye_hit(bx, by) || boss_eye_hit(bx, (u8)(by + 8u))) {
                    g_bullets[i].active = 0;
                    boss_take_damage(main_gun_damage());
                }
#else
                if (boss_seg_hit(bx, by)) {
                    g_bullets[i].active = 0;
                } else if (boss_eye_hit(bx, by)) {
                    g_bullets[i].active = 0;
                    boss_take_damage(main_gun_damage());
                }
#endif
            }
        }
    }
}

/* Map-Objekt-Schuesse (??13 bullet_on_loop): screen-absolute Bewegung wie ein
   Gegner-Pfad-Schritt (4.4-Fixed-Point, >>4 fuer die sichtbare Position ???
   siehe ANWEISUNG_Export_Integration.md Schritt 7, "haeufigster
   Integrationsfehler"). Despawnt ausserhalb des Spielfelds oder bei
   Schiffs-Treffer (immer Schaden, unabhaengig von lvl_mapobj_ship_damage ???
   das Flag gilt nur fuer direkten Objekt-KONTAKT, nicht fuer den Schuss).
   Fasst NICHT mehr direkt OAM an (kein UnsetSprite hier) ??? bei Flicker-
   Multiplexing (siehe draw_sprites) hat eine LOGISCHE Instanz keinen festen
   physischen Slot mehr, das Aufraeumen passiert dort zentral jeden Frame. */
static void mapobj_bullets_update(void) {
    u8 i;
    /* Performance-Fix 11.07.2026 (Nutzerbericht: extreme Slowdowns auf
       echter Hardware, wenn Blumen schiessen UND noch Gegner da sind):
       dieselbe Redundanz wie bei check_collisions() (siehe
       rects_overlap_cached-Kommentar), hier aber unentdeckt geblieben ???
       die Schiffs-Hitzone loeste bei JEDEM der bis zu 16 aktiven Blumen-
       Schuesse JEDEN Frame neu per linearer Suche auf, obwohl sich die
       Schiffs-Hitzone innerhalb eines Aufrufs nie aendert. Jetzt zusaetzlich
       ship_hitzone_rect() (O(1)-Tabelle statt Suche, siehe dortigen
       Kommentar) statt hitzone_rect()+ship_hitzone_spr(). NICHT das Flicker-
       Multiplexing (siehe draw_sprites) ??? das aendert nur, in welchen
       OAM-Slot geschrieben wird, nicht wie oft. */
    u8  ship_vuln = (u8)(g_player.inv_cd == 0u);
    s16 srx = 0, sry = 0;
    u8  srw = 0, srh = 0;
    /* Scroll-Versatz seit dem letzten Frame (Vorwaertsscroll: g_scr1_y SINKT, Welt
       wandert nach unten). (u8)(last-now) liefert den px-Vorwaertsschritt (0-7),
       ein Sprung (Segmentwechsel) wird verworfen. y_fix ist 4.4-Fixed -> *16. */
    u8  sdelta = (u8)(g_mo_last_scr1y - g_scr1_y);
    s16 soff;
    g_mo_last_scr1y = g_scr1_y;
    if (sdelta > 8u) sdelta = 0u;
    soff = (s16)((u16)sdelta << 4);
    if (ship_vuln)
        ship_hitzone_rect(&srx, &sry, &srw, &srh);
    for (i = 0u; i < (u8)MAX_MAPOBJ_BULLETS; i++) {
        s16 x, y;
        if (!g_mo_bullets[i].active) continue;
        g_busy_mobullets = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
        g_mo_bullets[i].x_fix = (s16)(g_mo_bullets[i].x_fix + g_mo_bullets[i].dx);
        g_mo_bullets[i].y_fix = (s16)(g_mo_bullets[i].y_fix + g_mo_bullets[i].dy + soff);   /* mit der Welt mitscrollen */
#if TEST_BULLET_UPDATE_HALF
        /* 2-Frame-Takt: zweiter Schritt im selben Aufruf (Doppel-Add statt
           Linksshift - nie eine negative Zahl shiften, cc900-Falle §6).
           OHNE soff: sdelta = (last - jetzt) akkumuliert beim 2-Frame-Takt
           bereits BEIDE Frames, der erste Add traegt den Scroll also schon
           vollstaendig - ein zweites soff waere doppelt gezaehlt. */
        g_mo_bullets[i].x_fix = (s16)(g_mo_bullets[i].x_fix + g_mo_bullets[i].dx);
        g_mo_bullets[i].y_fix = (s16)(g_mo_bullets[i].y_fix + g_mo_bullets[i].dy);
#endif
        x = (s16)(g_mo_bullets[i].x_fix >> 4);
        y = (s16)(g_mo_bullets[i].y_fix >> 4);
        if (x < 0 || x >= (s16)SCR_W || y < 0 || y >= (s16)BAR_Y) {
            mo_bullet_kill(i);   /* Slot SOFORT zurueck, siehe mo_bullet_kill (Geister-Sprite) */
            continue;
        }
        if (ship_vuln && rects_overlap(x, y, BULLET_HIT_W, BULLET_HIT_H, srx, sry, srw, srh)) {
            mo_bullet_kill(i);   /* dito */
            g_dmg_src=2u; player_damage((u8)DMG_ENEMY_SHOT);
        }
    }
}

/* ===== Gegner-Feuerverhalten (Schritt 18) ??? zurueckportiert 17.07.2026 =====
   Aus Archiv/xenon_split_wip_20260716_2221.c; ging beim 16.07-Reset verloren.
   Abweichung zum WIP (bewusst): der WIP pruefte die Schiffskollision mit
   collide(x, y, player.x, player.y, 10) ??? einem groben 10px-Kreis, der die
   Kippstufe des Schiffs ignoriert. Jede andere Schussart im Projekt nutzt
   inzwischen die Hitzone; hier deshalb dasselbe Muster wie in
   mapobj_bullets_update(): Schiffs-Hitzone EINMAL pro Frame aufloesen
   (O(1)-Tabelle), dann rects_overlap() pro Schuss. Gleiche Kosten, genauer. */

/* aimed=0: eine von 8 festen Richtungen (45-Grad-Schritte). Das Original laesst
   die Richtung offen ("zufaellig"); feste Richtungen vermeiden Trig, gleiches
   Prinzip wie die Titelscreen-Sterne.
   aimed=1: geradlinig auf die AKTUELLE Spielerposition ??? Chebyshev-Normierung
   (groessere Achse dominiert), dieselbe sqrt-freie Methode wie beim
   Pickup-Anflug, nur mit ENEMY_SHOT_SPEED_FIX als Zielgeschwindigkeit. */
static void enemy_fire(u8 ex, u8 ey, u8 aimed) {
    u8 b;
    s16 vx, vy;
    if (aimed) {
        s16 dx  = (s16)((s16)g_player.x - (s16)ex);
        s16 dy  = (s16)((s16)g_player.y - (s16)ey);
        s16 adx = (dx < 0) ? (s16)(-dx) : dx;
        s16 ady = (dy < 0) ? (s16)(-dy) : dy;
        s16 max_abs = (adx > ady) ? adx : ady;
        if (max_abs == 0) {
            vx = 0; vy = (s16)ENEMY_SHOT_SPEED_FIX;
        } else {
            s16 frames = (s16)((s16)(max_abs << 4) / (s16)ENEMY_SHOT_SPEED_FIX);
            if (frames < 1) frames = 1;
            vx = (s16)((s16)(adx << 4) / frames);
            vy = (s16)((s16)(ady << 4) / frames);
            if (dx < 0) vx = (s16)(-vx);
            if (dy < 0) vy = (s16)(-vy);
        }
    } else {
        static const s16 DX8[8] = { 16, 11, 0, -11, -16, -11, 0, 11 };
        static const s16 DY8[8] = { 0, 11, 16, 11, 0, -11, -16, -11 };
        u8 d = (u8)(QRandom() & 7u);
        vx = DX8[d]; vy = DY8[d];
    }
    for (b = 0u; b < (u8)MAX_ENEMY_BULLETS; b++) {
        if (g_ebullets[b].active) continue;
        g_ebullets[b].active = 1u;
        g_busy_ebullets = 1u;   /* Frueh-Ausstieg: Flag an der Erzeugungsstelle */
        g_ebullets[b].x_fix  = (s16)((s16)ex << 4);
        g_ebullets[b].y_fix  = (s16)((s16)ey << 4);
        g_ebullets[b].vx = vx; g_ebullets[b].vy = vy;
        g_ebullets[b].oam = OAM_NONE;
        g_ebullets[b].spr = 0u;   /* 0 = gemeinsames Sprite */
        break;   /* Pool voll -> dieser Schuss faellt aus, kein Nachrutschen */
    }
}

/* ============ ENDBOSS "das AUGE" (Schritt 20, 21.07.2026) ============
   Spezifikation CLAUDE.md 0.4d, hergeleitet aus XENON2.EXE (1000:758f/72c4/7252/
   7448/714d/71a9/71dd) und der portierten Referenz src/game/l1Boss.ts.

   Der KOERPER besteht aus Map-Kacheln (Zeilen 285-291, liegt im Terrain - nicht
   hier). Davor haengt eine 8-gliedrige TENTAKEL, deren Glieder Spielerschuesse
   SCHLUCKEN; verwundbar ist nur ein kleiner AUGEN-Fleck. Die KLAUE an der Spitze
   zielt auf den Spieler und feuert. Zeiten auf 30 fps umgerechnet (Original
   18,2 Hz, x1,648), Pixel halbiert (DOS 320x200 -> 160x152).

   Die Arena steht: LVL_SCROLL_END_ROW stoppt den Scroll, deshalb sind Welt- und
   Bildschirmkoordinaten hier identisch (so macht es auch der Port, placeBoss()).

   Sprite-Verweise stehen NUR hier, nicht in map.h -> in tools/gen_spr_compaction.js
   angemeldet, sonst fehlen die Rohkacheln im Pool und spr_vram() liefert die
   Rueckfallgrafik (= Schiffsspitze). */
#define BOSS_SPR_SHOT     195u
#define BOSS_SPR_SEG      196u
#define BOSS_TIP_ROTSET     2u   /* Klaue, 8 Richtungen, GESPLITTET (a+b) */
#define BOSS_SPR_EYE        0u   /* Auge: noch nicht zugewiesen (0 = nicht zeichnen) */

/* 27.07.2026 (Nutzerwunsch): Augen-Hitzone 30 % groesser. 8x7 -> 10x9, der
   Ursprung wandert je 1 Pixel nach links/oben, damit die Zone mittig ueber dem
   Auge bleibt statt nach rechts unten zu wachsen. */
#define BOSS_EYE_X0        75u   /* war 76 */
#define BOSS_EYE_W         10u   /* war  8 */
#define BOSS_EYE_DY0       33u   /* war 34 */
#define BOSS_EYE_H          9u   /* war  7 */
#define BOSS_MOUTH_X       80u
#define BOSS_MOUTH_DY      48u
#define BOSS_Y_MIN          8u
#define BOSS_Y_DOWN        33u
#define BOSS_SEG_COUNT      8u
#define BOSS_SEG_SPACING    4u   /* 25.07.2026: 5 -> 4 (Nutzer: "Glieder etwas enger zusammen") */
#define BOSS_REACH_MAX     11u
#define BOSS_BOB_TOP      280u
#define BOSS_BOB_DOWN      82u
#define BOSS_BOB_HOLD      99u
#define BOSS_BOB_UP        82u
#define BOSS_EXTEND       163u
#define BOSS_RETRACT       66u
#define BOSS_FIRE_ACC       9u
#define BOSS_HP            30u
/* KORREKTUR 22.07.2026: hier stand 285 mit dem Kommentar "oberste Map-Zeile des
   Koerpers". Auf dem BILDSCHIRM ist 285 aber die UNTERKANTE: hoehere Map-Zeilen
   rollen oben ein, der 6x7-Block 285..291 steht also mit 291 oben und 285 unten.
   Alle Boss-Offsets rechnen jedoch von der OBERKANTE nach unten (Auge +34..+41,
   Maul +48, aus bosses.md/l1Boss.ts) - mit 285 als Anker lag das Augenfeld 34 px
   UNTER dem Koerper im Leeren, und kein Schuss konnte je treffen. */
#define BOSS_ANCHOR_ROW   298u   /* obere Bildschirmkante des Koerpers (Block 292..298) */
/* 28.07.2026: von 291/285..291 auf 298/292..298. Die Map ist auf die Original-
   laenge 300 Zeilen gewachsen (vorher 293, am Ende abgeschnitten) und der
   Boss-Koerper sitzt jetzt dort, wo ihn das Original hat. Die Zeilennummern
   selbst haben sich NICHT verschoben (Korrelation gegen S1/MAP.CMP: unsere
   Zeile = 299 - DOS-Zeile, fuer alte wie neue Map) - nur der Boss ist an sein
   richtiges Ende gerueckt. */
/* 23.07.2026 Boss-Treffer-Flash (Nutzerwunsch "der Boss blinkt nicht beim Treffer",
   "du kannst die Map-Kacheln invertieren"): Der Boss-KOERPER sind Map-Kacheln, das
   verwundbare Auge hat kein eigenes Sprite - also flasht der Body ueber die Tilemap-
   Paletten. Reservierte, im Terrain-Export unbenutzte BG-Flash-Slots (weiss):
   SCR2-HW-Slot 4 (scr2_pal_hw belegt nur 0,5..15 -> 1..4 frei), SCR1-Slot 14
   (SCR1 nutzt 0..4, Slot 15 = STAR_PAL). Body-Kacheln liegen in Map-Zeilen 285..291.
   BOSS_FLASH_SCR2_PAL/SCR1_PAL sind weiter oben definiert (vor scr_pal_load). */
#define BOSS_BODY_ROW_LO  292u
#define BOSS_BODY_ROW_HI  298u
#define BOSS_CASH_LOOPS     9u   /* Regen: 9 Durchlaeufe a 50 + 100 Credits */

/* Viertel-Sinus, 0..64 in 17 Schritten. Der Spielcode hatte bisher keine
   Sinustabelle (die Wurmpfade sind vorberechnet), deshalb hier eine eigene,
   kleine - reicht fuer das Winden der Tentakel voellig. */
static const s8 BOSS_SIN[17] = { 0,12,24,35,45,53,59,63,64,63,59,53,45,35,24,12,0 };
static s8 boss_sin(u8 ph) {          /* ph 0..63 -> -64..64 */
    u8 q = (u8)(ph & 31u);
    s8 v = (q <= 16u) ? BOSS_SIN[q] : BOSS_SIN[32u - q];
    return (ph & 32u) ? (s8)(-v) : v;
}

typedef struct {
    u8  active, dead, hp, y;   /* dead: einmal gestorben, nie wieder scharf werden */
    s8  bob_v;
    u16 bob_c;
    s16 cycle;
    u8  reach, fire_acc, tick;
    u8  bob;                   /* Wipp-Offset auf die Ankerzeile */
    u8  oam[BOSS_SEG_COUNT];   /* a-Ebene je Glied */
    u8  oam_b;                 /* b-Ebene, nur die Klaue */
    u8  seg_x[BOSS_SEG_COUNT], seg_y[BOSS_SEG_COUNT];
    u8  flash;                 /* 23.07.2026: Rest-Frames Treffer-Flash (Sprite-Teile + Body-Map-Kacheln) */
} TBoss;
static TBoss g_boss;
/* Boss-Body-Treffer-Flash Save/Restore (23.07.2026): die Original-Tilemap-Woerter
   der Body-Zellen werden beim Flash-START gesichert und beim Flash-ENDE exakt
   zurueckgeschrieben. Bulletproof - restore_terrain_row() gab fuer Map-Objekt-/
   Wilt-Zellen in den Body-Zeilen NICHT den exakten Vorzustand zurueck (einzelne
   Zellen blieben weiss). Scroll steht in der Arena, die Ring-Positionen sind
   zwischen Sichern und Zuruecksetzen also stabil. */
#define BOSS_BODY_RINGS 8u
/* 23.07.2026 (Nutzerpraezisierung): nur die ECHTEN Boss-Kacheln invertieren/entfernen.
   Der Endboss besteht aus den Map-Tiles 141..178 (Nutzer-Angabe, in map.h Zeilen
   285..291 nur Spalten 7..12 = die Muschelform); die Arena-Waende drumherum haben
   andere Tiles und sollen NICHT flashen. Deshalb prueft der Flash/Remove jede Zelle
   gegen lvl_map (Kachelindex 141..178) statt die ganze Zeile zu nehmen. */
#define BOSS_TILE_LO 141u
#define BOSS_TILE_HI 181u   /* 28.07.2026: 178 -> 181. Der Koerper selbst liegt in
                               141..177; 178..181 sind die Folgebilder der beiden
                               Augen-Animationen (Koepfe 166/167), die beim Treffer-
                               Flash und beim Entfernen mitgemeint sind. */
static u8  g_boss_remove_pending;                 /* Boss besiegt -> Body-Kacheln am Frame-Ende leeren */
static u8  g_boss_flashing;                       /* Latch: Body gerade geflasht (Save-Once/Restore-Once) */
/* 29.07.2026: g_boss_save1/2 + g_boss_save_ring/_n (zusammen 649 Byte RAM)
   ENTFERNT. Seit der Body-Flash ueber die NEG-Invertierung laeuft (28.07.,
   §9.4), wurden die Sicherungen nirgends mehr GEFUELLT - boss_body_restore()
   lief immer ueber n=0. Der frei gewordene RAM traegt die 22 neuen
   Map-Objekte (kleine Tuerme + Anemonen, LVL_MAPOBJ_COUNT 10 -> 32). */

static void boss_reset(void) {
    u8 k;
    g_boss.active = 0u; g_boss.dead = 0u; g_boss.hp = (u8)BOSS_HP;
    g_boss.y = (u8)BOSS_Y_MIN; g_boss.bob_v = 0; g_boss.bob_c = 0u;
    g_boss.cycle = 0; g_boss.reach = 0u; g_boss.fire_acc = 0u; g_boss.tick = 0u; g_boss.bob = 0u;
    g_boss.flash = 0u;
    g_boss_flashing = 0u; g_boss_remove_pending = 0u;   /* Body-Flash/Remove-Latch zuruecksetzen (kein stale Restore nach Levelneustart) */
    g_boss.oam_b = OAM_NONE;
    for (k = 0u; k < (u8)BOSS_SEG_COUNT; k++) g_boss.oam[k] = OAM_NONE;
}

/* Wippen (1000:7448): Pause oben -> abwaerts -> Pause unten -> aufwaerts.
   Seit der Map-Verankerung ein OFFSET (g_boss.bob) auf die Ankerzeile, nicht
   mehr die absolute Position - sonst wuerde das Wippen den Scroll ueberschreiben. */
static void boss_bob_delta(void) {
    if (g_boss.bob_c > 0u) g_boss.bob_c--;
    else if (g_boss.bob_v < 0) { g_boss.bob_v = 0; g_boss.bob_c = (u16)BOSS_BOB_TOP; }
    else if (g_boss.bob_v > 0) { g_boss.bob_v = 0; g_boss.bob_c = (u16)BOSS_BOB_HOLD; }
    else if (g_boss.bob == 0u)                  { g_boss.bob_v = 1;  g_boss.bob_c = (u16)BOSS_BOB_DOWN; }
    else                                        { g_boss.bob_v = -1; g_boss.bob_c = (u16)BOSS_BOB_UP; }
    if (g_boss.bob_v > 0 && g_boss.bob < (u8)(BOSS_Y_DOWN - BOSS_Y_MIN)) g_boss.bob++;
    if (g_boss.bob_v < 0 && g_boss.bob > 0u)                             g_boss.bob--;
}

/* Tentakel-Zyklus: ausfahren bis Reichweite 11, dann einziehen (1000:73b6). */
static void boss_cycle(void) {
    if (g_boss.cycle >= 0) {
        if (g_boss.reach < (u8)BOSS_REACH_MAX) g_boss.reach++;
        g_boss.cycle++;
        if (g_boss.cycle >= (s16)BOSS_EXTEND) g_boss.cycle = -(s16)BOSS_RETRACT;
    } else {
        if (g_boss.reach > 0u) g_boss.reach--;
        g_boss.cycle++;
    }
}

/* Glieder entlang einer Sinuskurve unter dem Maul auslegen. Der Port ersetzt die
   inertiale Sinuswanderung des Originals durch eine einfache Kurve, deren
   Amplitude mit der Reichweite waechst - sichtbares Winden und Feuer-Tor bleiben
   gleich (dieselbe Abweichung dokumentiert l1Boss.ts). */
static void boss_place(void) {
    u8 k;
    for (k = 0u; k < (u8)BOSS_SEG_COUNT; k++) {
        u16 along = (u16)((u16)k * (u16)BOSS_SEG_SPACING * (u16)g_boss.reach) / (u16)BOSS_REACH_MAX;
        u8  ph    = (u8)((u8)(g_boss.tick >> 1) + (u8)(k * 5u));
        s16 wob   = (s16)boss_sin((u8)(ph & 63u));
        /* 25.07.2026 (Nutzer "Tentakel bewegt sich vom Boss weg"): die Winde-
           Amplitude waechst mit k ENTLANG der Kette - Glied 0 haengt FEST am Maul
           (dx=0), die Spitze windet wie bisher (~7px). Vorher wackelten alle
           Glieder gleich stark, also auch der Anflanschpunkt. */
        s16 dx    = (s16)(wob * (s16)g_boss.reach);
        dx = (s16)(dx * (s16)k);
        dx = (s16)(dx / 672);   /* 96 * (BOSS_SEG_COUNT-1) -> Spitze wie fruehere /96-Amplitude */
        g_boss.seg_x[k] = (u8)((s16)BOSS_MOUTH_X + dx);
        /* 25.07.2026 (Nutzer "Tentakel faehrt manchmal nach unten"): KEIN g_boss.bob
           mehr - das war das Original-Koerperwippen (bis 25px), aber unser Koerper
           ist STATISCHE Map-Grafik. Die Tentakel haengt jetzt fest am Maul. */
        g_boss.seg_y[k] = (u8)(g_boss.y + (u8)BOSS_MOUTH_DY + (u8)along);
    }
}

/* Klaue feuert, sobald die Tentakel voll ausgefahren ist (1000:7252/7277):
   Akkumulator +9 je Frame, bei 8-Bit-Ueberlauf ein gezielter Schuss, danach
   neu gesaet - im Mittel alle ~28 Frames. */
static void boss_fire(void) {
    u8 b, tipx, tipy, prev;
    if (g_boss.reach < (u8)BOSS_REACH_MAX) return;
    prev = g_boss.fire_acc;
    g_boss.fire_acc = (u8)(g_boss.fire_acc + (u8)BOSS_FIRE_ACC);
    if (g_boss.fire_acc >= prev) return;            /* kein Ueberlauf -> nicht feuern */
    g_boss.fire_acc = (u8)GetRandom(63u);
    tipx = g_boss.seg_x[BOSS_SEG_COUNT - 1u];
    tipy = g_boss.seg_y[BOSS_SEG_COUNT - 1u];
    enemy_fire(tipx, tipy, 1u);                     /* gezielt auf den Spieler */
    for (b = 0u; b < (u8)MAX_ENEMY_BULLETS; b++)    /* dem eben gesetzten Schuss S195 geben */
        if (g_ebullets[b].active && g_ebullets[b].spr == 0u &&
            g_ebullets[b].x_fix == (s16)((s16)tipx << 4)) { g_ebullets[b].spr = (u16)BOSS_SPR_SHOT; break; }
}

/* Trefferpruefung: NUR das Augenfeld zaehlt, Koerpertreffer fliegen glatt
   hindurch (1000:714d). bx/by ist die Mitte des Spielerschusses. */
static u8 boss_eye_hit(u8 bx, u8 by) {
    u8 y0;
    if (!g_boss.active) return 0u;
    y0 = (u8)(g_boss.y + (u8)BOSS_EYE_DY0);   /* 25.07.2026: ohne bob - die Augen-Hitzone wanderte sonst vom STATISCHEN Koerper weg (bis 25px daneben) */
    if (bx < (u8)BOSS_EYE_X0 || bx >= (u8)(BOSS_EYE_X0 + BOSS_EYE_W)) return 0u;
    if (by < y0 || by >= (u8)(y0 + BOSS_EYE_H)) return 0u;
    return 1u;
}

/* Schluckt ein Tentakelglied diesen Schuss? (death 0x3a99: Schuss weg, kein
   Schaden - die Glieder sind der bewegliche Schild vor dem Auge.) */
static u8 boss_seg_hit(u8 bx, u8 by) {
    u8 k;
    if (!g_boss.active || g_boss.reach == 0u) return 0u;
    for (k = 0u; k < (u8)BOSS_SEG_COUNT; k++) {
        u8 dx = (bx > g_boss.seg_x[k]) ? (u8)(bx - g_boss.seg_x[k]) : (u8)(g_boss.seg_x[k] - bx);
        u8 dy = (by > g_boss.seg_y[k]) ? (u8)(by - g_boss.seg_y[k]) : (u8)(g_boss.seg_y[k] - by);
        if (dx < 6u && dy < 6u) return 1u;
    }
    return 0u;
}

/* Ein Tentakelglied zeichnen. Die KLAUE (letztes Glied) kommt aus dem
   Rotationssatz und ist GESPLITTET (a+b) - deshalb ein zweiter OAM-Slot nur fuer
   sie. Solange der Satz leer ist (0xFFFF), faellt sie auf BOSS_SPR_SEG zurueck. */
static void boss_draw(void) {
    u8 k, dir, rf, flip;
    u16 raw, rawb;
    for (k = 0u; k < (u8)BOSS_SEG_COUNT; k++) {
        u8 last = (u8)(k == (u8)(BOSS_SEG_COUNT - 1u));
        if (!g_boss.active || g_boss.reach == 0u) {          /* eingezogen = unsichtbar */
            if (g_boss.oam[k] != OAM_NONE) {
                UnsetSprite(g_boss.oam[k]); oam_pool_free(g_boss.oam[k]); g_boss.oam[k] = OAM_NONE;
            }
            if (last && g_boss.oam_b != OAM_NONE) {
                UnsetSprite(g_boss.oam_b); oam_pool_free(g_boss.oam_b); g_boss.oam_b = OAM_NONE;
            }
            continue;
        }
        if (g_boss.oam[k] == OAM_NONE) {
            g_boss.oam[k] = oam_pool_alloc();
            if (g_boss.oam[k] == OAM_NONE) continue;          /* Pool leer: naechsten Frame */
        }
        if (!last) { spr_draw_s_single(g_boss.oam[k], (u16)BOSS_SPR_SEG, g_boss.seg_x[k], g_boss.seg_y[k]); continue; }

        /* Klaue: Richtung aus dem Winkel zum Spieler (Oktant), Grafik aus dem Satz */
        {
            s16 dx = (s16)((s16)(g_player.x + 12u) - (s16)g_boss.seg_x[k]);
            s16 dy = (s16)((s16)(g_player.y + 12u) - (s16)g_boss.seg_y[k]);
            s16 ax = (dx < 0) ? (s16)(-dx) : dx;
            s16 ay = (dy < 0) ? (s16)(-dy) : dy;
            if (ay >= (s16)(ax * 2)) dir = (dy > 0) ? 2u : 6u;          /* runter / hoch */
            else if (ax >= (s16)(ay * 2)) dir = (dx > 0) ? 0u : 4u;     /* rechts / links */
            else if (dx > 0) dir = (dy > 0) ? 1u : 7u;
            else             dir = (dy > 0) ? 3u : 5u;
        }
        raw  = lvl_rotset_idx[BOSS_TIP_ROTSET][dir];
        rawb = lvl_rotset_b_idx[BOSS_TIP_ROTSET][dir];
        if (raw == 0xFFFFu) {                                  /* Satz noch leer */
            spr_draw_s_single(g_boss.oam[k], (u16)BOSS_SPR_SEG, g_boss.seg_x[k], g_boss.seg_y[k]);
            continue;
        }
        rf   = lvl_rotset_flip[BOSS_TIP_ROTSET][dir];
        flip = (u8)(((rf & 1u) ? SPR_HFLIP : 0u) | ((rf & 2u) ? SPR_VFLIP : 0u));
        SetSprite(g_boss.oam[k], spr_vram(raw), 0, g_boss.seg_x[k], g_boss.seg_y[k],
                  lvl_rotset_pal[BOSS_TIP_ROTSET][dir]);
        SpriteControl(g_boss.oam[k], SPR_FRONT, flip);
        if (rawb != 0u && rawb != 0xFFFFu) {
            if (g_boss.oam_b == OAM_NONE) g_boss.oam_b = oam_pool_alloc();
            if (g_boss.oam_b != OAM_NONE) {
                SetSprite(g_boss.oam_b, spr_vram(rawb), 0, g_boss.seg_x[k], g_boss.seg_y[k],
                          lvl_rotset_b_pal[BOSS_TIP_ROTSET][dir]);
                SpriteControl(g_boss.oam_b, SPR_FRONT, flip);
            }
        } else if (g_boss.oam_b != OAM_NONE) {
            UnsetSprite(g_boss.oam_b); oam_pool_free(g_boss.oam_b); g_boss.oam_b = OAM_NONE;
        }
    }
    /* 23.07.2026: Treffer-Flash der Tentakel/Klaue (Sprite-Teile) - wie bei den
       Gegnern per OAM-Palettenzuordnung nach dem Zeichnen. boss_draw() schreibt die
       Boss-Sprites jeden Frame komplett neu (kein Positions-Schnellpfad), also kehrt
       die echte Palette von selbst zurueck, sobald flash 0 ist. Heruntergezaehlt wird
       flash in boss_body_flash() am Frame-Ende (dort ist auch der Body dran). */
    if (g_boss.flash) {
        u8 *sc = SPRITE_COLOUR;
        for (k = 0u; k < (u8)BOSS_SEG_COUNT; k++)
            if (g_boss.oam[k] != OAM_NONE) g_neg_on = 1u;
        g_neg_on = 1u;
    }
}

/* 23.07.2026: Boss-KOERPER (Map-Kacheln, Zeilen 285..291) beim Treffer weiss
   aufblitzen lassen - das verwundbare Auge hat kein Sprite, also ist der Body das
   sichtbare Treffer-Feedback (Nutzerwunsch). Laeuft am FRAME-ENDE (Tilemap-Schreib-
   zugriffe nur dort, §7d), setzt die Paletten-Bits der belegten Body-Zellen (beide
   Ebenen) auf die reservierten weissen BG-Flash-Slots und zaehlt flash herunter;
   beim Ablauf werden die betroffenen Ringzeilen aus den Terrain-Daten restauriert.
   Wird jeden Flash-Frame neu gesetzt, damit anim_update() es nicht ueberschreibt. */
/* Ist die Zelle (Ringzeile r, Spalte col) eine echte Boss-Kachel (lvl_map 141..178)? */
/* BUGFIX 27.07.2026 (Nutzer: "Auge wird beim Treffer nicht invertiert" UND "nach
   dem Tod ist die Augen-Animation noch da"): hier stand `& 0x7FFFu`. Das entfernt
   nur das FLIP-Bit (0x8000), laesst aber das ANIM-Bit (0x4000) stehen. Seit die
   Augen-Animation existiert (25.07.), tragen genau die zwei Augenzellen
   (Zeile 287, Spalte 9/10) dieses Bit: raw 0x40A6/0x40A7 -> maskiert 16550/16551,
   also weit ausserhalb von 141..178. Damit fielen ausgerechnet die Augenzellen aus
   JEDER Boss-Zellenpruefung heraus - kein Treffer-Flash und beim Tod nicht
   entfernt. Die Tile-Nummer steht in Bit 0..8, genau wie lvl1_put_cell() es liest. */
static u8 boss_cell(u16 map_row, u8 col) {
    u16 t = (u16)(lvl_map[(u16)map_row * (u16)LVL_MAP_W + (u16)col] & 0x01FFu);
    return (u8)(t >= (u16)BOSS_TILE_LO && t <= (u16)BOSS_TILE_HI);
}
static void boss_body_restore(void) {
    /* 29.07.2026: nur noch das Latch. Die Tilemap-Sicherung (g_boss_save1/2)
       ist mit dem Wechsel auf die NEG-Invertierung entfallen - der Invert
       aendert keine Tilemap-Woerter, es gibt nichts wiederherzustellen. */
    g_boss_flashing = 0u;
}
static void boss_body_flash(void) {
    u8 i, col, r;
    volatile u16 *m2 = SCROLL_PLANE_2, *m1 = SCROLL_PLANE_1;
    /* Boss besiegt -> Body-Kacheln entfernen (Nutzerwunsch), am Frame-Ende. Nur die
       echten Boss-Tiles leeren; die Arena-Waende bleiben. Ueberschreibt einen evtl.
       noch laufenden Flash. */
    if (g_boss_remove_pending) {
        g_boss_remove_pending = 0u; g_boss_flashing = 0u; g_boss.flash = 0u;
        for (r = 0u; r < 32u; r++) {
            u16 mr = g_row_map[r];
            if (mr < (u16)BOSS_BODY_ROW_LO || mr > (u16)BOSS_BODY_ROW_HI) continue;
            for (col = 0u; col < (u8)LVL_MAP_W; col++) {
                if (!boss_cell(mr, col)) continue;
                { u16 idx = (u16)r * 32u + (u16)col; m2[idx] = 0u; m1[idx] = 0u; }
                /* BUGFIX 27.07.2026: die Zelle auch aus dem Animations-Grid nehmen.
                   Sonst schreibt anim_update() die Augenzellen alle 10 Frames wieder
                   voll - der Koerper waere weg, das Auge blinzelte weiter. Das
                   Leeren geht hier bewusst DIREKT in die Tilemap (nicht ueber
                   lvl1_put_cell, das die ROM-Map liest und den Boss neu zeichnen
                   wuerde), also muss das Grid von Hand nachgezogen werden. */
                anim_grid_set(r, col, 0u);
                /* BUGFIX 24.07.2026 (Nutzer): auch die KOLLISION der Boss-Zellen weg -
                   terrain_solid() liest lvl_map (ROM, weiter 141..178) und wuerde sonst
                   eine unsichtbare Wand lassen ("kann nicht hinfliegen, wo der Boss war").
                   das Wilt-Bit ist genau der Nicht-Solide-Mechanismus (Ringzeile
                   r, Arena-Scroll steht -> stabil). */
                g_mapobj_grid[r][col] |= (u8)MAPOBJ_WILT_BIT;
            }
        }
        return;
    }
    /* 28.07.2026: Der Body-Flash laeuft jetzt ueber die HARDWARE-Invertierung
       (neg_flush()) wie jeder andere Treffer auch. Damit entfaellt der ganze
       Block, der die Boss-Zellen jeden Frame auf BOSS_FLASH_SCR2_PAL/SCR1_PAL
       umschaltete und ihre Originalwoerter vorher sichern musste - inklusive
       g_boss_save1/2 (zusammen 640 Byte RAM) und dem Sonderfall "SCR2 hat
       keinen freien Slot, also nur die Palettennummer auf den Bar-Slot 4
       schieben". Der Invert braucht keinen Slot und trifft Body, Tentakel und
       Auge gleichzeitig, ohne dass eine Zelle vergessen werden kann. */
    if (!g_boss.flash) return;
    g_neg_on = 1u;
    g_boss.flash--;
    if (g_boss.flash == 0u) boss_body_restore();
}

/* Cash-Regen beim Tod (Spawner 1000:15e4, CX=8 -> 9 Durchlaeufe a 50+100 Credits).
   Bei uns 6 Durchlaeufe, siehe MAX_PICKUPS. Positionen zufaellig ueber den Schirm,
   die Token nutzen danach die normale Pickup-Flugbahn. */
/* Belohnungs-Flugbahn-Ziel (Seek zur Mitte); hier oben, weil boss_cash_rain() es
   fuer die Cash-Token braucht und die Pickup-Funktionen weiter unten ebenfalls. */
#define PICKUP_SEEK_X   76u
#define PICKUP_SEEK_Y   56u
#define PICKUP_FALL_STEP 16
static void boss_cash_rain(void) {
    u8 n, p;
    for (n = 0u; n < 12u; n++) {
        for (p = 0u; p < (u8)MAX_PICKUPS; p++) {
            if (g_pickups[p].active) continue;
            g_pickups[p].active = 1u;
            g_busy_pickups = 1u;   /* Frueh-Ausstieg: Flag an der Erzeugungsstelle */
            g_pickups[p].kind   = 0u;                       /* Cash */
            g_pickups[p].value  = (u16)((n & 1u) ? 100u : 50u);
            g_pickups[p].spr    = reward_icon_for(0u, g_pickups[p].value);
            g_pickups[p].manim  = 0xFFu;
            /* 23.07.2026 (Nutzerwunsch, Original-Abgleich): die Cash-Token nutzen im
               Original DIESELBE Pickup-Update-Funktion (1000:28df) und spawnen in der
               SEEK-Phase ([si+0x1a]=7) - sie gleiten also zur Bildschirmmitte
               ("sortieren sich zur Mitte"), genau wie alle anderen Pickups, statt
               stur von ihrer Zufallsposition zu fallen. Mein 22.07.-Fix (direkt
               state=2/fallen) war deshalb falsch. Jetzt Seek-Setup wie in
               pickup_spawn_for_group(): Anflug-Vektor auf PICKUP_SEEK_X/Y, state 0.
               Die Token starten zufaellig verteilt und ziehen zur Mitte zusammen,
               danach laeuft die normale Phase 0->1->2. */
            g_pickups[p].x      = (u8)(5u + GetRandom(145u));
            g_pickups[p].y      = (u8)(3u + GetRandom(90u));
            g_pickups[p].x_fix  = (s16)((s16)g_pickups[p].x << 4);
            g_pickups[p].y_fix  = (s16)((s16)g_pickups[p].y << 4);
            {
                s16 total_dx = (s16)((s16)PICKUP_SEEK_X - (s16)g_pickups[p].x);
                s16 total_dy = (s16)((s16)PICKUP_SEEK_Y - (s16)g_pickups[p].y);
                s16 adx = (total_dx < 0) ? (s16)(-total_dx) : total_dx;
                s16 ady = (total_dy < 0) ? (s16)(-total_dy) : total_dy;
                s16 max_abs = (adx > ady) ? adx : ady;
                s16 frames = (s16)((s16)(max_abs << 4) / (s16)LVL_PICKUP_HOMING_SPEED);
                s16 hdx, hdy;
                if (frames < 1) frames = 1;
                hdx = (s16)((s16)(adx << 4) / frames);   /* immer den Betrag shiften (C89-Falle, siehe pickup_spawn_for_group) */
                hdy = (s16)((s16)(ady << 4) / frames);
                g_pickups[p].homing_frames = (u16)frames;
                g_pickups[p].homing_tick   = 0u;
                g_pickups[p].homing_dx     = (total_dx < 0) ? (s16)(-hdx) : hdx;
                g_pickups[p].homing_dy     = (total_dy < 0) ? (s16)(-hdy) : hdy;
            }
            g_pickups[p].state  = 0u;                       /* 0 = Anflug/Seek zur Mitte */
            g_pickups[p].dir    = 0u;
            g_pickups[p].path_frame    = 0u;
            g_pickups[p].oam0 = OAM_NONE; g_pickups[p].oam1 = OAM_NONE;
            break;
        }
    }
    /* 23.07.2026: Sammelfenster oeffnen -> Spieler unverwundbar, bis der letzte
       Token eingesammelt ist oder unten verfaellt (pickups_update loescht das
       Flag wieder). Beim Bosstod ist die Welt gerade gewischt, es sind also nur
       Regen-Token aktiv. */
    g_boss_rain = 1u;
}

/* Tod (1000:71a9): Latch aus, Cash-Regen, 19 Explosionen, Gegner-Ring leeren,
   Level komplett. KEINE Punkte - die eigene Todesroutine erreicht die
   Scoring-Funktionen nie (bosses.md). */
static void boss_die(void) {
    u8 k;
    g_boss.active = 0u;
    g_boss.dead   = 1u;   /* siehe boss_update(): ohne diesen Riegel wird der Boss
                             im naechsten Frame sofort wieder scharf gemacht */
    for (k = 0u; k < (u8)BOSS_SEG_COUNT; k++)
        if (g_boss.oam[k] != OAM_NONE) { UnsetSprite(g_boss.oam[k]); oam_pool_free(g_boss.oam[k]); g_boss.oam[k] = OAM_NONE; }
    if (g_boss.oam_b != OAM_NONE) { UnsetSprite(g_boss.oam_b); oam_pool_free(g_boss.oam_b); g_boss.oam_b = OAM_NONE; }
    for (k = 0u; k < (u8)MAX_ENEMIES; k++)      g_enemies[k].active = 0u;
    for (k = 0u; k < (u8)MAX_ENEMY_BULLETS; k++) g_ebullets[k].active = 0u;
    g_boss_remove_pending = 1u;       /* 23.07.2026: Body-Kacheln entfernen (am Frame-Ende, boss_body_flash) */
    boss_cash_rain();                 /* NACH dem Wischen, sonst raeumt es die Token gleich mit weg */
    shop_trigger_check(1u, 0u);       /* Endboss-Shop: kind 1, Verzoegerung = Sammelfenster */
}

/* Schaden aufs Auge. Bei 0 stirbt der Boss (1000:714d/71a9). */
static void boss_take_damage(u8 dmg) {
    if (!g_boss.active) return;
    if (g_boss.hp > dmg) {
        g_boss.hp = (u8)(g_boss.hp - dmg);
        g_boss.flash = (u8)HIT_FLASH_FRAMES;   /* 23.07.2026: Treffer-Flash (nur ueberlebter Treffer) */
        return;
    }
    g_boss.hp = 0u;
    boss_die();
}

/* Bildschirm-Y der Boss-Ankerzeile. Der KOERPER besteht aus Map-Kacheln und
   scrollt mit dem Terrain herein - die Tentakel muss also mitwandern, solange
   das Bild noch laeuft (Nutzerkorrektur 21.07.2026). Sobald der Scroll steht,
   liefert das denselben festen Wert wie eine Bildschirmverankerung.
   0xFF = Ankerzeile gerade nicht im Ring (Boss ausser Sicht). */
static u8 boss_anchor_y(void) {
    u8 ring = find_ring_row((u16)BOSS_ANCHOR_ROW);
    if (ring == MAPOBJ_NONE) return 0xFFu;
    return (u8)((u8)(ring << 3) - g_scr1_y);   /* u8-Wrap gewollt, wie terrain_solid */
}

/* Tentakel-Sprites abschalten und ihre OAM-Slots freigeben.
   BUGFIX 22.07.2026 (Nutzerbericht "nach dem Tod am Endboss steht ein Krakenarm
   statisch neben mir"): boss_update() kehrte bei unsichtbarem Anker einfach
   zurueck und respawn_do() fasste den Boss gar nicht an - die Glieder blieben
   also mit ihren letzten Koordinaten im OAM stehen, obwohl der Wiedereinstieg
   das Bild an eine ganz andere Stelle setzt. Dass sie sich nicht bewegten, war
   das Verraeterische: boss_place() lief nicht mehr, gezeichnet war aber noch. */
static void boss_hide(void) {
    u8 k;
    for (k = 0u; k < (u8)BOSS_SEG_COUNT; k++)
        if (g_boss.oam[k] != OAM_NONE) {
            UnsetSprite(g_boss.oam[k]); oam_pool_free(g_boss.oam[k]); g_boss.oam[k] = OAM_NONE;
        }
    if (g_boss.oam_b != OAM_NONE) {
        UnsetSprite(g_boss.oam_b); oam_pool_free(g_boss.oam_b); g_boss.oam_b = OAM_NONE;
    }
}

/* Pro Frame. Die Krake kommt ERST heraus, wenn das Bild steht (Nutzervorgabe) -
   im Original faellt das zusammen, weil der Kampf genau dort beginnt, wo der
   Scroll auslaeuft (DOS-Scroll 0x30). */
static void boss_update(void) {
    u8 anchor;
    /* BUGFIX 22.07.2026 (im Emulator beobachtet): boss_die() setzt active=0, was
       der Zweig darunter als "noch nicht aktiviert" las - der Boss wurde im
       naechsten Frame sofort wieder scharf, kaempfte mit 0 HP weiter und warf
       bei JEDEM weiteren Treffer einen kompletten Cash-Regen ab. */
    if (g_boss.dead) return;
    if (!g_boss.active) {
        /* 22.07.2026: gegen LVL_SCROLL_END_TOP_ROW pruefen, nicht gegen die rohe
           Map-Endzeile - sonst wartet der Boss auf einen Scroll-Stand, den der
           Deckel in scroll_update() nie zulaesst, und wird nie scharf. */
        if (g_scroll_y < (u16)((u16)LVL_SCROLL_END_TOP_ROW * 8u)) return;   /* Bild laeuft noch */
        g_boss.active = 1u;
        /* 23.07.2026: Das fruehere "Arena raeumen beim Scharfmachen" (Pflaster)
           ist raus. Mit dem korrigierten Spawn-Vorlauf (SPAWN_LEAD_ROWS, oberer
           Bildrand statt Schiff) spawnen die Anflug-Wellen (Map-Zeile 259-274)
           schon ~12 Zeilen frueher, fliegen herein und wieder raus, BEVOR der
           Boss am Deckel scharf wird - wie im Original (letztes Event 14 Zeilen
           vor dem Kampf). Kein Code-Clear mehr noetig. */
    }
    anchor = boss_anchor_y();
    if (anchor == 0xFFu) { boss_hide(); return; }   /* Anker ausser Sicht -> Glieder weg */
    g_boss.y = anchor;
    g_boss.tick++;
    boss_bob_delta();
    boss_cycle();
    boss_place();
    boss_fire();
}

/* Akkumulator 1:1 aus dem Original (map.h-Kommentar bei lvl_spawn_fire_rate):
   jeden Frame um (rate % 100) erhoehen, bewusster 8-Bit-Ueberlauf, und bei
   Ueberlauf feuern OHNE den Rest zurueckzusetzen. Rate >=100 = gezielt. */
static void enemy_fire_tick(TEnemy *e) {
    u8 rate = e->c_fire_rate;   /* 22.07.2026: beim Spawn gemerkt, siehe TEnemy */
    u8 before;
    if (rate == 0u) return;
    before = e->fire_acc;
    e->fire_acc = (u8)(e->fire_acc + (rate % 100u));
    if (e->fire_acc < before) enemy_fire(e->x, e->y, (u8)(rate >= 100u));
}

static void ebullets_update(void) {
    u8  i;
    u8  ship_vuln = (u8)(g_player.inv_cd == 0u);
    s16 srx = 0, sry = 0;
    u8  srw = 0, srh = 0;
    if (ship_vuln) ship_hitzone_rect(&srx, &sry, &srw, &srh);
    for (i = 0u; i < (u8)MAX_ENEMY_BULLETS; i++) {
        s16 x, y;
        if (!g_ebullets[i].active) continue;
        g_busy_ebullets = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
        g_ebullets[i].x_fix = (s16)(g_ebullets[i].x_fix + g_ebullets[i].vx);
        g_ebullets[i].y_fix = (s16)(g_ebullets[i].y_fix + g_ebullets[i].vy);
#if TEST_BULLET_UPDATE_HALF
        g_ebullets[i].x_fix = (s16)(g_ebullets[i].x_fix + g_ebullets[i].vx);   /* 2-Frame-Takt: Doppelschritt */
        g_ebullets[i].y_fix = (s16)(g_ebullets[i].y_fix + g_ebullets[i].vy);
#endif
        x = (s16)(g_ebullets[i].x_fix >> 4);
        y = (s16)(g_ebullets[i].y_fix >> 4);
        if (x < -8 || x >= (s16)SCR_W || y < -8 || y >= (s16)BAR_Y) {
            g_ebullets[i].active = 0u;
            continue;
        }
        if (ship_vuln && rects_overlap(x, y, BULLET_HIT_W, BULLET_HIT_H, srx, sry, srw, srh)) {
            g_ebullets[i].active = 0u;
            g_dmg_src=1u; player_damage((u8)DMG_ENEMY_SHOT);
        }
    }
}

static void ebullets_draw(void) {
    u8  i, still = 0u;
    u16 n;
    if (!g_busy_ebullets) return;   /* Frueh-Ausstieg, siehe g_busy_* */
    /* 25.07.2026: Gegner-Schuesse bleiben beim FLACKERN. Der Versuch, sie wie
       die Waffen-Schuesse auf feste Slots zu legen, brachte auf Hardware
       nichts messbares (Nutzertest) - also die aeltere, slot-sparende Fassung
       behalten. Bei den Waffen-Schuessen bleibt es dagegen bei festen Slots. */
    for (i = 0u; i < (u8)MAX_ENEMY_BULLETS; i++) {
        if (g_ebullets[i].active) still = 1u;
        if (!g_ebullets[i].active || !FLICKER_VIS(i)) {   /* OFF-Phase: Slot freigeben (Flacker 19.07.2026) */
            if (g_ebullets[i].oam != OAM_NONE) {
                UnsetSprite(g_ebullets[i].oam);
                oam_pool_free(g_ebullets[i].oam);
                g_ebullets[i].oam = OAM_NONE;
            }
            continue;
        }
        /* Sprite je Schuss (21.07.2026): 0 = gemeinsames Gegner-Projektil. */
        n = (u16)(((g_ebullets[i].spr ? g_ebullets[i].spr : (u16)LVL_ENEMY_BULLET_SPR) & 0x01FFu) - 1u);
        {
            u8 wx = (u8)(g_ebullets[i].x_fix >> 4);
            u8 wy = (u8)(g_ebullets[i].y_fix >> 4);
            if (g_ebullets[i].oam == OAM_NONE) {
                g_ebullets[i].oam = oam_pool_alloc();
                if (g_ebullets[i].oam == OAM_NONE) continue;   /* Pool leer -> naechsten Frame erneut */
                SetSprite(g_ebullets[i].oam, spr_vram(lvl_sspr_a_idx[n]), 0, wx, wy, lvl_sspr_a_pal[n]);
                /* SpriteControl(FRONT,0) entfernt ??? redundant nach SetSprite (17.07.2026) */
            } else {
                SetSpritePosition(g_ebullets[i].oam, wx, wy);
            }
        }
    }
    g_busy_ebullets = still;
}

/* ===== Fahrgefuehl-Startwerte =====
   Die fuenf Laufzeit-Parameter (Tempo/Scroll/Auslauf/Kippen/Gegnertempo) werden
   einmalig aus den #defines gesetzt. Das fruehere Live-Tuning-Menue (OPTION) und
   die STRIP-Diagnose sind am 18.07.2026 entfernt (Werte eingebacken); die
   g_tune_*-Variablen bleiben als reine Laufzeit-Konstanten erhalten. */
static void tune_init(void) {
    g_tune_speed  = (u8)PLAYER_SPEED_STEP;
    g_tune_scroll = (u8)SCROLL_TICK_THRESHOLD;
    g_tune_coast  = (u8)COAST_FRAMES;
    g_tune_tilt   = (u8)TILT_STEP_FRAMES;
    g_tune_espeed = 35u;   /* 19.07.2026: auf den ECHTEN Scroll abgestimmt (Nutzerwunsch).
       Kein Subpixel-Scroll -> Scroll bleibt 1 px / SCROLL_TICK_THRESHOLD(3) Frames
       = 0,3333 px/Frame (10 px/s). Original-Verhaeltnis Gegner/Scroll = DOS_speed S.
       Das Tool baeckt spawn.speed = S x 0,1517 px/Entry. Damit enemy/scroll = S:
       enemy_px/Frame = S x 0,3333, Entries/Frame = 0,3333 / 0,1517 = 2,197,
       espeed = 2,197 x 16 = 35 (S kuerzt sich raus -> ein globaler Wert). DOS-
       Echtzeit waere 32 (Scroll 0,303), aber unser Scroll laeuft 10% schneller,
       also die Gegner ebenso, damit sie im gleichen Verhaeltnis zum Terrain
       stehen wie im Original. Ketten-`gap` skaliert mit 16/espeed, s. spawn_gap_frames. */
}

/* ===== FPS-Zaehler (Validierung, bis final; Nutzerwunsch 18.07.2026) =====
   Tixus vbc-Methode: die Bildrate ist auf 60/N quantisiert (VBlank-gekoppelt),
   also die VBlanks pro Schleifendurchlauf zaehlen statt ueber ein Zeitfenster
   mitteln (das war der fruehere u8-Wrap-Bug). Angezeigt wird der SCHLECHTESTE
   vbc der letzten ~30 Frames (~1 s) als 60/vbc ??? Aussetzer sofort sichtbar.
   g_fps_shown landet im Score-Feld (score_draw), bis der Zaehler final raus soll.
   fps_tick() einmal pro Frame aus der Hauptschleife (frame_ref = VBCounter beim
   Frame-Start). */
static u8 g_vbc_worst;    /* max VBlanks/Frame seit dem letzten Snapshot */
static u8 g_vbc_fcount;   /* Frames seit dem letzten Snapshot */
static u8 g_fps_shown;
static u8 g_fps_avg;      /* 22.07.2026: DURCHSCHNITT statt schlechtester Frame */    /* = 60/vbc_worst, ~1x/s aktualisiert */
/* Profilierung 22.07.2026: g_fps_shown = 60/worst ist zum MESSEN zu grob - ein
   einziger Ausreisser unter 30 Frames zieht die Anzeige auf 20, und dauerhaft 20
   sieht genauso aus. g_vbc_sum zaehlt stattdessen die VBlanks ueber 30
   Spielframes: 60 = saubere 30 fps, 90 = durchgehend 20 fps, alles dazwischen
   ist linear ablesbar (z.B. 68 = jeder 4. Frame braucht 3 VBlanks). */
static u16 g_vbc_acc;     /* laufende Summe im aktuellen Fenster */
static u16 g_vbc_sum;     /* letzte fertige Summe ueber 30 Frames */
#if FPS_VBC_DISPLAY
/* 29.07.2026 (Nutzerwunsch nach der Messrunde): DURCHSCHNITT der letzten 30
   Messfenster. Ein Fenster = 30 Spielframes (~1 s bei 30 fps), der Ring haelt
   also rund die letzte halbe Minute - statt eine wackelnde Momentanzahl
   abzulesen, steht der Mittelwert direkt in der Anzeige (linke drei Stellen).
   Gleitende Summe statt Neuaufsummieren; u16/u16-Division ist nativ. */
#define VBC_AVG_WIN 30u
static u16 g_vbc_ring[VBC_AVG_WIN];
static u8  g_vbc_ring_i, g_vbc_ring_n;
static u16 g_vbc_ring_tot;
static u16 g_vbc_avg30;
#endif
/* 29.07.2026 PFLICHT-INIT. Alle Zaehler hier sind statics OHNE Initialisierer -
   der Emulator nullt das RAM beim Start, ECHTE HARDWARE NICHT (dieselbe Klasse
   wie die statischen Initialisierer in CLAUDE.md §6; der Emulator kann sie
   prinzipbedingt nicht zeigen). Bei g_vbc_acc/worst/fcount fiel das nie auf,
   weil sie sich nach spaetestens 30 Frames selbst ueberschreiben - der
   Durchschnitts-RING dagegen traegt seinen Startmuell 30 Fenster lang mit sich
   herum, und g_vbc_ring_tot bleibt dauerhaft falsch (gemeldet als "Ø zeigt 004
   bei aktuell 105"). Wird aus game_start() gerufen. */
static void vbc_stats_reset(void) {
#if FPS_VBC_DISPLAY
    u8 k;
    for (k = 0u; k < (u8)VBC_AVG_WIN; k++) g_vbc_ring[k] = 0u;
    g_vbc_ring_i = 0u; g_vbc_ring_n = 0u; g_vbc_ring_tot = 0u; g_vbc_avg30 = 0u;
#endif
    g_vbc_acc = 0u; g_vbc_sum = 0u; g_vbc_worst = 0u; g_vbc_fcount = 0u;
    g_fps_shown = 0u; g_fps_avg = 0u;
}

static void fps_tick(u8 frame_ref) {
    u8 vbc = (u8)(VBCounter - frame_ref);   /* u8-Sub wrappt modular korrekt */
    if (vbc > g_vbc_worst) g_vbc_worst = vbc;
    g_vbc_acc = (u16)(g_vbc_acc + vbc);
    if (++g_vbc_fcount >= 30u) {            /* ~1 s -> Snapshot */
        g_vbc_fcount = 0u;
        g_fps_shown  = (u8)(60u / (g_vbc_worst ? g_vbc_worst : 1u));
        g_vbc_worst  = 0u;
        g_vbc_sum    = g_vbc_acc;
        /* 22.07.2026: DURCHSCHNITTLICHE Bildrate. g_fps_shown rechnet
           60/schlechtester Frame - ein einziger Ausrutscher pro Sekunde zieht
           die Anzeige auf 15, obwohl 29 von 30 Frames sauber laufen. Genau das
           liess eine gemessene Verbesserung von 3-4 auf 2,1-2,5 VBlanks pro
           Frame wie "keine Aenderung" aussehen.
           30 Frames dauern g_vbc_sum VBlanks = g_vbc_sum/60 Sekunden,
           also fps = 30 / (sum/60) = 1800/sum. 16-Bit-Division ist nativ. */
        g_fps_avg    = (u8)(g_vbc_acc ? (1800u / g_vbc_acc) : 0u);
#if FPS_VBC_DISPLAY
        /* 30-Fenster-Durchschnitt fortschreiben (siehe VBC_AVG_WIN).
           29.07.2026, 2. Anlauf: JEDER Schritt einzeln. Die erste Fassung stand
           als ein Ausdruck da (tot - ring[i] + acc, dazu die Division mit
           u8-Operand) und lieferte auf HARDWARE Unsinn ("Ø 004 bei aktuell
           105"), im Emulator dagegen korrekt - das ist das Muster der
           cc900-Fallen aus CLAUDE.md §6 (verschachtelte Ausdruecke splitten,
           kein Subscript in zusammengesetzter Arithmetik). Nicht schoen, aber
           die Toolchain hat hier die Regeln. */
        { u16 alt = g_vbc_ring[g_vbc_ring_i];
          u16 tot = g_vbc_ring_tot;
          u16 n16;
          tot = (u16)(tot - alt);
          tot = (u16)(tot + g_vbc_acc);
          g_vbc_ring_tot = tot;
          g_vbc_ring[g_vbc_ring_i] = g_vbc_acc;
          g_vbc_ring_i++;
          if (g_vbc_ring_i >= (u8)VBC_AVG_WIN) g_vbc_ring_i = 0u;
          if (g_vbc_ring_n < (u8)VBC_AVG_WIN) g_vbc_ring_n++;
          n16 = (u16)g_vbc_ring_n;
          if (n16 == 0u) n16 = 1u;
          g_vbc_avg30 = (u16)(tot / n16); }
#endif
        g_vbc_acc    = 0u;
    }
}

/* Sprite-Streifen-Index i (0-basiert) mit lvl_sspr_anim_head[i]==n suchen
   (Schritt 8/9). Fallback 0: es gibt aktuell nur eine Animation. */
static u8 sspr_anim_index_for(u16 n) {
    u8 i;
    for (i = 0u; i < (u8)LVL_SSPR_ANIM_COUNT; i++)
        if (lvl_sspr_anim_head[i] == n) return i;
    return 0u;
}

/* Schubduesen laufen NUR bei tatsaechlich gedruecktem HOCH/RUNTER (Nutzer-
   korrektur 07.07.2026: vorher liefen sie dauerhaft) ??? nicht an den Auto-
   Scroll oder den Rueckwaertsflug-Status gekoppelt. g_thrust_dir: 0=aus,
   1=vorwaerts (HOCH), 2=rueckwaerts (RUNTER); HOCH gewinnt bei beiden. */
static void thrust_update(void) {
    u16 head;
    if (g_pad & J_UP)        { g_thrust_dir = 1u; head = SPR_S_THRUST_HEAD; }
    else if (g_pad & J_DOWN) { g_thrust_dir = 2u; head = SPR_S_THRUST_HEAD; }
    else { g_thrust_dir = 0u; g_thrust_tick = 0u; g_thrust_frame = 0u; return; }

    /* Performance-Fix 11.07.2026: Ergebnis in g_thrust_aidx ablegen, damit
       draw_sprites() im selben Frame nicht dieselben 12 Anim-Koepfe nochmal
       durchsuchen muss (siehe g_thrust_aidx-Kommentar bei der Deklaration). */
    g_thrust_aidx = sspr_anim_index_for(head);
    g_thrust_tick++;
    if (g_thrust_tick >= lvl_sspr_anim_speed[g_thrust_aidx]) {
        g_thrust_tick = 0u;
        g_thrust_frame++;
        if (g_thrust_frame >= lvl_sspr_anim_len[g_thrust_aidx]) g_thrust_frame = 0u;
    }
}

/* Mehrteiliger Metasprite-Gegner spawnen (Schritt 11, "mehrteilige animierte
   Gegner auf Bewegungspfaden") ??? n/is_anim wie bei normalen Spawns dekodiert,
   aber bei gesetztem Bit 12 (siehe enemies_update) ist n 0-basiert und meint
   NICHT eine S-Nummer, sondern einen Metasprite- bzw. Metaanim-Kopf-Index. */
/* Rueckgabe 1 = Instanz gespawnt, 0 = kein freier Slot (Bugfix 11.07.2026:
   der Aufrufer darf in diesem Fall den Ketten-Spawn-Versuch NICHT als
   verbraucht zaehlen, sonst gehen Kettenglieder spurlos verloren, siehe
   SPR_METAENEMY_0-Kommentar). */
static u8 metaenemy_spawn(u8 s, u16 n, u8 is_anim) {
    u8 i;
    for (i = 0u; i < (u8)MAX_METAENEMIES; i++) {
        if (g_metaenemies[i].active) continue;
        g_metaenemies[i].active    = 1u;
        g_metaenemies[i].spawn_x   = lvl_spawn_x[s];   /* s16, kein u8-Cast: siehe TEnemy.spawn_x */
        g_metaenemies[i].spawn_y   = (s16)(lvl_spawn_y[s] - 8);   /* 25.07.2026 Nutzerwunsch: Metasprite-Pfade 8px hoeher */
        g_metaenemies[i].path      = lvl_spawn_path[s];
        g_metaenemies[i].x_fix     = 0;
        g_metaenemies[i].y_fix     = 0;
        g_metaenemies[i].frame     = 0u;
        g_metaenemies[i].pp        = PATH_PTR(g_metaenemies[i].path, 0u);
        g_metaenemies[i].path_len  = lvl_path_len[g_metaenemies[i].path];
        g_metaenemies[i].x         = (u8)g_metaenemies[i].spawn_x;
        g_metaenemies[i].y         = (u8)g_metaenemies[i].spawn_y;
        g_metaenemies[i].off_timer = 0u;
        g_metaenemies[i].path_acc  = 0u;
        g_metaenemies[i].spawn_idx = s;
        g_metaenemies[i].health    = lvl_spawn_health[s];   /* Health/Schaden-System 19.07.2026 */
        g_metaenemies[i].flash     = 0u;                    /* 23.07.2026: kein Alt-Flash aus einem frueheren Slot-Nutzer */
        g_metaenemies[i].c_cached_idx = 0xFFFFu;            /* 25.07.2026: Zell-Cache des Vornutzers verwerfen */
        g_metaenemies[i].chain_base   = OAM_NONE;           /* 25.07.2026: Chaining-Block des Vornutzers verwerfen */
        g_metaenemies[i].chain_cells  = 0u;
        {
            u8 c;
            for (c = 0u; c < (u8)META_CELLS; c++) {
                g_metaenemies[i].oam[c] = OAM_NONE;
                g_metaenemies[i].oam_b[c] = OAM_NONE;   /* b-Overlay-Slot, 21.07.2026 */
                g_metaenemies[i].last_snum[c] = 0u;
            }
        }
        /* Performance-Fix 11.07.2026, siehe hitzone_resolve/hit_test_cached
           und TEnemy.hz_dx-Kommentar. */
        hitzone_resolve(lvl_spawn_spr[s], ENEMY_HIT_FALLBACK_W, ENEMY_HIT_FALLBACK_H,
                         &g_metaenemies[i].hz_dx, &g_metaenemies[i].hz_dy,
                         &g_metaenemies[i].hz_w, &g_metaenemies[i].hz_h);
        if (is_anim) {
            u8 h;
            for (h = 0u; h < (u8)LVL_METAANIM_COUNT; h++)
                if (lvl_metaanim_head[h] == n) break;
            g_metaenemies[i].metaanim   = h;
            g_metaenemies[i].anim_tick  = 0u;
            g_metaenemies[i].anim_frame = 0u;
            g_metaenemies[i].meta_idx   = lvl_metaanim_frames[h][0];
        } else {
            g_metaenemies[i].metaanim = 0xFFFFu;
            g_metaenemies[i].meta_idx = n;
        }
        return 1u;
    }
    return 0u;
}

/* Wurm-Kette spawnen (Schritt 12 "Rotationssaetze & Wurm-Ketten") ??? n ist hier
   der Rotationssatz-Index (0-basiert, wie beim Metasprite-Kopf). Alle
   lvl_spawn_worm_segs[s] Segmente entstehen SOFORT in einem Aufruf (anders als
   ein Xenon-Ketten-Spawn, der ueber mehrere enemies_update()-Ticks verteilt
   ist) ??? Segment k startet mit lvl_spawn_worm_delay*k Frames Verzug (frame
   negativ) und holt automatisch auf, weil jedes Segment ganz normal jeden
   Frame frame++ zaehlt (siehe worms_update). Rueckgabe wie metaenemy_spawn:
   1 = gespawnt, 0 = kein freier Wurm-Slot. */
static u8 worm_spawn(u8 s, u16 rot) {
    u8 w, k, segs;
    for (w = 0u; w < (u8)MAX_WORMS; w++) {
        if (g_worms[w].active) continue;
        segs = lvl_spawn_worm_segs[s];
        if (segs > (u8)MAX_WORM_SEGS) segs = (u8)MAX_WORM_SEGS;   /* Bounds-Schutz,
            siehe spr_vram-Kommentar: kuenftiger Export koennte mehr liefern */
        g_worms[w].active          = 1u;
        g_worms[w].spawn_x         = lvl_spawn_x[s];   /* s16, siehe TWorm-Kommentar */
        g_worms[w].spawn_y         = lvl_spawn_y[s];
        g_worms[w].path            = lvl_spawn_path[s];
        g_worms[w].rot             = (u8)rot;
        g_worms[w].scroll_at_spawn = g_scroll_y;
        g_worms[w].num_segs        = segs;
        g_worms[w].delay           = lvl_spawn_worm_delay[s];
        g_worms[w].spawn_idx       = s;
        /* Rotationssatz-Hitzone: kein Eintrag fuer 0x2000|rot in lvl_hitzone_spr[]
           vorgesehen (gilt fuer alle 8 Richtungen gleich, Anweisung Schritt 16) ???
           hitzone_resolve() faellt automatisch auf die volle Sprite-Flaeche
           zurueck, exakt wie dokumentiert. */
        hitzone_resolve(lvl_spawn_spr[s], ENEMY_HIT_FALLBACK_W, ENEMY_HIT_FALLBACK_H,
                         &g_worms[w].hz_dx, &g_worms[w].hz_dy,
                         &g_worms[w].hz_w, &g_worms[w].hz_h);
        for (k = 0u; k < segs; k++) {
            g_worms[w].seg[k].alive = 1u;
            g_worms[w].seg[k].frame = (s16)(-(s16)((u16)g_worms[w].delay * k));
            g_worms[w].seg[k].x_fix = 0;
            g_worms[w].seg[k].y_fix = 0;
            g_worms[w].seg[k].x     = (u8)g_worms[w].spawn_x;
            g_worms[w].seg[k].y     = (u8)g_worms[w].spawn_y;
            g_worms[w].seg[k].off_timer = 0u;
            g_worms[w].seg[k].oam       = OAM_NONE;
            g_worms[w].seg[k].last_snum = 0u;
            g_worms[w].seg[k].last_flip = 0xFFu;
        }
        return 1u;
    }
    return 0u;
}

/* --- Gegner aktualisieren + spawnen ---
   Bewegung ueber vorkompilierte, screen-absolute Pfade (lvl_path_*, Schritt 7),
   Grafik ueber eine pro Instanz laufende Sprite-Animation (lvl_sspr_anim_*,
   Schritt 8) ODER ein Standbild (seit 06.07.2026, 3. Runde ??? Spawn 5 nutzt
   spr=0x8001, Bit 14=0 = "Standbild Sprite-Streifen"). Spawns (lvl_spawn_*,
   Schritt 9) loesen aus, sobald ihre Map-Zeile eingerollt ist (g_lvl_row);
   `count` Gegner im Abstand von `gap` Frames auf demselben Pfad (Xenon-Kette,
   Schritt 9 Ende).
   Von den 4 Faellen aus der ANWEISUNG sind jetzt 3 belegt: "animierter
   Sprite-Streifen" (spr=0xC000|n), "Standbild Sprite-Streifen" (spr=0x8000|n)
   UND "Metasprite" (Bit 12 gesetzt, siehe metaenemy_spawn, Map-Update
   07.07.2026 Nacht ??? vorher f??lschlich als normale Sprite-Streifen-Animation
   behandelt, sichtbar als falsches/fehlendes Sprite) ??? "Map-Tile-als-Sprite"
   (Bit 15=0, Bit 12=0) bleibt unbenutzt und ist weiterhin nicht implementiert. */
/* Sichtbarer Ketten-Abstand = gap_frames * (espeed/16) * spawn.speed. Die
   lvl_spawn_gap-Werte sind fuer espeed=16 (1 Entry/Frame) kalibriert; bei
   hoeherem espeed waechst der Abstand proportional mit dem Tempo mit (Nutzer
   19.07.: "Abstand zu gross" nach dem Speed-Fix). Gegengewicht: gap * 16/espeed,
   gerundet. So bleibt der Abstand konstant, egal wie espeed getunt wird. */
static u8 spawn_gap_frames(u8 s) {
    u16 g, num, half;
    g = (u16)lvl_spawn_gap[s];
    if (g == 0u) return 0u;
    half = (u16)(g_tune_espeed >> 1);
    num  = (u16)(g * 16u);
    num  = (u16)(num + half);
    return (u8)(num / (u16)g_tune_espeed);
}

static void enemies_update(void) {
    u8 i, s;

    /* Fix 16.07.2026 (Nutzerbericht "zu viele Gegner am Anfang"): Spawns
       wurden gegen g_lvl_row getriggert, das nach lvl1_prefill() aber schon
       auf 19 steht (row_base+19) -- alle Spawns mit Trigger-Zeile < 19 feuerten
       dadurch SOFORT beim Start gleichzeitig. Fix: eigener Trigger-Zaehler
       g_spawn_scroll_row, startet bei 0 und zaehlt gleichmaessig mit dem Scroll
       hoch (alle 8*SCROLL_TICK_THRESHOLD Frames = eine Map-Zeile), entkoppelt
       vom Prefill-Offset. In game_start() auf 0 gesetzt. */
    /* Fix 19.07.2026 (Nutzer "zu viele Gegnerwellen, ab der Mitte komisch, stimmt
       nicht zum Original"): Trigger an die ECHTE Scroll-POSITION koppeln, nicht an
       einen Frame-Zeitzaehler. Der alte g_spawn_row_frame_acc zaehlte pro FRAME
       hoch (8*g_tune_scroll Frames = 1 Zeile) ??? driftete aber dem tatsaechlichen
       Scroll VORAUS, sobald der stockt (Waende/eingeklemmtes Schiff, Rueckwaerts-
       flug, terrainlastige 2. Haelfte, Levelende-Stop): der Zeitzaehler lief weiter,
       g_scroll_y nicht -> Wellen feuerten zu FRUEH und haeuften sich. g_scroll_y ist
       die reine Vorwaerts-Pixeldistanz (folgt auch dem Rueckwaertsflug), >>3 = echte
       gescrollte Map-Zeile == genau die Konvention von lvl_spawn_row (Tool: sp.row).
       Damit feuert jede Welle exakt an ihrer Kartenzeile wie im Original. */
    {
        u16 cur_row = (u16)(g_scroll_y >> 3);
        if (cur_row != g_spawn_scroll_row) {
            g_spawn_scroll_row = cur_row;
            g_spawn_row_changed = 1u;
        }
    }

    /* Trigger-Scan (ROM-Zugriff lvl_spawn_row) NUR bei Zeilenwechsel ??? sonst
       kann kein Spawn neu faellig werden. Spart 140 ROM-Reads auf 47 von 48
       Frames (Performance 17.07.2026, siehe g_spawn_row_changed). */
    if (g_spawn_row_changed) {
        /* 23.07.2026: Vorlauf-RAMPE gegen den Start-Burst UND gegen fehlende
           Startwellen. Bei vollem +12-Vorlauf haetten am Levelstart (scroll_row 0)
           ALLE Wellen der Zeilen 0..12 zugleich gefeuert (Burst); sie ganz zu
           ueberspringen liess sie fehlen. Stattdessen laeuft die Spawn-Linie vom
           Schiff (Vorlauf 0) in den ersten 12 gescrollten Zeilen auf +12 hoch:
           jede Startwelle feuert gestaffelt an ihrer echten Zeile, sobald die Linie
           sie erreicht (monoton, kein Ruecklauf). Ab scroll_row >= 12 - und damit
           nach jedem Checkpoint-Respawn - gilt sofort der volle Vorlauf. */
        u16 lead = (g_spawn_scroll_row < (u16)SPAWN_LEAD_ROWS)
                       ? g_spawn_scroll_row : (u16)SPAWN_LEAD_ROWS;
        g_spawn_row_changed = 0u;
        for (s = 0u; s < (u8)LVL_SPAWN_COUNT; s++) {
            if (!g_spawn_fired[s] && (u16)(g_spawn_scroll_row + lead) >= (u16)(lvl_spawn_row[s] + (u16)SPAWN_TRIGGER_DELAY)) {
                g_spawn_fired[s] = 1u;
                g_spawn_left[s]  = lvl_spawn_count[s];
                if (g_spawn_left[s]) g_spawn_pending++;   /* siehe g_spawn_pending */
                g_spawn_timer[s] = 0u;
#if WORM_TEST_MODE
                g_cur_wave = s;   /* Testphase: zuletzt getriggerte Welle (Spawn-Index) fuer HUD */
#endif
            }
        }
    }

    /* 22.07.2026: diese Schleife lief JEDEN Frame ueber alle 113 Wellen, auch
       wenn ueberhaupt keine mehr Mitglieder ausstehen hatte - also fast immer.
       g_spawn_pending zaehlt mit, wie viele Wellen noch offen sind; ist es 0,
       faellt die ganze Schleife weg. */
    if (g_spawn_pending)
    for (s = 0u; s < (u8)LVL_SPAWN_COUNT; s++) {
        if (g_spawn_left[s] == 0u) continue;
        if (g_spawn_timer[s] == 0u) {
            u16 spr      = lvl_spawn_spr[s];
            u16 n        = spr & 0x01FFu;
            u8  is_anim  = (u8)((spr >> 14) & 1u);
            u8  is_meta  = (u8)((spr >> 12) & 1u);
            u8  is_rot   = (u8)((spr >> 13) & 1u);   /* Schritt 12: Rotationssatz/Wurm */
            u8  spawned;   /* Deklaration am Blockanfang, Zuweisung spaeter (cc900-Codegen) */
            /* Bugfix 15.07.2026, beim 16.07-Reset verlorengegangen und hier
               wiederhergestellt: 109 der 140 Spawns haben laut Tool-Warnung noch
               keine Grafik (lvl_spawn_spr[s]==0). Ohne diesen Guard laufen sie
               alle durch den normalen Gegner-Zweig (is_meta/is_rot sind bei 0
               ebenfalls 0) und richten zweierlei Schaden an:
               1. Sie belegen unsichtbar Slots im nur 10 Eintraege grossen
                  MAX_ENEMIES-Pool und lassen damit ECHTE Gegner verhungern
                  (Simulation gegen die Exportdaten: 366 statt 80 still
                  verlorene Kettenglieder ueber das Level).
               2. draw_sprites() rechnet s_num-1 zu 0xFFFF und liest
                  lvl_sspr_b_idx[0xFFFF] weit ausserhalb des Arrays.
               Regel wie spawn_resolve() in der ANWEISUNG, Schritt 9: die
               gesamte Kette dieses Spawns von vornherein ueberspringen. */
            if (spr == 0u) {
                if (g_spawn_left[s] && g_spawn_pending) g_spawn_pending--;

                g_spawn_left[s] = 0u;
                continue;
            }
            if (is_rot) {
                /* Gleiches Muster wie beim Metasprite-Zweig: nur bei
                   erfolgreichem Spawn als verbraucht zaehlen (kein freier
                   Wurm-Slot -> naechsten Frame erneut versuchen). */
                if (worm_spawn(s, n)) {
                    g_spawn_left[s]--;

                    if (g_spawn_left[s] == 0u && g_spawn_pending) g_spawn_pending--;
                    g_spawn_timer[s] = spawn_gap_frames(s);
                }
                continue;
            }
            if (is_meta) {
                /* Bugfix 11.07.2026: nur bei ERFOLGREICHEM Spawn als
                   verbraucht zaehlen ??? sonst blieb g_spawn_timer auf 0
                   stehen (kein freier Slot), versucht also naechsten Frame
                   erneut, statt das Kettenglied stillschweigend zu
                   verlieren (siehe metaenemy_spawn/SPR_METAENEMY_0). */
                if (metaenemy_spawn(s, n, is_anim)) {
                    g_spawn_left[s]--;

                    if (g_spawn_left[s] == 0u && g_spawn_pending) g_spawn_pending--;
                    g_spawn_timer[s] = spawn_gap_frames(s);
                }
                continue;
            }
            spawned = 0u;
            for (i = 0u; i < MAX_ENEMIES; i++) {
                if (!g_enemies[i].active) {
                    /* Formation (Original spacing>=100, 19.07.2026): die Mitglieder
                       einer Rang-Welle stehen NEBENEINANDER statt hintereinander ???
                       jedes k-te Mitglied startet um k*lvl_spawn_xstep px in X
                       versetzt (0 = normaler Stream auf demselben Pfad). k = bereits
                       gespawnte Mitglieder = count - verbleibende. */
                    u8 k = (u8)(lvl_spawn_count[s] - g_spawn_left[s]);
                    g_enemies[i].active     = 1u;
                    g_enemies[i].spawn_x    = (s16)(lvl_spawn_x[s] + (s16)((s16)k * (s16)lvl_spawn_xstep[s]));   /* s16, kein u8-Cast: siehe TEnemy.spawn_x */
                    g_enemies[i].spawn_y    = lvl_spawn_y[s];
                    g_enemies[i].path       = lvl_spawn_path[s];
                    g_enemies[i].x_fix      = 0;
                    g_enemies[i].y_fix      = 0;
                    g_enemies[i].frame      = 0u;
                    g_enemies[i].pp         = PATH_PTR(g_enemies[i].path, 0u);
                    /* 22.07.2026: konstante Spawn-Werte EINMAL aus dem ROM holen.
                       Auf echter Hardware kostet jeder Modul-Lesezugriff Wartezyklen -
                       im Emulator faellt das nicht auf (dort braucht das Spiel 0,5
                       VBlanks, auf Hardware ueber 2). Diese vier wurden bisher fuer
                       JEDEN Gegner in JEDEM Frame neu gelesen, obwohl sie sich nie
                       aendern. */
                    g_enemies[i].path_len     = lvl_path_len[g_enemies[i].path];
                    g_enemies[i].c_fire_rate  = lvl_spawn_fire_rate[s];
                    g_enemies[i].is_static  = (u8)(is_anim ? 0u : 1u);
                    if (is_anim) {
                        g_enemies[i].anim_idx   = sspr_anim_index_for(n);
                        g_enemies[i].anim_tick  = 0u;
                        g_enemies[i].anim_frame = 0u;
                        g_enemies[i].c_anim_len   = lvl_sspr_anim_len[g_enemies[i].anim_idx];
                        g_enemies[i].c_anim_speed = lvl_sspr_anim_speed[g_enemies[i].anim_idx];
                    } else {
                        g_enemies[i].spr_num = n;   /* bereits 1-basierte S-Nummer */
                    }
                    g_enemies[i].x          = (u8)g_enemies[i].spawn_x;
                    g_enemies[i].y          = (u8)g_enemies[i].spawn_y;
                    g_enemies[i].off_timer  = 0u;
                    g_enemies[i].spawn_idx  = s;
                    g_enemies[i].health     = lvl_spawn_health[s];   /* Health/Schaden-System 19.07.2026 */
                    g_enemies[i].flash      = 0u;                    /* 23.07.2026: kein Alt-Flash aus einem frueheren Slot-Nutzer */
                    /* Zufaelliger Startwert (Original: "beliebig/zufaellig") ??? sonst
                       feuert eine ganze Kette mit gleicher Rate im Gleichschritt. */
                    g_enemies[i].fire_acc   = QRandom();
                    g_enemies[i].path_acc   = 0u;
                    /* Slot-Felder zuruecksetzen. Freigegeben werden die OAM-Slots
                       beim TOD (enemy_take_damage) bzw. vom Zeichendurchlauf -
                       NICHT hier: ein Versuch am 27.07.2026, an dieser Stelle
                       aufzuraeumen, hat den Pool zerlegt (die Kanone verschwand
                       komplett). Der Zeitpunkt hier ist zu unscharf. */
                    g_enemies[i].oam0 = g_enemies[i].oam1 = OAM_NONE;
                    g_enemies[i].last_snum  = 0u;
                    g_enemies[i].chain_b    = 0u;   /* 25.07.2026: siehe TEnemy.chain_b */
                    /* Performance-Fix 11.07.2026: Hitzone einmal beim Spawn
                       aufloesen statt bei jedem Kollisionstest, siehe
                       hitzone_resolve/hit_test_cached. */
                    hitzone_resolve(lvl_spawn_spr[s], ENEMY_HIT_FALLBACK_W, ENEMY_HIT_FALLBACK_H,
                                     &g_enemies[i].hz_dx, &g_enemies[i].hz_dy,
                                     &g_enemies[i].hz_w, &g_enemies[i].hz_h);
                    spawned = 1u;
                    break;
                }
            }
            /* Bugfix 15.07.2026, beim 16.07-Reset verlorengegangen und hier
               wiederhergestellt: nur bei ERFOLGREICHEM Spawn als verbraucht
               zaehlen. Vorher lief g_spawn_left[s]-- unconditional ??? war der
               Pool gerade voll, fand die Schleife keinen Slot, das Kettenglied
               galt aber trotzdem als verbraucht und war spurlos weg. Der Meta-
               (metaenemy_spawn) und der Wurm-Zweig (worm_spawn) machen es seit
               11.07. schon richtig; nur der normale Gegner-Zweig hier nicht.
               Jetzt bleibt g_spawn_timer auf 0 -> naechster Frame versucht es
               erneut, sobald ein Slot frei wird. */
            if (spawned) {
                g_spawn_left[s]--;

                if (g_spawn_left[s] == 0u && g_spawn_pending) g_spawn_pending--;
                g_spawn_timer[s] = spawn_gap_frames(s);
            }
        } else {
            g_spawn_timer[s]--;
        }
    }

    for (i = 0u; i < MAX_ENEMIES; i++) {
        u16 len, off;
        u8 anim_len;
        if (!g_enemies[i].active) continue;
        g_busy_enemies = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */

        len = g_enemies[i].path_len;   /* 22.07.2026: aus dem Cache, nicht aus dem ROM */
        if (g_enemies[i].frame >= len) {
            g_enemies[i].active = 0u;
            continue;
        }
        /* Tempo-Multiplikator (17.07.2026): g_tune_espeed/16 Pfadschritte pro Frame.
           Bei 16 exakt ein Schritt = unveraendertes Originalverhalten. Bei >16 kann
           es mehrere pro Frame sein, deshalb die Schleife ??? jeder Schritt muss sein
           Delta wirklich anwenden, sonst verformt sich die Bahn. Pfadende wird
           innerhalb der Schleife geprueft. */
        g_enemies[i].path_acc = (u8)(g_enemies[i].path_acc + g_tune_espeed);
        while (g_enemies[i].path_acc >= 16u) {
            g_enemies[i].path_acc = (u8)(g_enemies[i].path_acc - 16u);
            if (g_enemies[i].frame >= len) { g_enemies[i].active = 0u; break; }
            /* Optimierung 22.07.2026, siehe PATH_PTR: Zeiger statt 32-Bit-Index */
            g_enemies[i].x_fix = (s16)(g_enemies[i].x_fix + g_enemies[i].pp[0]);
            g_enemies[i].y_fix = (s16)(g_enemies[i].y_fix + g_enemies[i].pp[1]);
            g_enemies[i].pp += 2;
            g_enemies[i].frame++;
        }
        if (!g_enemies[i].active) continue;
        g_enemies[i].x = (u8)(g_enemies[i].spawn_x + (g_enemies[i].x_fix >> 4));
        g_enemies[i].y = (u8)(g_enemies[i].spawn_y + (g_enemies[i].y_fix >> 4));

        if (!g_enemies[i].is_static) {
            anim_len = g_enemies[i].c_anim_len;         /* aus dem Cache */
            g_enemies[i].anim_tick++;
            if (g_enemies[i].anim_tick >= g_enemies[i].c_anim_speed) {
                g_enemies[i].anim_tick = 0u;
                g_enemies[i].anim_frame++;
                if (g_enemies[i].anim_frame >= anim_len) g_enemies[i].anim_frame = 0u;
            }
        }

        /* Kein sofortiges Despawn bei y>=CLIP_Y: der Pfad darf den Bildschirm
           kurz verlassen und wieder hereinkommen (Loop). Aber die Pfade sind
           jetzt teils >3000 Frames lang ??? ohne Zeitlimit wuerde ein dauerhaft
           abgeflogener Gegner seinen Slot fuer den ganzen Rest des Pfads
           blockieren. Daher: Slot erst freigeben, wenn der Gegner
           ENEMY_OFFSCREEN_GRACE Frames AM STUECK ausserhalb war.
           WICHTIG (Bugfix 10.07.2026): NICHT gegen g_enemies[i].x/y (u8)
           testen ??? spawn_x ist oft leicht negativ (Anflug von links) und
           manche Pfade haben kurze Passagen mit leicht negativem y (Flug
           knapp ueber dem oberen Rand, Sprite bleibt fast komplett sichtbar).
           Der u8-Wrap macht daraus faelschlich einen SEHR GROSSEN Wert
           (-1 -> 255), der die =>CLIP_Y/SCR_W-Pruefung ausloest, obwohl der
           Gegner die ganze Zeit sichtbar war ??? sichtbar als "Gegner
           verschwindet/despawnt obwohl Pfad laut Tool im Bild bleibt" bzw.
           als Kettenglieder, die nie einen Slot frei bekommen. Stattdessen
           mit der VORZEICHENBEHAFTETEN Position rechnen und erst ab einer
           Marge von 16px (typische Sprite-Groesse) als "off" werten.
           Bugfix 17.07.2026: spawn_x/y sind jetzt echtes s16 (siehe
           TEnemy.spawn_x) ??? der frueher hier noetige (s16)(s8)-Trick ist weg,
           er hat alle Spawns mit echtem x/y >=128 zerstoert. */
        {
            s16 real_x = (s16)(g_enemies[i].spawn_x + (g_enemies[i].x_fix >> 4));
            s16 real_y = (s16)(g_enemies[i].spawn_y + (g_enemies[i].y_fix >> 4));
            if (real_x <= -16 || real_x >= (s16)SCR_W || real_y <= -16 || real_y >= (s16)CLIP_Y) {
                g_enemies[i].off_timer++;
                if (g_enemies[i].off_timer >= (u8)ENEMY_OFFSCREEN_GRACE) {
                    g_enemies[i].active = 0u;
                }
            } else {
                g_enemies[i].off_timer = 0u;
            }
        }
        /* Schritt 18: wie Kollision/Zeichnen ueberspringt auch das Feuern
           off-screen Gegner (gleicher CLIP_Y-Fastpath wie ueberall sonst). */
        if (g_enemies[i].active && g_enemies[i].y < (u8)CLIP_Y) enemy_fire_tick(&g_enemies[i]);
    }
}

#if BENCH_DETERM
/* 24.07.2026 Deterministischer Gegner-Benchmark (siehe BENCH_DETERM): haelt JEDEN
   Frame exakt BENCH_DETERM_N Gegner an festen Positionen. Tempo 0 -> sie stehen
   (kein Pfad-Despawn), on-screen (kein Offscreen-Despawn), c_fire_rate 0 (feuern
   nicht) -> die Sprite- UND Update-Last ist KONSTANT. Selbstheilend: nach einem
   respawn_do()-Wisch werden sie im naechsten Frame neu gesetzt. So liefern die
   Profiling-Blocke (Block 2 Zeichnen / Block 9 Gegner-Update) wiederholbare Zahlen. */
#if BENCH_DETERM_WORM
#define BENCH_DETERM_N 0u             /* Wurmband-Variante: keine festen Gegner, nur Wuermer */
#else
#define BENCH_DETERM_N MAX_ENEMIES    /* 10 = voller Gegner-Pool */
#endif
static void bench_determ_tick(void) {
    static u16 s_spr; static u8 s_n, s_anim, s_ready;
    u8 i;
    if (!s_ready) {
        u8 s; s_spr = 0u;
        for (s = 0u; s < (u8)LVL_SPAWN_COUNT; s++) {
            u16 sp = lvl_spawn_spr[s];
            if ((sp & 0x01FFu) && !((sp >> 12) & 1u) && !((sp >> 13) & 1u)) { s_spr = sp; break; }
        }
        if (!s_spr) s_spr = 1u;
        s_n = (u8)(s_spr & 0x01FFu); s_anim = (u8)((s_spr >> 14) & 1u);
        /* ALLE Wellen als bereits gefeuert markieren -> der Trigger-Scan in
           enemies_update schaerft beim Hochfliegen keine Meta-/Wurm-Wellen mehr
           (die haben EIGENE Pools, mein voller g_enemies-Pool blockt sie nicht). */
        { u16 w; for (w = 0u; w < (u16)LVL_SPAWN_COUNT; w++) { g_spawn_fired[w] = 1u; g_spawn_left[w] = 0u; } }
        s_ready = 1u;
    }
    g_tune_espeed   = 0u;   /* Gegner bewegen sich nicht -> feste Positionen, kein Pfadende */
    g_spawn_pending = 0u;   /* keine weiteren Spawns (normal/Meta/Wurm) mehr faellig */
    g_spawn_row_changed = 0u;   /* Trigger-Scan gar nicht erst ausloesen */
    for (i = 0u; i < (u8)BENCH_DETERM_N; i++) {
        TEnemy *e = &g_enemies[i];
        if (e->active) continue;   /* laeuft/animiert schon */
        e->active  = 1u;
        e->spawn_x = (s16)(20 + (s16)((u16)(i % 5u) * 28u));
        e->spawn_y = (s16)(24 + (s16)((u16)(i / 5u) * 40u));
        e->x_fix = 0; e->y_fix = 0; e->frame = 0u;
        e->path = 0u; e->pp = PATH_PTR(0u, 0u); e->path_len = 30000u;   /* nie Pfadende (Tempo 0 -> pp nie dereferenziert) */
        e->path_acc = 0u; e->c_fire_rate = 0u; e->c_needs_b = 0u;
        e->is_static = (u8)(s_anim ? 0u : 1u);
        if (s_anim) {
            e->anim_idx = sspr_anim_index_for((u16)s_n);
            e->anim_tick = 0u; e->anim_frame = 0u;
            e->c_anim_len = lvl_sspr_anim_len[e->anim_idx];
            e->c_anim_speed = lvl_sspr_anim_speed[e->anim_idx];
        } else {
            e->spr_num = (u16)s_n;
        }
        e->x = (u8)e->spawn_x; e->y = (u8)e->spawn_y;
        e->off_timer = 0u; e->spawn_idx = 0u; e->health = 200u;
        e->flash = 0u; e->fire_acc = 0u;
        e->oam0 = e->oam1 = OAM_NONE; e->last_snum = 0u; e->chain_b = 0u;
        hitzone_resolve(s_spr, ENEMY_HIT_FALLBACK_W, ENEMY_HIT_FALLBACK_H,
                        &e->hz_dx, &e->hz_dy, &e->hz_w, &e->hz_h);
    }
}
#endif

#if BENCH_META
/* 25.07.2026 METASPRITE-BENCHMARK (siehe BENCH_META). Haelt JEDEN Frame exakt
   BENCH_META_N Alien-Kaefer aktiv, die auf einer festen Kreisbahn um die
   Bildmitte fliegen. Warum so und nicht ueber Pfade/Spawns:
     - Pfadfortschritt 0 (g_tune_espeed) -> kein Pfadende, kein Despawn; die
       Position setze ich selbst aus der Kreistabelle (spawn_x/y, das Update
       rechnet daraus x/y).
     - Health hoch + Neusetzen im naechsten Frame, falls doch mal einer wegfaellt
       (z.B. nach einem respawn_do()-Wisch) -> selbstheilend.
     - Alle Wellen als gefeuert markiert -> es kommt NICHTS dazu.
   Die Bild-Animation laeuft weiter (anim_tick haengt nicht am Tempo), der
   Zeichenpfad ist also der echte, inklusive Zell-Cache-Verwerfen beim
   Bildwechsel. Ein Umlauf = 64 Frames (~2 s bei 30 fps). */
/* Ellipse statt Kreis (25.07.2026): auf einer engen Kreisbahn um die Bildmitte
   verschwanden die Kaefer hinter dem Boss-Koerper. Breit und flach verteilt
   fliegen alle sieben sichtbar durchs freie Feld. */
static const s8 BENCH_CIRC_X[32] = {
     52,  51,  48,  43,  37,  29,  20,  10,   0, -10, -20, -29, -37, -43, -48, -51,
    -52, -51, -48, -43, -37, -29, -20, -10,   0,  10,  20,  29,  37,  43,  48,  51 };
static const s8 BENCH_CIRC_Y[32] = {
      0,   7,  13,  19,  24,  28,  31,  33,  34,  33,  31,  28,  24,  19,  13,   7,
      0,  -7, -13, -19, -24, -28, -31, -33, -34, -33, -31, -28, -24, -19, -13,  -7 };
#define BENCH_CIRC_CX 72   /* Metasprite-Anker (links oben), 16x16 -> Bahn 20..140 px */
#define BENCH_CIRC_CY 58   /* Bahn 24..108 px, bleibt ueber der HUD-Bar (CLIP_Y=136) */

static void bench_meta_tick(void) {
    static u16 s_n;
    static u8  s_s, s_anim, s_ready, s_phase;
    u8 i;
    if (!s_ready) {
        u8 s, found = 0u;
        s_s = 0u; s_n = 0u; s_anim = 0u;
        /* Metasprite-Welle aus der Map suchen (Bit 12 = Metasprite, Bit 14 =
           animiert). Bevorzugt die mit VIER Zellen - das ist der Alien-Kaefer;
           die 7-Zellen-Kombination waere ein anderer Gegner. Datengetrieben,
           damit ein map.h-Re-Export den Benchmark nicht still umstellt. */
        for (s = 0u; s < (u8)LVL_SPAWN_COUNT; s++) {
            u16 sp = lvl_spawn_spr[s];
            u16 n, idx;
            u8  an;
            if (!((sp >> 12) & 1u)) continue;
            n   = (u16)(sp & 0x01FFu);
            an  = (u8)((sp >> 14) & 1u);
            idx = n;
            if (an) {
                u8 h;
                for (h = 0u; h < (u8)LVL_METAANIM_COUNT; h++)
                    if (lvl_metaanim_head[h] == n) break;
                if (h >= (u8)LVL_METAANIM_COUNT) continue;
                idx = lvl_metaanim_frames[h][0];
            }
            if (!found) { s_s = s; s_n = n; s_anim = an; found = 1u; }   /* Rueckfall */
            if (lvl_meta_count[idx] == 4u) { s_s = s; s_n = n; s_anim = an; break; }
        }
        { u16 w; for (w = 0u; w < (u16)LVL_SPAWN_COUNT; w++) { g_spawn_fired[w] = 1u; g_spawn_left[w] = 0u; } }
        s_ready = 1u;
    }
    g_tune_espeed       = 0u;   /* kein Pfadfortschritt -> feste, selbst gesetzte Positionen */
    g_spawn_pending     = 0u;
    g_spawn_row_changed = 0u;
    s_phase++;
    for (i = 0u; i < (u8)BENCH_META_N; i++) {
        TMetaEnemy *m = &g_metaenemies[i];
        u8 k = (u8)(((u8)(s_phase >> 1) + (u8)(i * 6u)) & 31u);   /* 5 Kaefer gleichmaessig auf dem Kreis */
        if (!m->active) {
            /* Slots 0..i-1 sind zu diesem Zeitpunkt alle belegt -> der erste
               freie Slot IST i. */
            if (!metaenemy_spawn(s_s, s_n, s_anim)) continue;
        }
        m->path_len = 30000u;   /* nie Pfadende (pp wird bei Tempo 0 nie dereferenziert) */
        m->frame    = 0u;
        m->x_fix    = 0;
        m->y_fix    = 0;
        m->path_acc = 0u;
        m->health   = 200u;
        m->off_timer = 0u;
        m->spawn_x  = (s16)((s16)BENCH_CIRC_CX + (s16)BENCH_CIRC_X[k]);
        m->spawn_y  = (s16)((s16)BENCH_CIRC_CY + (s16)BENCH_CIRC_Y[k]);
    }
}
#endif

/* Bewegung/Animation der mehrteiligen Metasprite-Gegner (Schritt 11) ??? Pfad-
   Logik 1:1 wie enemies_update(), nur die Grafikwahl unterscheidet sich
   (meta_idx statt spr_num/anim_frame). Eigene Funktion statt in enemies_update
   verschachtelt, da komplett andere OAM-Slots (SPR_METAENEMY_0) und Struktur. */
/* 29.07.2026 GEPRUEFT UND VERWORFEN: Zeiger-Iteration statt g_metaenemies[i]
   (44 Index-Zugriffe je Kaefer und Frame, ueber 300 Multiplikationen bei 7
   aktiven) plus die doppelte spawn+(fix>>4)-Rechnung zusammengefasst.
   HW-Messung Endboss-Szene: 92 -> 91, also Rauschen. Anders als im ZEICHENpfad
   (25.07., dort +1,5 fps) traegt die Umstellung hier nichts - der Block kostet
   seine 11 VBlanks nicht an der Adressrechnerei. Nicht nochmal versuchen. */
static void metaenemies_update(void) {
    u8 i;
    for (i = 0u; i < (u8)MAX_METAENEMIES; i++) {
        u16 len, off;
        if (!g_metaenemies[i].active) continue;

        g_busy_metas = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
        len = g_metaenemies[i].path_len;   /* 22.07.2026: aus dem Cache */
        if (g_metaenemies[i].frame >= len) {
            g_metaenemies[i].active = 0u;
            continue;
        }
        /* Tempo-Multiplikator, identisch zu enemies_update() ??? siehe dortigen
           Kommentar (Pfad-FORTSCHRITT skalieren, nicht die Deltas). */
        g_metaenemies[i].path_acc = (u8)(g_metaenemies[i].path_acc + g_tune_espeed);
        while (g_metaenemies[i].path_acc >= 16u) {
            g_metaenemies[i].path_acc = (u8)(g_metaenemies[i].path_acc - 16u);
            if (g_metaenemies[i].frame >= len) { g_metaenemies[i].active = 0u; break; }
            g_metaenemies[i].x_fix = (s16)(g_metaenemies[i].x_fix + g_metaenemies[i].pp[0]);
            g_metaenemies[i].y_fix = (s16)(g_metaenemies[i].y_fix + g_metaenemies[i].pp[1]);
            g_metaenemies[i].pp += 2;
            g_metaenemies[i].frame++;
        }
        if (!g_metaenemies[i].active) continue;
        g_metaenemies[i].x = (u8)(g_metaenemies[i].spawn_x + (g_metaenemies[i].x_fix >> 4));
        g_metaenemies[i].y = (u8)(g_metaenemies[i].spawn_y + (g_metaenemies[i].y_fix >> 4));

        if (g_metaenemies[i].metaanim != 0xFFFFu) {
            u8 h = (u8)g_metaenemies[i].metaanim;
            g_metaenemies[i].anim_tick++;
            if (g_metaenemies[i].anim_tick >= lvl_metaanim_speed[h]) {
                g_metaenemies[i].anim_tick = 0u;
                g_metaenemies[i].anim_frame++;
                if (g_metaenemies[i].anim_frame >= lvl_metaanim_len[h]) g_metaenemies[i].anim_frame = 0u;
            }
            g_metaenemies[i].meta_idx = lvl_metaanim_frames[h][g_metaenemies[i].anim_frame];
        }

        /* Vorzeichenrichtige Pruefung ??? siehe Bugfix-Kommentar in
           enemies_update() (gleicher u8-Wrap-Bug bei leicht negativem x/y,
           und seit 17.07.2026 spawn_x/y als echtes s16 statt (s8)-Trick). */
        {
            s16 real_x = (s16)(g_metaenemies[i].spawn_x + (g_metaenemies[i].x_fix >> 4));
            s16 real_y = (s16)(g_metaenemies[i].spawn_y + (g_metaenemies[i].y_fix >> 4));
            if (real_x <= -16 || real_x >= (s16)SCR_W || real_y <= -16 || real_y >= (s16)CLIP_Y) {
                g_metaenemies[i].off_timer++;
                if (g_metaenemies[i].off_timer >= (u8)ENEMY_OFFSCREEN_GRACE) {
                    g_metaenemies[i].active = 0u;
                }
            } else {
                g_metaenemies[i].off_timer = 0u;
            }
        }
    }

}

/* Bewegung der Wurm-Ketten (Schritt 12) ??? Pfad-Logik pro Segment grundsaetzlich
   wie bei TEnemy/TMetaEnemy (screen-absoluter Pfad, x_fix/y_fix inkrementell),
   mit zwei Besonderheiten:
   - `frame` ist SIGNED und startet bei nachfolgenden Segmenten negativ (siehe
     worm_spawn) ??? waehrend frame<0 bewegt sich das Segment noch nicht
     (x_fix/y_fix bleiben 0, das Segment "steckt noch im Wurmloch" an
     spawn_x/spawn_y), nur der Zaehler laeuft weiter hoch, bis er 0 erreicht.
   - Map-gebundene Pfade (`lvl_path_map_relative`, siehe TWorm-Kommentar):
     zusaetzliche Y-Korrektur um den seit dem Spawn-Trigger akkumulierten
     Scroll-Betrag ??? nur Y, das Spiel scrollt ausschliesslich vertikal.
   Kein Kettenglied-Shift hier (nur bei echtem Treffer, siehe worm_seg_hit) ???
   natuerliches Pfadende/Off-Screen-Grace despawnt ein Segment einfach an Ort
   und Stelle. */
static void worms_update(void) {
    u8 w, k;
    for (w = 0u; w < (u8)MAX_WORMS; w++) {
        u8  any_alive = 0u;
        u16 len;
        u8  map_rel;
        if (!g_worms[w].active) continue;
        len     = lvl_path_len[g_worms[w].path];
        map_rel = lvl_path_map_relative[g_worms[w].path];
        for (k = 0u; k < g_worms[w].num_segs; k++) {
            TWormSeg *sg = &g_worms[w].seg[k];
            s16 real_x, real_y;
            if (!sg->alive) continue;
            if (sg->frame >= 0) {
                if ((u16)sg->frame >= len) {
                    sg->alive = 0u;
                    continue;
                }
                {
                    /* Siehe PATH_PTR. Der Zeiger wird erst gesetzt, wenn das
                       Segment seinen negativen Startverzug abgearbeitet hat -
                       vorher liest es gar nicht aus dem Pfad. */
                    if (sg->frame == 0) sg->pp = PATH_PTR(g_worms[w].path, 0);
                    sg->x_fix = (s16)(sg->x_fix + sg->pp[0]);
                    sg->y_fix = (s16)(sg->y_fix + sg->pp[1]);
                    sg->pp += 2;
                }
            }
            sg->frame++;

            /* spawn_x/y sind hier ECHTES s16 (siehe TWorm-Kommentar) ??? anders
               als bei TEnemy/TMetaEnemy KEINE (s8)-Reinterpretation noetig
               (und fuer spawn_y=149 auch falsch, siehe dortigen Bugfix-
               Kommentar). Eine einzige Rechnung bedient sowohl die Zeichen-
               als auch die Grace-Position, keine getrennte plain/signed
               Variante mehr noetig. */
            real_x = (s16)(g_worms[w].spawn_x + (sg->x_fix >> 4));
            real_y = (s16)(g_worms[w].spawn_y + (sg->y_fix >> 4));
            if (map_rel)
                real_y = (s16)(real_y - (s16)((s16)g_scroll_y - (s16)g_worms[w].scroll_at_spawn));
            sg->x = (u8)real_x;
            sg->y = (u8)real_y;

            if (real_x <= -16 || real_x >= (s16)SCR_W || real_y <= -16 || real_y >= (s16)CLIP_Y) {
                sg->off_timer++;
                if (sg->off_timer >= (u8)WORM_OFFSCREEN_GRACE) sg->alive = 0u;
            } else {
                sg->off_timer = 0u;
            }
            if (sg->alive) { any_alive = 1u; g_busy_worms = 1u; }   /* Frueh-Ausstieg: siehe g_busy_* */
        }
        if (!any_alive) g_worms[w].active = 0u;
    }
}

/* Richtung fuers Zeichnen eines Wurm-Segments (Schritt 12): lvl_path_dir8[] an
   der aktuellen Pfad-Position, solange das Segment schon "geboren" ist
   (frame>=0) ??? waehrend der Verzoegerung (frame<0) noch keine echte Position
   auf dem Pfad, daher Default = Richtung bei Pfad-Frame 0. */
static u8 worm_seg_dir(const TWorm *wm, u8 k) {
    const TWormSeg *sg = &wm->seg[k];
    u16 off = lvl_path_off[wm->path];
    if (sg->frame > 0) off = (u16)(off + (u16)(sg->frame - 1));
    return lvl_path_dir8[off];
}

/* Belohnungs-Icon (Standbild-Kopf-S-Nummer) je nach Belohnungsart, wenn das
   Tool KEIN eigenes Icon gesetzt hat (lvl_group_reward_spr==0 ??? aktuell ueberall,
   siehe screenshot "kein Belohnungs-Sprite"). Ohne das blieben Gruppen-
   Belohnungen UND Container-Powerups unsichtbar (spr==0 wird in draw_sprites
   uebersprungen). Standard-Grafiken wie im Tool (pickupTypes): Punkte -> Cash-
   Blase klein (<100) / gross (>=100), Feuerrate -> S130, Power -> S123,
   Waffe -> S144. Diese Kopf-Tiles (a+b) liegen im Sprite-Pool (spr_raw_used,
   via gen_spr_compaction.js explizit ergaenzt). */
/* Eine einzelne S-Nummer fuer die Grafik einer Waffe - fuer Stellen, die kein
   mehrzelliges Sprite zeichnen koennen (Pickup-Icon). Metasprite/Metaanim werden
   auf die erste Zelle des ersten Bildes reduziert. 0 = keine Grafik hinterlegt,
   dann faellt der Aufrufer auf die alte Sammelgrafik zurueck. */
/* Bildzahl der Modul-Animation einer Waffe (1 = Standbild). Wird gebraucht, um
   die Animation ueber das Schussintervall zu strecken - siehe g_wp_adiv. */
static u8 wp_anim_len(u8 w) {
    u16 spr, n;
    if (w >= (u8)LVL_WEAPON_COUNT) return 1u;
    spr = lvl_weapon_spr[w];
    if (spr == 0u || !(spr & 0x4000u)) return 1u;   /* kein Sprite / nicht animiert */
    n = (u16)(spr & 0x01FFu);
    if (spr & 0x1000u) {                            /* Metaanim */
        u8 h;
        for (h = 0u; h < (u8)LVL_METAANIM_COUNT; h++)
            if (lvl_metaanim_head[h] == n) return lvl_metaanim_len[h];
        return 1u;
    }
    { u8 ai = sspr_anim_find(n);
      return (ai != 0xFFu) ? lvl_sspr_anim_len[ai] : 1u; }
}

/* Metaanim-Index der Modulgrafik einer Waffe, 0xFF wenn keiner. */
static u8 weapon_manim_of(u8 w) {
    u16 spr, n;
    u8 h;
    if (w >= (u8)LVL_WEAPON_COUNT) return 0xFFu;
    spr = lvl_weapon_spr[w];
    if (!(spr & 0x1000u) || !(spr & 0x4000u)) return 0xFFu;
    n = (u16)(spr & 0x01FFu);
    for (h = 0u; h < (u8)LVL_METAANIM_COUNT; h++)
        if (lvl_metaanim_head[h] == n) return h;
    return 0xFFu;
}

static u16 weapon_icon_s(u8 w) {
    u16 spr, n, mi;
    if (w >= (u8)LVL_WEAPON_COUNT) return 144u;
    spr = lvl_weapon_spr[w];
    if (spr == 0u) return 144u;
    n = (u16)(spr & 0x01FFu);
    if (!(spr & 0x1000u)) return n;                 /* einfacher Streifen */
    mi = n;
    if (spr & 0x4000u) {                            /* Metaanim -> erstes Bild */
        u8 h;
        for (h = 0u; h < (u8)LVL_METAANIM_COUNT; h++)
            if (lvl_metaanim_head[h] == n) break;
        if (h >= (u8)LVL_METAANIM_COUNT) return 144u;
        mi = lvl_metaanim_frames[h][0];
    }
    if (mi >= (u16)LVL_META_COUNT || lvl_meta_count[mi] == 0u) return 144u;
    return lvl_meta_num[lvl_meta_off[mi]];          /* erste Zelle */
}

static u16 reward_icon_for(u8 kind, u16 value) {
    switch (kind) {
    /* BUGFIX 21.07.2026 (Nutzerbericht "klein 50 und gross 100 waren vertauscht"):
       aus den Kacheldaten nachgemessen ??? S157 hat 8x8 px Ausdehnung = die GROSSE
       Muenze, S171 nur 7x7 px = die kleine. Vorher bekam die 100er-Belohnung die
       kleine Grafik und die 50er die grosse. Nachpruefbar ohne Augenmass ueber die
       Bounding-Box der Anim-Bilder (scratchpad/render.mjs).
       MERKE: diese Koepfe stehen NUR hier im C-Code, nicht in map.h ??? ein
       Tool-Export kann sie also still verschieben, ohne dass etwas auffaellt. */
    case 0:  return (value >= 100u) ? 157u : 171u;  /* Punkte: Cash gross (8x8) / klein (7x7) */
    case 1:  return 130u;                           /* Feuerrate */
    case 2:  return 123u;                           /* Power-Stufe */
    /* BUGFIX 27.07.2026 (Nutzer: "Container mit der Kanone zeigt die Heck-Animation,
       erst nach dem Einsammeln ist es die Kanone"): hier stand fest 144 - die
       Grafik von Waffe 0. Jeder Waffen-Container sah also gleich aus, egal was
       drin war. Jetzt die Grafik DER Waffe (value = Waffenindex). Ist ihr Sprite
       ein Metasprite oder Metaanim, wird die erste Zelle des ersten Bildes
       genommen: der Pickup-Zeichenpfad kann nur EIN Sprite, kein mehrzelliges. */
    case 3:  return weapon_icon_s((u8)value);       /* Waffe */
    case 4:  return 116u;                           /* Speedup (AS116, "S") */
    default: return 157u;
    }
}

/* Belohnungs-Flugbahn wie im Original (Seek zur Mitte -> Kreisel -> Fall).
   PICKUP_SEEK_X/Y/FALL_STEP sind weiter oben definiert (vor boss_cash_rain, das
   sie fuer den Cash-Regen ebenfalls braucht). */
static const s8 pickup_circ_dx[8] = {   0, 11, 16, 11,  0, -11, -16, -11 };
static const s8 pickup_circ_dy[8] = { -16,-11,  0, 11, 16,  11,   0, -11 };
#define PICKUP_CIRCLE_TICKS 48u
#define PICKUP_CIRCLE_ROT    3u
static u8 g_pickup_anim_tick;
/* Anim-Index einer Kopf-S-Nummer, 0xFF wenn KEIN Anim-Kopf (dann Standbild). */
static u8 sspr_anim_find(u16 n) {
    u8 i;
    for (i = 0u; i < (u8)LVL_SSPR_ANIM_COUNT; i++)
        if (lvl_sspr_anim_head[i] == n) return i;
    return 0xFFu;
}

/* Bild fpos einer Animation, aber LEERE Bilder ueberspringen (21.07.2026).
   Hintergrund: der Tool-Export schreibt 0xFFFF ("keine Kachel"), wenn ein Sprite
   keine Palette mehr bekommen hat ??? der Sprite-Pool braucht dann mehr als die 16
   Hardware-Paletten. Bisher zeichnete das Spiel so ein Bild trotzdem, was als
   Wegblinken auffiel (Nutzerbericht: Schubduesen-Flamme, S156 = Bild 2 der Anim
   S155). Statt zu blinken halten wir das letzte gueltige Bild.
   Das ist eine NOTBREMSE, keine Loesung: fehlt die Grafik, fehlt sie ??? aber die
   Animation bleibt ruhig und der Fehler faellt nicht als Flackern auf. */
static u16 sspr_anim_frame_s(u8 ai, u8 fpos) {
    u8  len = lvl_sspr_anim_len[ai];
    u8  k, p;
    u16 s;
    if (len == 0u) return lvl_sspr_anim_head[ai];
    for (k = 0u; k < len; k++) {
        p = (u8)(fpos + len - k);
        while (p >= len) p = (u8)(p - len);        /* Modulo ohne Division */
        s = lvl_sspr_anim_frames[ai][p];
        if (s != 0u && lvl_sspr_a_idx[s - 1u] != 0xFFFFu) return s;
    }
    return lvl_sspr_anim_head[ai];
}

/* --- Gruppen-Belohnungen (Schritt 14) ---
   g_group_count wird in game_start() aus lvl_spawn_group/lvl_spawn_count
   aufgebaut. Erreicht eine Gruppe 0, entsteht an der Todesposition des
   letzten Gegners ein Pickup mit der zur Gruppen-Id passenden
   lvl_group_reward_kind/value/spr ??? siehe apply_pickup() fuer die Wirkung. */
/* Anflug-Vektor berechnen (Schritt 15): geradlinig von der Todesposition
   zum festen Anker LVL_PICKUP_ANCHOR_X/Y, Tempo LVL_PICKUP_HOMING_SPEED.
   KEINE (s32)-Casts (cc900-Fallstrick, Link-Fehler -209) ??? bei Bildschirm-
   Distanzen (max. ~160px) bleibt jede Zwischengroesse locker in s16.
   Bugfix 10.07.2026: NIE eine NEGATIVE Zahl linksshiften (`total_dx << 4`
   mit total_dx<0 ist in C89 undefiniertes Verhalten ??? auf cc900 sichtbar
   als diagonal aus dem Bild fliegendes Pickup, wenn der Anker links/oberhalb
   der Todesposition lag). Immer den Betrag (adx/ady) shiften, Vorzeichen
   erst danach wieder anwenden. */
static void pickup_spawn_for_group(u8 grp, u8 x, u8 y) {
    u8 k, p;
    for (k = 0u; k < (u8)LVL_GROUP_REWARD_COUNT; k++) {
        if (lvl_group_reward_id[k] != grp) continue;
        for (p = 0u; p < (u8)MAX_PICKUPS; p++) {
            s16 total_dx, total_dy, adx, ady, max_abs, frames, hdx, hdy;
            if (g_pickups[p].active) continue;
            total_dx = (s16)((s16)PICKUP_SEEK_X - (s16)x);
            total_dy = (s16)((s16)PICKUP_SEEK_Y - (s16)y);
            adx = (total_dx < 0) ? (s16)(-total_dx) : total_dx;
            ady = (total_dy < 0) ? (s16)(-total_dy) : total_dy;
            max_abs = (adx > ady) ? adx : ady;
            frames = (s16)((s16)(max_abs << 4) / (s16)LVL_PICKUP_HOMING_SPEED);
            if (frames < 1) frames = 1;   /* Kantenfall: Todesposition == Anker */
            g_pickups[p].homing_frames = (u16)frames;
            g_pickups[p].homing_tick   = 0u;
            hdx = (s16)((s16)(adx << 4) / frames);   /* IMMER Betrag shiften */
            hdy = (s16)((s16)(ady << 4) / frames);
            g_pickups[p].homing_dx = (total_dx < 0) ? (s16)(-hdx) : hdx;
            g_pickups[p].homing_dy = (total_dy < 0) ? (s16)(-hdy) : hdy;
            g_pickups[p].x_fix = (s16)((s16)x << 4);
            g_pickups[p].y_fix = (s16)((s16)y << 4);
            g_pickups[p].path_frame = 0u;
            g_pickups[p].dir   = 0u;
            g_pickups[p].state = 0u;
            g_pickups[p].active = 1u;
            g_busy_pickups = 1u;   /* Frueh-Ausstieg: Flag an der Erzeugungsstelle */
            g_pickups[p].x      = x;
            g_pickups[p].y      = y;
            g_pickups[p].kind   = lvl_group_reward_kind[k];
            g_pickups[p].value  = lvl_group_reward_value[k];
            /* Nur die 1-basierte S-Nummer wird gebraucht (Standbild-Icon,
               siehe draw_sprites) ??? Strip/Anim-Bits aus Schritt 9 ignoriert.
               Setzt das Tool kein Icon (spr==0, aktueller Stand), leiten wir es
               aus kind/value ab (reward_icon_for), sonst blieben Belohnungen +
               Container-Powerups unsichtbar. */
            g_pickups[p].spr    = (u16)(lvl_group_reward_spr[k] & 0x01FFu);
            if (g_pickups[p].spr == 0u)
                g_pickups[p].spr = reward_icon_for(g_pickups[p].kind, g_pickups[p].value);
            /* Waffen-Container: Metaanim merken, damit das Icon laeuft (s. TPickup). */
            g_pickups[p].manim = (g_pickups[p].kind == 3u)
                                   ? weapon_manim_of((u8)g_pickups[p].value) : 0xFFu;
            g_pickups[p].oam0   = g_pickups[p].oam1 = OAM_NONE;
            break;
        }
        return;
    }
}

/* Gemeinsamer Kill-Pfad fuer Schuss- UND Schiffskontakt-Abschuss: Gruppen-
   Zaehler wird IMMER dekrementiert (ein toter Gegner zaehlt unabhaengig von
   der Todesart), Punkte (lvl_spawn_points, siehe CLAUDE.md-Altlast "fix
   +200") nur bei award_points!=0 ??? Rammen gab schon vorher keine Punkte. */
static void spawn_killed(u8 spawn_idx, u8 x, u8 y, u8 award_points) {
    u8 grp = lvl_spawn_group[spawn_idx];
    if (award_points)
        /* BUGFIX 22.07.2026: hier stand "* 100u". lvl_spawn_points enthaelt aber
           bereits die ORIGINALWERTE (50/100/120/150/200/300/400, map.h) - mit dem
           Faktor lief der u16-Punktestand schon nach zwei 400er-Abschuessen ueber
           (2 x 40000 > 65535) und sprang auf einen kleinen Wert zurueck. Dass es
           ein Versehen war und keine Absicht, zeigen die Wandwuermer: die addieren
           ihre 250/50 roh (siehe wallworm_seg_hit). Aufgefallen ist es nie, weil
           die HUD-Ziffern seit Tagen Diagnosewerte statt des Punktestands zeigen. */
        g_score += (u16)lvl_spawn_points[spawn_idx];
    if (grp != 0u && g_group_count[grp] > 0u) {
        g_group_count[grp]--;
        if (g_group_count[grp] == 0u)
            pickup_spawn_for_group(grp, x, y);
    }
}
static void enemy_killed(u8 j, u8 award_points) {
    spawn_killed(g_enemies[j].spawn_idx, g_enemies[j].x, g_enemies[j].y, award_points);
}
static void metaenemy_killed(u8 j, u8 award_points) {
    spawn_killed(g_metaenemies[j].spawn_idx, g_metaenemies[j].x, g_metaenemies[j].y, award_points);
}

/* Health/Schaden-System (19.07.2026, Nutzerwunsch "wie im Original"): Schuss
   reduziert die Trefferpunkte (lvl_spawn_health aus dem Tool) um dmg. Erst wenn
   sie aufgebraucht sind, stirbt der Gegner (Kill + Belohnung). Der Schuss wird
   IMMER verbraucht (Aufrufer setzt bullet.active=0), auch wenn der Gegner
   ueberlebt ??? wie im DOS-Original (BULLET_BASE_DAMAGE 1 + Power). Rueckgabe nur
   informativ. Main-Gun-Schaden = lvl_power_stage_damage[power_stage] (1/2/3),
   Heckwaffe = lvl_weapon_damage[0] + power_stage (DOS: 1 + rear power, Cap 2). */
/* OAM-Slots eines Gegners zurueckgeben und die Felder auf OAM_NONE setzen.
   Doppelaufrufe sind unschaedlich (OAM_NONE-Pruefung), und oam_pool_free() faengt
   Slots ausserhalb des Pools ohnehin ab. */
static void enemy_free_oam(u8 j) {
    if (g_enemies[j].oam0 != OAM_NONE) {
        UnsetSprite(g_enemies[j].oam0);
        oam_pool_free(g_enemies[j].oam0);
        g_enemies[j].oam0 = OAM_NONE;
    }
    if (g_enemies[j].oam1 != OAM_NONE) {
        UnsetSprite(g_enemies[j].oam1);
        oam_pool_free(g_enemies[j].oam1);
        g_enemies[j].oam1 = OAM_NONE;
    }
    g_enemies[j].last_snum = 0u;
    g_enemies[j].chain_b   = 0u;
}

/* Dasselbe fuer Metasprite-Gegner (Nutzerbericht 27.07.2026: "ein Metasprite-
   Kaefer wurde soeben zum Geist"). Sie haben je Zelle einen a- und einen
   b-Slot; bei aktivem Chaining liegt zusaetzlich ein zusammenhaengender Block
   vor, der ueber chain_base/chain_cells verwaltet wird. */
static void metaenemy_free_oam(u8 j) {
    u8 c;
    for (c = 0u; c < (u8)META_CELLS; c++) {
        if (g_metaenemies[j].oam[c] != OAM_NONE) {
            UnsetSprite(g_metaenemies[j].oam[c]);
            oam_pool_free(g_metaenemies[j].oam[c]);
            g_metaenemies[j].oam[c] = OAM_NONE;
        }
        if (g_metaenemies[j].oam_b[c] != OAM_NONE) {
            UnsetSprite(g_metaenemies[j].oam_b[c]);
            oam_pool_free(g_metaenemies[j].oam_b[c]);
            g_metaenemies[j].oam_b[c] = OAM_NONE;
        }
        g_metaenemies[j].last_snum[c] = 0u;
    }
    g_metaenemies[j].chain_base  = OAM_NONE;
    g_metaenemies[j].chain_cells = 0u;
    g_metaenemies[j].c_cached_idx = 0xFFFFu;
}

static void enemy_take_damage(u8 j, u8 dmg) {
#if BENCH_NOKILL
    g_enemies[j].flash = (u8)HIT_FLASH_FRAMES; (void)dmg; return;   /* Benchmark: unsterblich */
#endif
    if (g_enemies[j].health > dmg) {
        g_enemies[j].health = (u8)(g_enemies[j].health - dmg);
        g_enemies[j].flash  = (u8)HIT_FLASH_FRAMES;   /* 23.07.2026: Treffer-Flash (nur bei ueberlebtem Treffer - ein toter Gegner explodiert ohnehin) */
        return;
    }
    g_enemies[j].active = 0u;
    /* BUGFIX 27.07.2026 (Nutzerbericht "Geister-Gegner nach dem Abschuss, immer
       S201-S212"): die OAM-Slots SOFORT beim Tod zurueckgeben und die Felder auf
       OAM_NONE setzen. Bisher lag das allein beim Zeichendurchlauf - wird der
       Gegnerplatz vorher neu belegt (S201 haengt an 21 der Spawn-Wellen bei nur
       MAX_ENEMIES Plaetzen, trifft ihn also weit oefter als jeden anderen),
       ueberschrieb der Spawn die Slot-Nummern und das Sprite blieb ohne Besitzer
       stehen - auch fuer den 128-Frame-Hausputz unsichtbar. */
    enemy_free_oam(j);
    enemy_killed(j, 1u);
}
static void metaenemy_take_damage(u8 j, u8 dmg) {
#if BENCH_NOKILL
    g_metaenemies[j].flash = (u8)HIT_FLASH_FRAMES; (void)dmg; return;   /* Benchmark: unsterblich */
#endif
    if (g_metaenemies[j].health > dmg) {
        g_metaenemies[j].health = (u8)(g_metaenemies[j].health - dmg);
        g_metaenemies[j].flash  = (u8)HIT_FLASH_FRAMES;   /* siehe enemy_take_damage */
        return;
    }
    g_metaenemies[j].active = 0u;
    metaenemy_free_oam(j);   /* siehe enemy_free_oam - Slots sofort beim Tod zurueck */
    metaenemy_killed(j, 1u);
}
/* Schaden der aktuell aktiven Schuss-Quellen ??? je 1 + power_stage (DOS-faithful,
   siehe enemy_take_damage). Als Funktionen, damit alle 4 Trefferstellen konsistent
   denselben Wert nutzen. */
static u8 main_gun_damage(void)  { return lvl_power_stage_damage[g_player.power_stage]; }
static u8 rear_gun_damage(void)  { return (u8)(lvl_weapon_damage[0] + g_player.power_stage); }
/* 22.07.2026: Schaden je Modul-Waffe statt pauschal dem der Heckwaffe. */
static u8 wp_gun_damage(u8 w)    { return (u8)(lvl_weapon_damage[w] + g_player.power_stage); }

/* Wurm-Segment treffen (Schritt 12 "Schrumpfen bei Treffer", Centipede-
   Prinzip): Segment k stirbt, alle NACHFOLGENDEN Segmente ruecken eine
   Position in der Kette auf (kompletter Struct-Copy, nicht nur frame ??? die
   Bildschirmposition steckt in x_fix/y_fix, die inkrementell fortgeschrieben
   werden, ein reiner frame-Kopiervorgang wuerde einen sichtbaren Sprung
   verursachen), letztes Segment despawnt. Kette wird kuerzer, Rest bewegt
   sich lueckenlos weiter zusammen ??? exakt wie in der Anweisung Schritt 12
   gefordert. */
static void worm_seg_hit(u8 w, u8 k, u8 award_points) {
    u8 i;
#if BENCH_NOKILL
    (void)w; (void)k; (void)award_points; (void)i; return;   /* Benchmark: Wurm unsterblich */
#endif
    spawn_killed(g_worms[w].spawn_idx, g_worms[w].seg[k].x, g_worms[w].seg[k].y, award_points);
    /* OAM-Fix 18.07.2026 (Nutzer-HW: "manche Gegner bleiben nach dem Tod statisch
       stehen", Segment B / Wuermer): Der Struct-Shift unten kopiert die OAM-Slots
       MIT. Ohne Vorbehandlung ginge der OAM-Slot des getroffenen Segments (Index k,
       dessen Sprite verschwindet) verloren (Leak) UND seg[num-1].oam bliebe ein
       Duplikat von seg[num-2].oam -> beim naechsten draw_sprites Doppel-Free +
       Slot-Konflikt -> OAM-Pool laeuft ueber, Sprites bleiben haengen. Deshalb:
       (a) getroffenes Segment-OAM JETZT freigeben (sein Sprite geht weg),
       (b) nach dem Shift die Duplikat-Referenz am Ketten-Ende kappen. */
    if (g_worms[w].seg[k].oam != OAM_NONE) {
        UnsetSprite(g_worms[w].seg[k].oam);
        oam_pool_free(g_worms[w].seg[k].oam);
        g_worms[w].seg[k].oam = OAM_NONE;
    }
    for (i = k; i < (u8)(g_worms[w].num_segs - 1u); i++)
        g_worms[w].seg[i] = g_worms[w].seg[i + 1u];
    g_worms[w].seg[g_worms[w].num_segs - 1u].oam = OAM_NONE;   /* Duplikat kappen (Slot lebt bei seg[num-2] weiter) */
    g_worms[w].seg[g_worms[w].num_segs - 1u].alive = 0u;
    {
        u8 any = 0u;
        for (i = 0u; i < g_worms[w].num_segs; i++)
            if (g_worms[w].seg[i].alive) any = 1u;
        if (!any) g_worms[w].active = 0u;
    }
}

/* ============ WANDWUERMER (Schritt 19) ??? hartcodiertes L1-Spezialsystem ============
   Reintegriert & fertiggestellt 2026-07-19: Grundgeruest aus Archiv/
   xenon_split_wip_20260716_2221.c, Original-Mechanik aus DOS (Remaster
   src/game/enemies.ts "WALL WORMS", docs snakes.md/tube-snakes.md,
   tools/worms-evidence.ts). Kommt in KEINER Tool-Exportdatei vor.
   - Der (unsichtbare, unverwundbare) KOPF folgt einem der 16 Original-Wurm-Pfade
     (worm_paths.h, aus DOS S1/PATHS.BIN, WORM_SPEED=4). Nur der Kopf laeuft den
     Pfad; er stirbt ausschliesslich am Pfadende.
   - 11 sichtbare Teile (Neck + 10 Segmente): jedes kopiert die Vor-Tick-Position
     des Vordermanns -> 1 Tick Versatz = WORM_SPEED px Abstand (dichte Raupe).
   - Loecher (WALLWORM_EXITS) sind deckungsgleich zur eigenen Karte nummeriert
     (row/col in Map-Kacheln; aufwaendig abgeglichen ??? NICHT neu nummerieren!).
   - Treffer (Centipede): getroffenes Teil loest sich als wegfliegender Ball, der
     Rest der Kette rueckt auf und marschiert weiter. Kopf ist unverwundbar.
   - Ebenen-Trick ("kommt aus dem Tile", kein Zusatz-Tile/Masking): Teil startet
     SPR_FURTHEST (hinter Terrain-SCR2/SCR1), wechselt auf SPR_FRONT sobald sein
     Loch die Bildschirmzeile erreicht. Layer: SPR_FURTHEST->SCR2->MIDDLE->SCR1->FRONT.
   - Kopf/Neck = Rotationssatz 0 (10x10), Segmente = Rotationssatz 1 (8x8). */
#define WALLWORM_SLOTS      2u   /* 25.07.2026 von 3 auf 2 (Nutzerwunsch). Nebeneffekt: weniger Wurm-Sprites = weniger Draw-Last im Wurmband (der einzige Ort unter 30fps). */    /* OAM-Budget: 2*6=12 (Pool 48, Schiff separat). Original: 5 Slots / 11 Segmente ??? an OAM angepasst. TUNABLE. */
#define WALLWORM_SEGMENTS   6u    /* 1 Neck + 5 Segmente (Nutzerwunsch 2026-07-19; Original 11) */
#define WALLWORM_NECK_SCORE 250u
#define WALLWORM_SEG_SCORE  50u
#define WALLWORM_HEAD_ROT   0u
#define WALLWORM_BODY_ROT   1u
/* 25.07.2026 (Nutzerwunsch fuer Wurmband-fps): die KOERPER-Segmente drehen nicht
   mehr mit der Kriechrichtung mit, sondern zeigen ein FESTES Sprite (WORM_BODY_DIR).
   Damit greift der Draw-Cache IMMER (nur SetSpritePosition, nie SetSprite/ROM-
   Lookup/SpriteControl) -> die Vollredraw-Spitze im Wurmband faellt weg. Der KOPF
   dreht weiter (1 Segment, sieht richtig aus). 0 = alte volle Rotation. */
#define WORM_NO_ROTATE   0    /* 25.07.2026 zurueckgestellt: brachte kaum fps (68->66) und sah steif aus */
#define WORM_BODY_DIR    0u    /* feste Anzeigerichtung der Koerpersegmente (nur bei WORM_NO_ROTATE) */
#define WALLWORM_BALLS      6u
/* Zeilenband der 16 Wandloecher (aus WALLWORM_EXITS: 92..123). Nur innerhalb
   davon - plus Bildschirmhoehe Reserve - kann ueberhaupt eines im Ring liegen;
   siehe die Abkuerzung in wallworms_update(). Bei einer neuen Map mitziehen! */
#define WALLWORM_ROW_MIN 92u
#define WALLWORM_ROW_MAX 123u    /* gleichzeitige wegfliegende Baelle */
#define WALLWORM_BALL_LIFE  40u   /* Ticks bis der Ball verschwindet (zusaetzlich Rand-Cull) */
/* SPAWN-Modell: engine-treuer Per-Loch-Trigger (wie die Gegnerwellen: "sobald
   die Map-Zeile eingerollt ist", g_lvl_row) statt Original-Zufallsband ??? so
   spawnt jeder Wurm an SEINEM deckungsgleich nummerierten Loch.
   VERTIKALE VERANKERUNG: der Wurm ist mit DERSELBEN Formel wie das Terrain ans
   Loch gepinnt ??? eine Ringzeile rr sitzt bei (rr*8 - g_scr1_y) (u8-Wrap, siehe
   z.B. mapobj-Zeichnen). Beim Spawn wird die Ringzeile des Lochs erfasst
   (TWallWorm.hole_ring); die Screen-Y folgt dann exakt dem scrollenden Terrain,
   KEIN Vorzeichen-/Offset-Raten mehr. Nur ein Fein-Bias bleibt tunable. */
#define WORM_HOLE_Y_BIAS    0     /* TUNABLE: konstante Y-Korrektur in px, falls Wurm ein paar px ueber/unter dem Loch sitzt */
/* Roehren-Emergenz/-Rueckkehr (19.07.2026, Nutzer-Screenshot + Vorschlag "Offset,
   dann greift der Pfad"): die DOS-Pfade sind mit Off-Screen-POSITION-Headern
   designt (Anflug), die die Runtime skippt ??? dadurch starten manche Pfade
   "mitten in der Kurve" statt aus der Roehre. Deshalb: PROLOG = Kopf kriecht
   erst WORM_PROLOG_STEPS px HORIZONTAL aus der Roehre (links=Ost/rechts=West),
   DANN laeuft der Original-Pfad 1:1. EPILOG = am Pfad-Ende kriecht der Kopf
   horizontal zur naechstgelegenen Wand und stirbt, sobald er IM Terrain steckt
   (terrain_solid) = verschwindet in einer Roehre; die Kette zieht hinterher. */
#define WORM_PROLOG_STEPS   16u
#define WORM_RET_STEP       1     /* px pro Schritt im Rueckkehr-Kriechen (x WORM_STEPS_PER_TICK pro Tick) */
#define WORM_PATH_MAX_STEPS 140u  /* Pfad-Anteil begrenzen (~5s wie DOS-Gesamtdauer): die vollen 345-580
                                     Schritte dauern 12-19s ??? das Loch ist laengst runtergescrollt, die
                                     Rueckkehr fand nie SICHTBAR statt (Nutzer: "keiner kommt zurueck") */
#define WORM_STEPS_PER_TICK 2u    /* Kopf advanced so viele Pfad-Schritte/Tick ??? = Segment-Abstand (px) UND Tempo. Nach der Pfad-Halbierung 19.07.: 1=zu langsam+zu nah, 2 ~ DOS WORM_SPEED 4 halbiert */
#define WORM_EMERGE_TICKS   3u    /* Logik-Ticks SPR_FURTHEST (hinter der Wand), dann SPR_FRONT (Emerge-Optik) */
/* GESCHWINDIGKEIT: Die Wurm-Logik (Kopf-Pfadschritt, Kette, Emergenz) laeuft nur
   alle WORM_TICK_DIV Frames ??? sonst kriecht der Wurm ~12x zu schnell (Pfade sind
   bei Original-Speed 4 @18,2Hz gebacken, unsere Schleife laeuft mit 30fps). Bei
   DIV rueckt den Kopf 1 Pfad-Schritt (=2px bei Speed 2) alle DIV Frames vor.
   Der KETTENABSTAND haengt NICHT hieran, sondern an der Pfad-Schrittweite (Speed
   im Re-Bake, worm_paths.mjs): Speed 2 = 2px Abstand. Fuer noch enger Speed 1
   re-baken. TUNABLE gegen "zu schnell/zu langsam". Baelle fliegen jeden Frame. */
#define WORM_TICK_DIV       2u
#define WORM_SPAWN_INTERVAL 10u   /* Spawn-Stagger (Nutzer 19.07. "alle gleichzeitig raus"): min. so viele Ticks zwischen zwei Wurm-Spawns */

typedef struct { u8 row, col, path; } TWallWormExit;   /* path = 0-basiert in worm_path_* */
static const TWallWormExit WALLWORM_EXITS[16] = {
    {122, 2, 0}, {116, 2, 1}, {107, 2, 2}, {123, 18, 3}, {120, 17, 4}, {117, 18, 5},
    {112, 16, 6}, {98, 7, 7}, {95, 6, 8}, {92, 7, 9}, {99, 13, 10}, {100, 18, 11},
    {95, 12, 12}, {97, 17, 13}, {93, 13, 14}, {94, 18, 15},
};
/* 8 feste Ball-Flugrichtungen (screen-fest, ~3px/Tick), Index = QRandom()&7 ???
   Original: (sin/4, cos/4); auf dem kleinen 160px-Screen zu schnell, deshalb
   die gedaempfte 8-Wege-Naeherung (wie die Titelscreen-Sterne). */
static const s8 wallworm_ball_vx[8] = {  3,  2,  0, -2, -3, -2,  0,  2 };
static const s8 wallworm_ball_vy[8] = {  0,  2,  3,  2,  0, -2, -3, -2 };

typedef struct {
    s16 wx, wy, prev_wx, prev_wy;  /* LOCH-RELATIVE Position (px, scroll-unabhaengig); prev = vor dem letzten Logik-Tick.
                                       Screen = (col*8 + wx, hole_screen_y + wy) ??? jeden Frame neu (glattes Mitscrollen). */
    u8  dir;                    /* 0-7 Rotationssatz-Richtung */
    u8  emerge_ct;              /* zaehlt ab Geburt hoch; < WORM_EMERGE_TICKS = SPR_FURTHEST (hinter Wand) */
    u8  alive, oam; u16 last_snum; u8 last_flip;
    u8  last_dir, last_prio;    /* 24.07.2026 Draw-Cache: bei unveraendertem dir den Rotset-Lookup (2x 2D-ROM) UND SpriteControl sparen -> nur SetSpritePosition. 0xFF = ungueltig (Vollredraw erzwingen). */
} TWallWormPart;
typedef struct {
    u8  active, x, y, dir, big, anim, life;
    u8  oam; u16 last_snum; u8 last_flip;
} TWallWormBall;
typedef struct {
    u8  active, emerged, head_alive, exit_idx, part_count;
    u16 head_frame;             /* Pfad-Tick in worm_path_* */
    s16 head_x_fix, head_y_fix; /* akkumulierte Pfad-Deltas (4.4) ab Loch */
    u8  head_angle;
    u8  hole_ring;              /* Ringzeile des Lochs (beim Spawn erfasst) -> Screen-Y wie Terrain */
    u8  returning;              /* 1 = Epilog: kriecht ins naechstgelegene Loch (Roehren-Rueckkehr) */
    u8  ret_west;               /* Rueckkehr-Richtung: 1 = nach Westen (links), 0 = Osten */
    s16 ret_tx, ret_ty;         /* 25.07.2026 Epilog-Ziel: NAECHSTGELEGENES Loch, relativ zum EIGENEN Loch (Original: Pfad endet an einem - meist anderen - Loch, dort laeuft die Kette ein) */
    s16 ret_ax, ret_ay;         /* Anflugpunkt VOR der offenen Roehrenseite des Ziel-Lochs (Kurvenflug steuert erst hierhin) */
    u8  ret_entry_dir;          /* Einfahrt-Richtung (dir8): IMMER entgegengesetzt zur Auskriech-Richtung des Ziel-Lochs */
    u8  ret_phase;              /* 0 = Kurvenflug zum Anflugpunkt, 1 = gerade kopfvoran ins Loch */
    u8  ret_exit;               /* Exit-Index des Ziel-Lochs (fuer das Loch-Anim-Skript beim Eintauchen) */
    u8  out_west;               /* Emergenz-Richtung (aus terrain_solid beim Spawn): 1 = West */
    u8  flip_x;                 /* 1 = Pfad horizontal gespiegelt (Anfangsrichtung zeigte ZUR Roehre -> Wurm fuhr rueckwaerts rein; 19.07.) */
    TWallWormPart part[WALLWORM_SEGMENTS];
} TWallWorm;
static TWallWorm     g_wallworms[WALLWORM_SLOTS];
static TWallWormBall g_wallworm_balls[WALLWORM_BALLS];

/* Aktuelle Bildschirm-Y des Wurm-Lochs ??? EXAKT wie das Terrain seine Ringzeile
   zeichnet: (ring*8 - g_scr1_y) mit u8-Wrap. sy>=208 => Zeile ueber dem Bildschirm
   (gewrappt) -> negativ zurueckgeben, damit ueber-dem-Rand-Segmente korrekt
   ausgeblendet werden. Damit ist der Wurm fest ans scrollende Loch gepinnt. */
static s16 wallworm_hole_screen_y_ww(const TWallWorm *ww) {
    u8 sy = (u8)((u8)((u8)(ww->hole_ring << 3) - g_scr1_y) + (u8)WORM_HOLE_Y_BIAS);
    return (sy < 208u) ? (s16)sy : (s16)((s16)sy - 256);
}

/* (19.07.2026) KEIN Loch-Naeherungs-Trigger: DOS-Kopf stirbt NUR am Pfad-Ende
   (RE enemies.ts "The head dies ONLY at path end"). Fruehere entered_hole-
   Varianten (Screen- und map-relativ) entfernt ??? sie liessen Wuermer beim
   ersten Vorbeistreifen sofort ins Nachbarloch kippen, was es im Original
   nicht gibt. Merkregel Map-Geometrie (falls je wieder gebraucht): GROESSERE
   row = SPAETER eingerollt = weiter OBEN am Screen -> rel. Y = (rowA-rowB)*8. */

static void wallworm_ball_spawn(u8 x, u8 y, u8 big) {
    u8 b;
    for (b = 0u; b < (u8)WALLWORM_BALLS; b++) {
        if (g_wallworm_balls[b].active) continue;
        g_wallworm_balls[b].active   = 1u;
        g_busy_wworms = 1u;   /* Frueh-Ausstieg: Baelle werden in wallworms_draw mitgezeichnet */
        g_wallworm_balls[b].x        = x;
        g_wallworm_balls[b].y        = y;
        g_wallworm_balls[b].dir      = (u8)(QRandom() & 7u);
        g_wallworm_balls[b].big      = big;
        g_wallworm_balls[b].anim     = 0u;
        g_wallworm_balls[b].life     = (u8)WALLWORM_BALL_LIFE;
        g_wallworm_balls[b].oam      = OAM_NONE;
        g_wallworm_balls[b].last_snum = 0u;
        g_wallworm_balls[b].last_flip = 0xFFu;
        return;
    }
}

/* 25.07.2026: OFFENE Seite des Lochs e (datengetrieben wie beim Spawn): 1 =
   Roehre oeffnet nach WESTEN (Wurm kriecht nach West raus, Einfahrt nach Ost).
   Fuer die Rueckkehr ins Ziel-Loch (Einfahrt IMMER kopfvoran entgegengesetzt
   zur Auskriech-Richtung, Nutzerwunsch "nicht von hinten eintauchen"). */
static u8 wallworm_exit_open_west(u8 e) {
    u8 ring = find_ring_row((u16)WALLWORM_EXITS[e].row);
    u8 hx = (u8)((u8)WALLWORM_EXITS[e].col * 8u + 4u);
    u8 ow = (u8)((WALLWORM_EXITS[e].col >= 10u) ? 1u : 0u);   /* Fallback */
    if (ring != (u8)MAPOBJ_NONE) {
        u8 sy = (u8)((u8)(ring << 3) - g_scr1_y);
        if (sy < (u8)CLIP_Y && hx >= 12u && hx < (u8)(SCR_W - 12u)) {
            u8 solid_l = terrain_solid((u8)(hx - 12u), sy);
            u8 solid_r = terrain_solid((u8)(hx + 12u), sy);
            if (solid_l && !solid_r)      ow = 0u;   /* Wand links -> offen nach Ost */
            else if (solid_r && !solid_l) ow = 1u;   /* Wand rechts -> offen nach West */
        }
    }
    return ow;
}
/* 8-Richtungs-Vektoren (O=0,SO=1,S=2,SW=3,W=4,NW=5,N=6,NO=7; y nach unten) -
   fuer den Kurvenflug der Rueckkehr (gleiches 8-Richtungs-Raster wie die
   Original-Pfade). */
static const s8 worm_dir8_dx[8] = { 1, 1, 0,-1,-1,-1, 0, 1 };
static const s8 worm_dir8_dy[8] = { 0, 1, 1, 1, 0,-1,-1,-1 };

/* Einen Wurm an Loch exit_idx starten (Per-Loch-Trigger, siehe wallworms_update).
   Rueckgabe 1 = gestartet, 0 = kein freier Slot. */
static u8 wallworm_spawn(u8 exit_idx) {
    u8 w;
    for (w = 0u; w < (u8)WALLWORM_SLOTS; w++) {
        if (g_wallworms[w].active) continue;
        g_wallworms[w].active         = 1u;
        g_wallworms[w].exit_idx       = exit_idx;
        g_wallworms[w].head_alive     = 1u;
        g_wallworms[w].emerged        = 0u;
        g_wallworms[w].part_count     = 0u;
        g_wallworms[w].head_frame     = 0u;
        g_wallworms[w].head_x_fix     = 0;
        g_wallworms[w].head_y_fix     = 0;
        g_wallworms[w].returning      = 0u;
        g_wallworms[w].ret_west       = 0u;
        /* Emergenz-Richtung DATENGETRIEBEN aus der Wandgrafik (19.07.2026, Nutzer
           "alle falsche Richtung"): die Roehre sitzt in der Wand ??? auf welcher Seite
           des Lochs ist FREIRAUM? Dahin kriecht der Wurm raus. terrain_solid links/
           rechts vom Loch pruefen (12px). col<10 nur noch Fallback (Innen-Felsen
           col 6-13 sagen per Spalte nichts ueber die Roehrenrichtung). */
        {
            u8 ow = wallworm_exit_open_west(exit_idx);   /* 25.07.2026: Logik in Helper (auch fuer die Rueckkehr) */
            g_wallworms[w].out_west   = ow;
            g_wallworms[w].head_angle = (u8)(ow ? 4u : 0u);
            /* Pfad-Spiegel-Check (Nutzer 19.07. "kommt raus und geht rueckwaerts"):
               Anfangs-X-Tendenz des Pfads (Summe der ersten 24 dx) messen. Zeigt
               sie ZUR Roehre (gegen die Emergenz-Richtung), Pfad horizontal
               spiegeln ??? dx negieren + dir8 spiegeln beim Abspielen (flip_x). */
            {
                s16 sum = 0; u8 k;
                u16 po = worm_path_off[WALLWORM_EXITS[exit_idx].path];
                for (k = 0u; k < 24u; k++) sum = (s16)(sum + (s16)worm_path_data[(u32)(po + k) * 2u]);
                g_wallworms[w].flip_x = 0u;
                if (!ow && sum < -32) g_wallworms[w].flip_x = 1u;   /* raus Ost, Pfad zieht West */
                if (ow  && sum >  32) g_wallworms[w].flip_x = 1u;   /* raus West, Pfad zieht Ost */
            }
        }
        /* Loch-Ringzeile via find_ring_row() ??? die ECHTE physische Ringzeile, die
           gerade die Map-Zeile des Lochs zeigt (g_row_map, exakt wie Map-Objekte).
           NICHT row&31: der 32-Zeilen-Ring ist NICHT direkt row%32, sondern wird in
           g_row_map getrackt (Bugfix 19.07.2026: row&31 setzte die Wuermer auf die
           falsche Y = "nicht aus den Rissen", zuckten, verschwanden mittig).
           screen_Y = (hole_ring<<3) - g_scr1_y, wie Terrain/mapobj. */
        g_wallworms[w].hole_ring      = find_ring_row((u16)WALLWORM_EXITS[exit_idx].row);
        return 1u;
    }
    return 0u;
}

/* ===== Wurmloch-Animations-Skript (25.07.2026, Nutzerwunsch) =====
   Statt Dauerschleife spielt jedes Loch seine Animation NUR beim Wurm-Austritt
   und -Eintritt, fest choreographiert:
     AUF:      138 -> 139 -> 140      (dann erscheint der Wurm / beginnt einzutauchen)
     FLATTERN: 139 -> 140 -> 139 -> 140 -> 139
     ZU:       138                     (fertig)
   Der Auto-Anim laesst Kopf 138 aus (zustandslos in anim_update, 2. Fassung);
   die Ruhelage der Zellen ist die Map-Grundkachel 138 (zu). Gezeichnet wird
   ueber denselben put_cell/remap-Pfad wie anim_update, auf die beim Skriptstart
   registrierten 138er-Anim-Zellen nahe des Lochs. */
#define WORMHOLE_ANIM_SLOTS 4u
#define WORMHOLE_ANIM_TICKS 3u   /* Wurm-Ticks je Schritt (x WORM_TICK_DIV = 6 Frames) */
#define WORMHOLE_SEQ_LEN    9u
#define WORMHOLE_WORM_STEP  3u   /* ab diesem Schritt erscheint der Wurm (nach 138,139,140) */
/* 25.07.2026 Nutzerkorrektur 2: Flattern wieder lang - 139,140,139,140,139. */
static const u16 wormhole_seq[WORMHOLE_SEQ_LEN] = { 138u,139u,140u,139u,140u,139u,140u,139u,138u };
typedef struct {
    u8 active, exit_idx, ring;
    u8 ncell; u8 cell_row[2], cell_col[2];   /* 138er-Zellen am Loch (RING-Zeile/Spalte, max 2) */
    u8 cell_flip[2];                          /* h-Flip je Zelle (aus lvl_map Bit 15 - statische 138er stehen nicht im Anim-Grid) */
    u8 step, tick;
    u8 pending_spawn;   /* 1 = bei Schritt WORMHOLE_WORM_STEP den Wurm spawnen */
} TWormholeAnim;
static TWormholeAnim g_wormhole_anims[WORMHOLE_ANIM_SLOTS];
static u8 g_wallworm_last_exit = 0xFFu;   /* zuletzt benutztes Loch (gegen "3 Wuermer aus demselben Loch hintereinander") */

static void wormhole_anim_draw(u8 s) {
    TWormholeAnim *wa = &g_wormhole_anims[s];
    u16 n = wormhole_seq[wa->step];
    u16 a_i2, b_i2; u8 a_p2, b_p2, a_hw2, j, flip2, ty, tx;
    a_i2 = g_lvl_tile_remap[lvl_tile_a_idx[n - 1u]];
    a_p2 = lvl_tile_a_pal[n - 1u];
    b_i2 = g_lvl_tile_remap[lvl_tile_b_idx[n - 1u]];
    b_p2 = lvl_tile_b_pal[n - 1u];
    a_hw2 = (a_p2 < 16u) ? g_scr2_pal_map[a_p2] : g_scr2_pal_map[0];
    for (j = 0u; j < wa->ncell; j++) {
        ty = wa->cell_row[j]; tx = wa->cell_col[j];
        flip2 = wa->cell_flip[j];   /* aus lvl_map Bit 15 - gilt auch fuer statische 138er ("f steht fuer flip") */
        put_cell(SCR_2_PLANE, a_hw2, tx, ty, g_lvl_vram[a_i2], flip2);
        put_cell(SCR_1_PLANE, b_p2,  tx, ty, g_lvl_vram[b_i2], flip2);
    }
}

/* Skript fuer Loch exit_idx starten. with_spawn=1: bei Schritt 3 den Wurm spawnen
   (Austritt); 0: nur Animation (Eintritt - der Wurm ist schon unterwegs). */
static void wormhole_anim_start(u8 exit_idx, u8 with_spawn) {
    u8 s;
    u8 ring = find_ring_row((u16)WALLWORM_EXITS[exit_idx].row);
    for (s = 0u; s < (u8)WORMHOLE_ANIM_SLOTS; s++)
        if (g_wormhole_anims[s].active && g_wormhole_anims[s].exit_idx == exit_idx) break;
    if (s >= (u8)WORMHOLE_ANIM_SLOTS)
        for (s = 0u; s < (u8)WORMHOLE_ANIM_SLOTS; s++)
            if (!g_wormhole_anims[s].active) break;
    if (s >= (u8)WORMHOLE_ANIM_SLOTS) return;
    g_wormhole_anims[s].active = 1u;
    g_wormhole_anims[s].exit_idx = exit_idx;
    g_wormhole_anims[s].ring = ring;
    g_wormhole_anims[s].step = 0u;
    g_wormhole_anims[s].tick = 0u;
    g_wormhole_anims[s].pending_spawn = with_spawn;
    g_wormhole_anims[s].ncell = 0u;
    {
        /* 25.07.2026 (Nutzer "alle 138 UND 138a"): Loch-Zellen direkt in lvl_map
           suchen - Kachel 138 mit ODER ohne Anim-Flag (die Map hat 9 "138a" + 8
           statische "138"; das Anim-Register kannte nur die 9). Fenster: bis 7
           Zeilen UEBER dem DOS-Austrittspunkt, +-4 Spalten (dort sitzen die
           Grafiken laut Map-Analyse). Die ZWEI naechstgelegenen Zellen gewinnen
           (Loecher bestehen teils aus 2 gespiegelten Haelften); h-Flip je Zelle
           aus lvl_map Bit 15 ("f steht fuer flip", Nutzer). */
        u16 erow = WALLWORM_EXITS[exit_idx].row;
        u8  ecol = WALLWORM_EXITS[exit_idx].col;
        s16 rr, cc2, d;
        s16 bd0 = 999, bd1 = 999;
        u8  br0 = 0u, bc0 = 0u, bf0 = 0u, br1 = 0u, bc1 = 0u, bf1 = 0u;
        for (rr = (s16)erow - 7; rr <= (s16)(erow + 2u); rr++) {
            if (rr < 0 || rr >= (s16)LVL_MAP_H) continue;
            for (cc2 = (s16)ecol - 4; cc2 <= (s16)(ecol + 4u); cc2++) {
                u16 raw; u8 ring2, fl; s16 adr, adc;
                if (cc2 < 0 || cc2 >= (s16)LVL_MAP_W) continue;
                raw = lvl_map[(u16)rr * (u16)LVL_MAP_W + (u16)cc2];
                if ((raw & 0x01FFu) != 138u) continue;
                ring2 = find_ring_row((u16)rr);
                if (ring2 == (u8)MAPOBJ_NONE) continue;   /* Zeile (noch) nicht im Ring */
                fl = (raw & 0x8000u) ? 1u : 0u;
                adr = (s16)(rr - (s16)erow); if (adr < 0) adr = (s16)-adr;
                adc = (s16)(cc2 - (s16)ecol); if (adc < 0) adc = (s16)-adc;
                d = (s16)(adr + adc);
                if (d < bd0) {
                    bd1 = bd0; br1 = br0; bc1 = bc0; bf1 = bf0;
                    bd0 = d; br0 = ring2; bc0 = (u8)cc2; bf0 = fl;
                } else if (d < bd1) {
                    bd1 = d; br1 = ring2; bc1 = (u8)cc2; bf1 = fl;
                }
            }
        }
        if (bd0 < 999) {
            g_wormhole_anims[s].cell_row[0] = br0; g_wormhole_anims[s].cell_col[0] = bc0;
            g_wormhole_anims[s].cell_flip[0] = bf0; g_wormhole_anims[s].ncell = 1u;
        }
        /* zweite Zelle nur, wenn sie dicht bei der ersten liegt (gespiegelte
           Loch-Haelfte), nicht ein ganz anderes Loch im Fenster */
        if (bd1 < 999 && (s16)(bd1 - bd0) <= 3) {
            g_wormhole_anims[s].cell_row[1] = br1; g_wormhole_anims[s].cell_col[1] = bc1;
            g_wormhole_anims[s].cell_flip[1] = bf1; g_wormhole_anims[s].ncell = 2u;
        }
    }
    wormhole_anim_draw(s);   /* Schritt 0 (138) sofort zeigen */
}

/* 25.07.2026: alle Loch-Skripte verwerfen. MUSS bei jedem Welt-Wisch laufen
   (game_start / respawn_do / shop_resume) - ein ueberlebendes Skript schrieb
   sonst nach dem Neuaufbau des Rings Loch-Kacheln in fremdes Terrain
   ("am Levelanfang lauter Wurmloecher", Nutzer 25.07.). */
static void wormhole_anims_reset(void) {
    u8 s;
    for (s = 0u; s < (u8)WORMHOLE_ANIM_SLOTS; s++) g_wormhole_anims[s].active = 0u;
    g_wallworm_last_exit = 0xFFu;
}

/* Ein Wurm-Tick fuer alle laufenden Loch-Skripte (aus wallworms_update). */
static void wormhole_anims_update(void) {
    u8 s;
    for (s = 0u; s < (u8)WORMHOLE_ANIM_SLOTS; s++) {
        TWormholeAnim *wa = &g_wormhole_anims[s];
        if (!wa->active) continue;
        /* Loch aus dem 32er-Ring gescrollt -> Skript beenden (nie in fremde Zellen
           schreiben). 25.07.2026 VERSCHAERFT (Nutzer "am Levelanfang lauter
           Wurmloecher"): zusaetzlich pruefen, dass die gemerkten Ringzeilen noch
           WIRKLICH die Map-Zeilen des Lochs zeigen (g_row_map). Nach einem
           Level-Neustart/Respawn wird der Ring komplett neu belegt - die alte
           Ringzeile existiert dann zwar noch, zeigt aber voellig anderes Terrain;
           das Skript stempelte dort weiter Loch-Kacheln hinein. */
        if (find_ring_row((u16)WALLWORM_EXITS[wa->exit_idx].row) != wa->ring) { wa->active = 0u; continue; }
        {
            u8 bad = 0u, q;
            for (q = 0u; q < wa->ncell; q++)
                if (g_row_map[wa->cell_row[q]] >= (u16)LVL_MAP_H) { bad = 1u; break; }
            if (bad) { wa->active = 0u; continue; }
        }
        wa->tick++;
        if (wa->tick < (u8)WORMHOLE_ANIM_TICKS) continue;
        wa->tick = 0u;
        wa->step++;
        if (wa->step >= (u8)WORMHOLE_SEQ_LEN) { wa->active = 0u; continue; }
        wormhole_anim_draw(s);
        if (wa->pending_spawn && wa->step >= (u8)WORMHOLE_WORM_STEP) {
            if (wallworm_spawn(wa->exit_idx)) { g_wallworm_last_exit = wa->exit_idx; wa->pending_spawn = 0u; }
        }
    }
}

#if WORM_TEST_MODE
/* Testphase (B-Taste): einen Wurm am Loch spawnen, dessen Spalte dem Schiff am
   naechsten liegt ??? kommt oben in dieser Spalte raus (hole_ring = aktuelle
   Top-Ringzeile via wallworm_spawn) und kriecht seinen Pfad. So kann man sich in
   eine Loch-Spalte stellen und den Wurm auf Knopfdruck ausloesen (unabhaengig
   vom Asteroiden-Abschnitt). */
static void wallworm_spawn_test(void) {
    u8 e, best = 0xFFu, k;
    s16 pcol = (s16)((s16)g_player.x + 8);   /* Schiffsmitte in px */
    s16 bestd = 9999;
    for (e = 0u; e < 16u; e++) {
        s16 hx, d; u8 ring = find_ring_row((u16)WALLWORM_EXITS[e].row);
        if (ring == (u8)MAPOBJ_NONE || (u8)((u8)(ring << 3) - g_scr1_y) >= (u8)CLIP_Y) continue;   /* nur sichtbare Risse */
        hx = (s16)((s16)WALLWORM_EXITS[e].col * 8);
        d = (s16)(hx - pcol);
        if (d < 0) d = (s16)-d;
        if (d < bestd) { bestd = d; best = e; }
    }
    if (best == 0xFFu) return;   /* kein Loch gerade sichtbar */
    if (!wallworm_spawn(best)) {
        /* Alle Slots voll -> Slot 0 hart freigeben (OAM zurueck) und neu spawnen,
           damit B beliebig oft ausloest (nur Testphase). */
        for (k = 0u; k < (u8)WALLWORM_SEGMENTS; k++)
            if (g_wallworms[0].part[k].oam != OAM_NONE) {
                UnsetSprite(g_wallworms[0].part[k].oam);
                oam_pool_free(g_wallworms[0].part[k].oam);
                g_wallworms[0].part[k].oam = OAM_NONE;
            }
        g_wallworms[0].active = 0u;
        g_wallworms[0].part_count = 0u;
        wallworm_spawn(best);
    }
}
#endif

static void wallworm_tick(u8 w) {
    TWallWorm *ww = &g_wallworms[w];
    u8 k, pth;
    if (!ww->active) return;
    g_busy_wworms = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
    /* 1) Kette bewegen (rueckwaerts, LOCH-RELATIV): jedes Teil kopiert die
       Vor-Tick-Loch-Position des Vordermanns -> 1-Logik-Tick-Versatz = Kettenabstand.
       Kein Scroll hier drin (das kommt erst beim Zeichnen dazu). */
    for (k = ww->part_count; k > 1u; k--) {
        u8 i = (u8)(k - 1u);
        ww->part[i].prev_wx = ww->part[i].wx; ww->part[i].prev_wy = ww->part[i].wy;
        ww->part[i].wx = ww->part[i - 1u].prev_wx; ww->part[i].wy = ww->part[i - 1u].prev_wy;
        ww->part[i].dir = ww->part[i - 1u].dir;
        if (ww->part[i].emerge_ct < 0xFFu) ww->part[i].emerge_ct++;
    }
    if (ww->part_count > 0u) {
        ww->part[0].prev_wx = ww->part[0].wx; ww->part[0].prev_wy = ww->part[0].wy;
        if (ww->part[0].emerge_ct < 0xFFu) ww->part[0].emerge_ct++;
        if (ww->head_alive) {
            ww->part[0].wx = (s16)(ww->head_x_fix >> 4);   /* Loch-relative Kopf-Position (px) */
            ww->part[0].wy = (s16)(ww->head_y_fix >> 4);
            ww->part[0].dir = ww->head_angle;
        } else {
            /* Kopf weg: Neck friert ein, verschwindet naechsten Tick lautlos (kein Ball, kein Punkt). */
            if (ww->part[0].oam != OAM_NONE) { UnsetSprite(ww->part[0].oam); oam_pool_free(ww->part[0].oam); ww->part[0].oam = OAM_NONE; }
            for (k = 0u; k < (u8)(ww->part_count - 1u); k++) ww->part[k] = ww->part[k + 1u];
            ww->part_count--;
            ww->part[ww->part_count].oam = OAM_NONE;
            ww->part[ww->part_count].alive = 0u;
        }
    }
    /* 2) Kopf WORM_STEPS_PER_TICK Schritte weiter: PROLOG (horizontal aus der
       Roehre) -> Original-Pfad 1:1 -> EPILOG (horizontal zur Wand, stirbt im
       Terrain = verschwindet in einer Roehre). Kopf-Weg pro Tick = Segment-
       Abstand (Kette kopiert 1 Tick versetzt). */
    if (ww->head_alive) {
        u8 hs;
        u8 west0 = ww->out_west;   /* Emergenz-Richtung (terrain-basiert, siehe Spawn) */
        pth = WALLWORM_EXITS[ww->exit_idx].path;
        for (hs = 0u; hs < (u8)WORM_STEPS_PER_TICK && ww->head_alive; hs++) {
            if (ww->returning) {
                /* EPILOG Fassung 6 (25.07.2026, Nutzer "noch 90/120-Grad-Knicks"):
                   DURCHGEHEND kurven, NIE die Richtung hart setzen. Beide Phasen
                   fliegen dieselbe 8-Richtungs-Steuerung (max 45 Grad je 2px);
                   Phase 0 zielt auf den ANFLUGPUNKT (24px vor der offenen
                   Roehrenseite), Phase 1 kurvt von dort weiter ins Loch - durch
                   den vorgelagerten Anflugpunkt ist die Einfahrt praktisch
                   horizontal und immer kopfvoran, aber ohne Knick. Tod im Loch. */
                s16 tx, ty, wx, wy, dx, dy, adx, ady, hsx, hsy;
                u8 want, cur, diff;
                wx = (s16)(ww->head_x_fix >> 4);
                wy = (s16)(ww->head_y_fix >> 4);
                if (ww->ret_phase == 0u) { tx = ww->ret_ax; ty = ww->ret_ay; }
                else                     { tx = ww->ret_tx; ty = ww->ret_ty; }
                dx = (s16)(tx - wx); dy = (s16)(ty - wy);
                adx = dx; if (adx < 0) adx = (s16)-adx;
                ady = dy; if (ady < 0) ady = (s16)-ady;
                if (ww->ret_phase == 0u && adx <= 4 && ady <= 4) {
                    ww->ret_phase = 1u;   /* Anflugpunkt erreicht -> weiter (kurvend) ins Loch */
                    wormhole_anim_start(ww->ret_exit, 0u);   /* 25.07.2026: Ziel-Loch oeffnet sich (138,139,140), dann Flattern beim Eintauchen */
                    dx = (s16)(ww->ret_tx - wx); dy = (s16)(ww->ret_ty - wy);
                    adx = dx; if (adx < 0) adx = (s16)-adx;
                    ady = dy; if (ady < 0) ady = (s16)-ady;
                }
                if (ww->ret_phase == 1u && adx <= 2 && ady <= 2) {
                    ww->head_alive = 0u;   /* im Ziel-Loch angekommen */
                } else {
                    /* Zielrichtung auf 8 Richtungen quantisieren (Dominanz 2:1) */
                    if (adx > (s16)(ady * 2))      want = (u8)(dx > 0 ? 0u : 4u);
                    else if (ady > (s16)(adx * 2)) want = (u8)(dy > 0 ? 2u : 6u);
                    else if (dx > 0)               want = (u8)(dy > 0 ? 1u : 7u);
                    else                           want = (u8)(dy > 0 ? 3u : 5u);
                    cur = ww->head_angle;
                    diff = (u8)((u8)(want - cur) & 7u);
                    ww->head_frame++;
                    if (diff != 0u && (ww->head_frame & 1u)) {   /* max 45 Grad je 2px */
                        if (diff <= 4u) cur = (u8)((u8)(cur + 1u) & 7u);
                        else            cur = (u8)((u8)(cur + 7u) & 7u);
                        ww->head_angle = cur;
                    }
                    ww->head_x_fix = (s16)(ww->head_x_fix + (s16)((s16)worm_dir8_dx[ww->head_angle] * 16));
                    ww->head_y_fix = (s16)(ww->head_y_fix + (s16)((s16)worm_dir8_dy[ww->head_angle] * 16));
                    /* Notbremse: aus dem Bild gekurvt -> still verschwinden */
                    hsx = (s16)((s16)((s16)WALLWORM_EXITS[ww->exit_idx].col * 8) + (ww->head_x_fix >> 4));
                    hsy = (s16)(wallworm_hole_screen_y_ww(ww) + (ww->head_y_fix >> 4));
                    if (hsx < -24 || hsx >= (s16)(SCR_W + 24) || hsy < -48 || hsy >= (s16)(CLIP_Y + 48))
                        ww->head_alive = 0u;
                }
            } else if (ww->head_frame < (u16)WORM_PROLOG_STEPS) {
                /* PROLOG: horizontal aus der Roehre (links=Ost, rechts=West), 1px/Schritt. */
                ww->head_x_fix = (s16)(ww->head_x_fix + (west0 ? (s16)-16 : (s16)16));
                ww->head_angle = (u8)(west0 ? 4u : 0u);
                ww->head_frame++;
            } else {
                u16 pf = (u16)(ww->head_frame - (u16)WORM_PROLOG_STEPS);
                u16 plen = worm_path_len[pth];
                if (plen > (u16)WORM_PATH_MAX_STEPS) plen = (u16)WORM_PATH_MAX_STEPS;
                if (pf >= plen) {
                    /* Pfad-Ende -> Rueckkehr einleiten. 25.07.2026 (Nutzer: "im
                       Original kommen Wuermer definitiv in ein Loch zurueck"):
                       Ziel = das NAECHSTGELEGENE Loch (Original: der Pfad endet an
                       einem - meist ANDEREN - Loch, dort laeuft die Kette ein).
                       Beim 1/2-skalierten Pfad liegt das naechste Loch kurz neben
                       dem Pfadende -> kurzer, natuerlicher Restweg statt der alten
                       Quer-ueber-den-Schirm-Rueckkehr zum eigenen Spawn-Loch. Ziel
                       relativ zum EIGENEN Loch gespeichert (Ring-Deltas sind
                       scroll-unabhaengig, u8-Wrap der 32er-Ringe beachtet). */
                    s16 wx = (s16)(ww->head_x_fix >> 4);
                    s16 wy = (s16)(ww->head_y_fix >> 4);
                    u8  own_col = WALLWORM_EXITS[ww->exit_idx].col;
                    s16 best_d = 32767; s16 best_x = 0; s16 best_y = 0;
                    u8  e2, best_e = 0xFFu, ow2;
                    for (e2 = 0u; e2 < 16u; e2++) {
                        u8 ring2 = find_ring_row((u16)WALLWORM_EXITS[e2].row);
                        s16 ex, ey, dd, adx, ady;
                        u8 dy8;
                        if (ring2 == (u8)MAPOBJ_NONE) continue;
                        ex  = (s16)(((s16)WALLWORM_EXITS[e2].col - (s16)own_col) * 8);
                        dy8 = (u8)((u8)(ring2 << 3) - (u8)(ww->hole_ring << 3));
                        ey  = (s16)((s8)dy8);   /* toroidaler 256px-Ring -> signed Delta */
                        adx = (s16)(ex - wx); if (adx < 0) adx = (s16)-adx;
                        ady = (s16)(ey - wy); if (ady < 0) ady = (s16)-ady;
                        dd  = (s16)(adx + ady);
                        if (dd < best_d) { best_d = dd; best_x = ex; best_y = ey; best_e = e2; }
                    }
                    if (best_e == 0xFFu) { best_x = 0; best_y = 0; ow2 = ww->out_west; }   /* Fallback: eigenes Loch */
                    else ow2 = wallworm_exit_open_west(best_e);
                    ww->ret_exit = (u8)(best_e == 0xFFu ? ww->exit_idx : best_e);
                    /* 25.07.2026 (Nutzer "Glieder verschwinden beim Eintreten zu frueh"):
                       das Sterbeziel liegt HINTER dem Loch-Zentrum (in die Wand
                       hinein) - der Kettenaufloesepunkt sitzt damit unter dem Felsrand
                       (b-Ebene), die Glieder verschwinden erst verdeckt, nicht davor.
                       Tiefe 5px (10 war laut Nutzer zu viel). */
                    ww->ret_tx = (s16)(best_x + (ow2 ? 5 : -5));
                    ww->ret_ty = best_y;
                    /* Einfahrt IMMER kopfvoran entgegengesetzt zur Auskriech-Richtung des
                       Ziel-Lochs (Nutzer: "nicht von hinten eintauchen"): Anflugpunkt 16px
                       VOR der offenen Seite, von dort gerade rein. */
                    ww->ret_entry_dir = (u8)(ow2 ? 0u : 4u);   /* offen West -> Einfahrt nach Ost */
                    ww->ret_ax = (s16)(best_x + (ow2 ? (s16)-24 : (s16)24));   /* 24px: Platz zum Ausrichten (kein Knick am Anflugpunkt) */
                    ww->ret_ay = best_y;
                    ww->ret_phase = 0u;
                    ww->head_frame = 0u;   /* Kurvenflug: Dreh-Taktteiler (45 Grad je 2px) */
                    ww->returning = 1u;
                    ww->ret_west  = (u8)(ww->ret_entry_dir == 4u);
                } else {
                    u16 o = (u16)(worm_path_off[pth] + pf);
                    s16 pdx = (s16)worm_path_data[(u32)o * 2u];
                    u8  pdir = worm_path_dir8[o];
                    if (ww->flip_x) { pdx = (s16)-pdx; pdir = (u8)((u8)(4u - pdir) & 7u); }
                    ww->head_x_fix = (s16)(ww->head_x_fix + pdx);
                    ww->head_y_fix = (s16)(ww->head_y_fix + (s16)worm_path_data[(u32)o * 2u + 1u]);
                    ww->head_angle = pdir;
                    ww->head_frame++;
                }
            }
        }
    }
    /* 3) Emergenz: ein Segment pro Tick am Loch (wx=wy=0), solange Kopf lebt. */
    if (ww->emerged < (u8)WALLWORM_SEGMENTS && ww->head_alive && ww->part_count < (u8)WALLWORM_SEGMENTS) {
        TWallWormPart *seg = &ww->part[ww->part_count];
        seg->wx = seg->prev_wx = 0; seg->wy = seg->prev_wy = 0;
        seg->dir = ww->head_angle;
        seg->alive = 1u; seg->emerge_ct = 0u;
        seg->oam = OAM_NONE; seg->last_snum = 0u; seg->last_flip = 0xFFu;
        ww->part_count++; ww->emerged++;
    }
    if (!ww->head_alive && ww->part_count == 0u) ww->active = 0u;
}

/* Trefferregel (Centipede): Teil k loest sich als Ball, Rest rueckt auf. Kopf unverwundbar. */
static void wallworm_seg_hit(u8 w, u8 k) {
    TWallWorm *ww = &g_wallworms[w];
    u8 i;
    s16 sx = (s16)((s16)((s16)WALLWORM_EXITS[ww->exit_idx].col * 8) + ww->part[k].wx);
    s16 sy = (s16)(wallworm_hole_screen_y_ww(ww) + ww->part[k].wy);
    wallworm_ball_spawn((u8)sx, (u8)sy, (u8)(k == 0u ? 1u : 0u));
    if (ww->part[k].oam != OAM_NONE) { UnsetSprite(ww->part[k].oam); oam_pool_free(ww->part[k].oam); }
    for (i = k; i < (u8)(ww->part_count - 1u); i++) ww->part[i] = ww->part[i + 1u];
    ww->part_count--;
    ww->part[ww->part_count].oam = OAM_NONE;
    ww->part[ww->part_count].alive = 0u;
    g_score = (u16)(g_score + (k == 0u ? (u16)WALLWORM_NECK_SCORE : (u16)WALLWORM_SEG_SCORE));
    if (!ww->head_alive && ww->part_count == 0u) ww->active = 0u;
}

static void wallworm_draw(u8 w) {
    TWallWorm *ww = &g_wallworms[w];
    s16 holey, holex;
    u8 k;
    if (!ww->active) {
        for (k = 0u; k < (u8)WALLWORM_SEGMENTS; k++)
            if (ww->part[k].oam != OAM_NONE) { UnsetSprite(ww->part[k].oam); oam_pool_free(ww->part[k].oam); ww->part[k].oam = OAM_NONE; }
        return;
    }
    holey = wallworm_hole_screen_y_ww(ww);   /* aktuelle Loch-Bildschirm-Y (JEDEN Frame -> glattes Mitscrollen) */
    holex = (s16)((s16)WALLWORM_EXITS[ww->exit_idx].col * 8);
    for (k = 0u; k < (u8)WALLWORM_SEGMENTS; k++) {
        TWallWormPart *p = &ww->part[k];
        u8  rot = (u8)(k == 0u ? WALLWORM_HEAD_ROT : WALLWORM_BODY_ROT);
#if WORM_NO_ROTATE
        u8  ddir = (u8)(k == 0u ? p->dir : (u8)WORM_BODY_DIR);   /* 25.07.2026: Kopf dreht, Koerper festes Sprite (Cache greift immer) */
#else
        u8  ddir = p->dir;
#endif
        s16 sx  = (s16)(holex + p->wx);   /* X ohne Scroll -> immer exakt die Loch-Spalte */
        s16 sy  = (s16)(holey + p->wy);
        u8  prio;
        u8  show = (u8)(k < ww->part_count && p->alive && sy >= 0 && sy < (s16)CLIP_Y && sx >= 0 && sx < (s16)SCR_W);
        if (!show) {
            if (p->oam != OAM_NONE) { UnsetSprite(p->oam); oam_pool_free(p->oam); p->oam = OAM_NONE; p->last_dir = 0xFFu; p->last_flip = 0xFFu; p->last_prio = 0xFFu; }
            continue;
        }
        /* 25.07.2026 EBENEN-SANDWICH (Nutzer: "Sprite unter Kachel 137, aber ueber
           138-140"): nahe an einem Loch liegt der Wurm auf SPR_MIDDLE = ueber der
           a-Ebene (Loch-Inneres 138-140), unter der b-Ebene (Fels 137 + Loch-
           Raender) -> der Fels selbst blendet den Wurm pixelgenau aus/ein, es
           sieht nach echtem Ein-/Austauchen aus. Kein terrain_solid/FURTHEST mehr
           (das liess Segmente zu frueh komplett verschwinden). Abseits der Loecher
           SPR_FRONT, damit der Kurvenflug nie hinter Felsvorspruengen haengt. */
        {
            s16 hdx = p->wx, hdy = p->wy;
            if (hdx < 0) hdx = (s16)-hdx;
            if (hdy < 0) hdy = (s16)-hdy;
            prio = (u8)SPR_FRONT;
            if ((s16)(hdx + hdy) <= 16) {
                prio = (u8)SPR_MIDDLE;   /* nahe am EIGENEN Loch (Austritt) */
            } else if (ww->returning) {
                s16 tdx = (s16)(p->wx - ww->ret_tx);
                s16 tdy = (s16)(p->wy - ww->ret_ty);
                if (tdx < 0) tdx = (s16)-tdx;
                if (tdy < 0) tdy = (s16)-tdy;
                if ((s16)(tdx + tdy) <= 20) prio = (u8)SPR_MIDDLE;   /* nahe am ZIEL-Loch (Eintritt; +4 weil das Ziel 10px in der Wand liegt) */
            }
        }
        /* 24.07.2026 Draw-Cache (Wurmband-fps): unveraenderte (Anzeige-)Richtung -> Rotset-
           Sprite, Flip UND Palette identisch zum Vorframe. Dann KEIN 2x-2D-ROM-Lookup, KEIN
           SetSprite - nur Position schreiben. SpriteControl (OAM read-modify-write) nur,
           wenn die Ebene (emerge-Prio) tatsaechlich wechselt. oam!=NONE garantiert einen
           vorangegangenen Vollredraw, also gueltiges last_dir/last_flip. Mit WORM_NO_ROTATE
           ist ddir fuer den Koerper konstant -> ab dem 2. Frame immer dieser Fastpath. */
        if (p->oam != OAM_NONE && ddir == p->last_dir) {
            SetSpritePosition(p->oam, (u8)sx, (u8)sy);
            if (prio != p->last_prio) { SpriteControl(p->oam, prio, p->last_flip); p->last_prio = prio; }
            continue;
        }
        {
            u16 raw  = lvl_rotset_idx[rot][ddir];
            u8  rf   = lvl_rotset_flip[rot][ddir];
            u8  flip = (u8)(((rf & 1u) ? SPR_HFLIP : 0u) | ((rf & 2u) ? SPR_VFLIP : 0u));
            if (raw == 0xFFFFu) {
                if (p->oam != OAM_NONE) { UnsetSprite(p->oam); oam_pool_free(p->oam); p->oam = OAM_NONE; }
                p->last_dir = 0xFFu; p->last_flip = 0xFFu; p->last_prio = 0xFFu;
                continue;
            }
            if (p->oam == OAM_NONE) {
                p->oam = oam_pool_alloc();
                if (p->oam == OAM_NONE) continue;
            }
            SetSprite(p->oam, spr_vram(raw), 0, (u8)sx, (u8)sy, lvl_rotset_pal[rot][ddir]);
            SpriteControl(p->oam, prio, flip);   /* SetSprite hat Byte1 auf FRONT/kein-Flip zurueckgesetzt -> Prio+Flip neu anwenden */
            p->last_dir = ddir; p->last_flip = flip; p->last_prio = prio;
        }
    }
}

static void wallworm_balls_update_draw(void) {
    u8 b;
    for (b = 0u; b < (u8)WALLWORM_BALLS; b++) {
        TWallWormBall *bl = &g_wallworm_balls[b];
        s16 nx, ny;
        if (!bl->active) {
            /* BUGFIX 22.07.2026 (Nutzerbericht "nach dem Tod bleiben manchmal
               Sprites statisch stehen"): hier stand nur ein continue. Der
               OAM-Slot wurde ausschliesslich beim natuerlichen Verfall
               freigegeben - wird ein Ball von aussen deaktiviert (respawn_do
               wischt sie seit heute mit), blieb sein Sprite mit alten
               Koordinaten fuer immer im Bild und hielt dazu den Slot. */
            if (bl->oam != OAM_NONE) {
                UnsetSprite(bl->oam); oam_pool_free(bl->oam); bl->oam = OAM_NONE;
            }
            continue;
        }
        g_busy_wworms = 1u;   /* Frueh-Ausstieg: fliegende Baelle halten das System wach,
                                 auch wenn ihr Wurm laengst weg ist */
        nx = (s16)((s16)bl->x + (s16)wallworm_ball_vx[bl->dir]);
        ny = (s16)((s16)bl->y + (s16)wallworm_ball_vy[bl->dir]);
        bl->anim++;
        if (bl->life > 0u) bl->life--;
        if (bl->life == 0u || nx < 0 || nx >= (s16)SCR_W || ny < 0 || ny >= (s16)CLIP_Y) {
            if (bl->oam != OAM_NONE) { UnsetSprite(bl->oam); oam_pool_free(bl->oam); bl->oam = OAM_NONE; }
            bl->active = 0u;
            continue;
        }
        bl->x = (u8)nx; bl->y = (u8)ny;
        {
            /* Spin: die 8 Richtungstiles des Koerper-/Kopf-Rotationssatzes als Dreh-Frames. */
            u8  rot  = (u8)(bl->big ? WALLWORM_HEAD_ROT : WALLWORM_BODY_ROT);
            u8  d    = (u8)((bl->anim >> 1) & 7u);
            u16 raw  = lvl_rotset_idx[rot][d];
            u8  rf   = lvl_rotset_flip[rot][d];
            u8  flip = (u8)(((rf & 1u) ? SPR_HFLIP : 0u) | ((rf & 2u) ? SPR_VFLIP : 0u));
            if (raw == 0xFFFFu) continue;
            if (bl->oam == OAM_NONE) {
                bl->oam = oam_pool_alloc();
                if (bl->oam == OAM_NONE) continue;
                bl->last_snum = 0u; bl->last_flip = 0xFFu;
            }
            if (raw == bl->last_snum && flip == bl->last_flip) {
                SetSpritePosition(bl->oam, bl->x, bl->y);
            } else {
                SetSprite(bl->oam, spr_vram(raw), 0, bl->x, bl->y, lvl_rotset_pal[rot][d]);
                SpriteControl(bl->oam, SPR_FRONT, flip);
                bl->last_snum = raw; bl->last_flip = flip;
            }
        }
    }
}

static u8  g_wallworm_div;        /* Frame-Teiler fuer die Wurm-Logik (siehe WORM_TICK_DIV) */
static u8  g_wallworm_spawn_cd;   /* Spawn-Stagger-Cooldown (siehe WORM_SPAWN_INTERVAL) */
/* Ist Loch e gerade ON-SCREEN? find_ring_row() liefert die Ringzeile (oder
   MAPOBJ_NONE, wenn die Map-Zeile nicht im 32-Zeilen-Fenster ist); dann pruefen,
   ob die daraus folgende Screen-Y im sichtbaren Bereich [0..CLIP_Y) liegt. So
   spawnt der Wurm nur an tatsaechlich sichtbaren Rissen (Bugfix 19.07.2026). */
static u8 wallworm_hole_visible(u8 e) {
    u8 ring = find_ring_row((u16)WALLWORM_EXITS[e].row);
    u8 sy;
    if (ring == (u8)MAPOBJ_NONE) return 0u;
    sy = (u8)((u8)((u8)(ring << 3) - g_scr1_y));
    return (u8)(sy < (u8)CLIP_Y);
}
/* 25.07.2026 (Nutzer + Original-Abgleich enemies.ts updateWorms): das Original
   wuerfelt ueber ALLE 16 Loecher (Doppel selten, 1/16); unser Sichtbarkeits-
   filter (19.07.) schrumpft die Auswahl auf 2-4 Loecher -> QRandom traf staendig
   dasselbe ("3 Wuermer aus demselben Loch hintereinander"). Fix: Loecher mit
   bereits AKTIVEM Wurm ausschliessen und das zuletzt benutzte meiden, wenn es
   Alternativen gibt. 0xFF = kein waehlbares Loch. */
static u8 wallworm_pick_exit(void) {
    u8 vis[16]; u8 nvis = 0u; u8 e, wi, used;
    for (e = 0u; e < 16u; e++) {
        if (!wallworm_hole_visible(e)) continue;
        used = 0u;
        for (wi = 0u; wi < (u8)WALLWORM_SLOTS; wi++)
            if (g_wallworms[wi].active && g_wallworms[wi].exit_idx == e) { used = 1u; break; }
        /* 25.07.2026: auch Loecher mit laufendem Anim-Skript meiden (dort oeffnet
           sich gerade ein Loch fuer einen anderen Wurm oder eine Rueckkehr) */
        if (!used)
            for (wi = 0u; wi < (u8)WORMHOLE_ANIM_SLOTS; wi++)
                if (g_wormhole_anims[wi].active && g_wormhole_anims[wi].exit_idx == e) { used = 1u; break; }
        if (used) continue;
        vis[nvis] = e; nvis++;
    }
    if (nvis > 1u) {   /* das zuletzt benutzte Loch meiden (Exit-Indizes sind eindeutig -> es bleibt >= 1) */
        u8 t = 0u, j;
        for (j = 0u; j < nvis; j++) { if (vis[j] != g_wallworm_last_exit) { vis[t] = vis[j]; t++; } }
        if (t > 0u) nvis = t;
    }
    if (nvis == 0u) return 0xFFu;
    return vis[(u8)(QRandom() % nvis)];
}
static void wallworms_update(void) {
    u8 w;
    /* 25.07.2026 Frueh-Ausstieg: das busy-Flag MUSS hier oben stehen, VOR dem
       Tick-Teiler. wallworm_tick() laeuft nur jeden WORM_TICK_DIV-ten Frame,
       gezeichnet wird aber JEDEN Frame (die Wuermer scrollen mit dem Terrain
       mit). Wuerde das Flag nur im Tick gesetzt, stuenden sie zwei von drei
       Frames still. Balls zaehlen mit: sie werden in wallworms_draw bewegt,
       haben also keine eigene Update-Stelle, die das Flag halten koennte. */
    for (w = 0u; w < (u8)WALLWORM_SLOTS; w++)
        if (g_wallworms[w].active) { g_busy_wworms = 1u; break; }
    for (w = 0u; w < (u8)WALLWORM_BALLS; w++)
        if (g_wallworm_balls[w].active) { g_busy_wworms = 1u; break; }
    /* Logik + Spawn nur jeden WORM_TICK_DIV-ten Frame (ein "Tick"). */
    g_wallworm_div++;
    if (g_wallworm_div < (u8)WORM_TICK_DIV) return;
    g_wallworm_div = 0u;
    wormhole_anims_update();   /* 25.07.2026: Loch-Anim-Skripte (VOR dem Band-Fruehausstieg, damit sie beim Rausscrollen sauber enden) */
    /* DOS-Modell (enemies.ts updateWorms / 1000:655b): bis zu WALLWORM_SLOTS Wuermer
       gleichzeitig, jeder an einem ZUFAELLIGEN sichtbaren Loch ??? aber GESTAFFELT
       (Nutzer 19.07.: "kommen alle gleichzeitig raus"): nur EIN Spawn pro
       WORM_SPAWN_INTERVAL Ticks. Slot wird frei sobald sein Wurm weg ist -> Respawn.
       Loecher/Pfade deckungsgleich zum Original (16 VERSCHIEDENE DOS-Pfade). */
    /* OPTIMIERUNG 22.07.2026: die Loch-Suche darunter prueft alle 16 Loecher,
       jede Pruefung durchsucht in find_ring_row() 32 Ringzeilen - 512
       Durchlaeufe. Und weil g_wallworm_spawn_cd NUR bei erfolgreichem Spawn
       gesetzt wird, lief das ausserhalb des Wurmbands jeden dritten Frame
       durch, obwohl dort nie ein Loch sichtbar sein kann.
       Die Loecher liegen alle in einem schmalen Zeilenband; ausserhalb davon
       (plus Bildschirmhoehe Reserve nach beiden Seiten) kann keines im Ring
       liegen. Dann gar nicht erst suchen.
       Bewusst KEINE Umkehrtabelle: die brachte zwar dasselbe, zerlegte aber
       die Wandwuermer (siehe find_ring_row). */
    {
        u16 prow = (u16)(g_scroll_y >> 3);
        if (prow + (u16)LVL_SCREEN_ROWS < (u16)WALLWORM_ROW_MIN ||
            prow > (u16)WALLWORM_ROW_MAX + (u16)LVL_SCREEN_ROWS) {
            for (w = 0u; w < (u8)WALLWORM_SLOTS; w++) wallworm_tick(w);
            return;   /* kein Loch in Reichweite - Suche entfaellt */
        }
    }
#if BENCH_DETERM_WORM
    /* 24.07.2026 Wurmband-Benchmark: ALLE Slots sofort + dauerhaft fuellen (kein
       Stagger, kein Cooldown) -> konstante Wurm-Maximallast fuer wiederholbare
       Messungen. Danach normal ticken und raus (staggerte Spawn-Logik ueberspringen). */
    {
        for (w = 0u; w < (u8)WALLWORM_SLOTS; w++) {
            if (!g_wallworms[w].active) {
                u8 pick = wallworm_pick_exit();   /* 25.07.2026: gleiche Loch-Wahl wie im Spiel */
                if (pick != 0xFFu && wallworm_spawn(pick)) g_wallworm_last_exit = pick;
            }
        }
        for (w = 0u; w < (u8)WALLWORM_SLOTS; w++) wallworm_tick(w);
        return;
    }
#endif
    if (g_wallworm_spawn_cd > 0u) {
        g_wallworm_spawn_cd--;
    } else {
        for (w = 0u; w < (u8)WALLWORM_SLOTS; w++) {
            if (!g_wallworms[w].active) {
                u8 pick = wallworm_pick_exit();   /* 25.07.2026: belegte + zuletzt benutzte + skript-aktive Loecher meiden */
                if (pick != 0xFFu) {
                    /* 25.07.2026 Loch-Skript: erst oeffnet sich das Loch (138,139,
                       140), DANN erscheint der Wurm (wormhole_anims_update spawnt
                       ihn bei Schritt 3). last_exit sofort merken. */
                    wormhole_anim_start(pick, 1u);
                    g_wallworm_last_exit = pick;
                    g_wallworm_spawn_cd = (u8)WORM_SPAWN_INTERVAL;
                }
                break;   /* nur einen Slot pro Tick betrachten -> gestaffelt */
            }
        }
    }
    for (w = 0u; w < (u8)WALLWORM_SLOTS; w++) wallworm_tick(w);
}
static void wallworms_draw(void) {
    u8 w;
    for (w = 0u; w < (u8)WALLWORM_SLOTS; w++) wallworm_draw(w);
    wallworm_balls_update_draw();
}

/* ============ WANDKRIECHER ("Schleim", 28.07.2026) ============
   Original: Xenon 2, Level 1, EVENTSBG.PC Typ 4 ("wall pod") ??? Handler
   1000:705b, Update 1000:6e2c, portiert in src/game/turrets.ts (updatePod/
   podBurst). Ein an der Wand haengendes SPRITE (kein Map-Kachel-Turm wie die
   Typen 1-3), das senkrecht auf und ab kriecht und PLATZT, sobald das Schiff
   auf gleicher Hoehe ist: drei Kugeln faecherfoermig quer in den Gang, danach
   bleibt die leere Huelle haengen ??? weiter abschiessbar, gibt dann die Punkte.
   Im Original 11 Stueck (S1/EVENTSBG.PC, Laufweg word3 = 5..10 Zellen).

   Umrechnung auf NGPC (siehe CLAUDE.md ??6):
     Laufweg word3 x 16 DOS-px  -> amp Zellen a 8 px      (Kachelzahl bleibt)
     1 px/Tick Kriechtempo      -> LVL_WCRAWL_STEP/16 px/Frame (5/16 ~ 0,3)
     Fenster -30..+10 DOS-px    -> WIN_UP 15 / WIN_DOWN 5 px
     Wuerfel 0x32/256 je Tick   -> LVL_WCRAWL_CHANCE/256 je Frame (77 ~ 30 %)
     8 px/Tick Schusstempo      -> 43/16 px/Frame (Projekt-Massstab wie
                                   ENEMY_SHOT_SPEED_FIX, das fuer 6 px/Tick 32 nimmt)
     HP 2 / 250 Punkte          -> unveraendert

   AUFHAENGUNG AN DER MAP: der Kriecher haengt an einer Map-Zeile, nicht am
   Bildschirm. Die Zuordnung Map-Zeile -> Ringzeile passiert an genau EINER
   Stelle (lvl1_put_row), dort haengt wcrawl_row_in() mit drin ??? dasselbe
   Muster wie mapobj_grid_update_row() und wie der Wandwurm sein Loch merkt
   (TWallWorm.hole_ring). find_ring_row() pro Frame waere eine lineare Suche
   ueber 32 Ringzeilen JE Kriecher; so ist es ein Tabellen-Zugriff.

   Solange das Tool die Daten noch nicht exportiert hat, greift die
   Uebergangstabelle unten ??? sie verschwindet automatisch, sobald map.h
   LVL_WCRAWL_COUNT mitbringt. */
#ifndef LVL_WCRAWL_COUNT
/* --- Uebergangswerte bis zum naechsten map.h-Export (28.07.2026) ---
   Vier Testplatzierungen an echten Waenden (Map-Zeilen gegen lvl_map geprueft:
   14/24 im ersten Levelviertel, 36/44 danach). Sprites 0 = noch keine Grafik
   zugewiesen -> das Spiel nimmt die Rueckfallgrafik, siehe WCRAWL_SPR_EFF. */
#define LVL_WCRAWL_COUNT 4
static const u16 lvl_wcrawl_row[4]  = { 14u, 24u, 36u, 44u };
static const u8  lvl_wcrawl_col[4]  = { 18u,  3u, 18u,  3u };
static const u8  lvl_wcrawl_side[4] = {  1u,  0u,  1u,  0u };
static const u8  lvl_wcrawl_amp[4]  = {  6u,  4u,  6u,  5u };
#define LVL_WCRAWL_SPR        0u
#define LVL_WCRAWL_SPR_SPENT  0u
#define LVL_WCRAWL_BULLET_SPR 0u
#define LVL_WCRAWL_MIRROR     1
#define LVL_WCRAWL_SHIP_DMG   1
#define LVL_WCRAWL_HEALTH     2
#define LVL_WCRAWL_POINTS     250
#define LVL_WCRAWL_STEP       5
#define LVL_WCRAWL_CHANCE     77
#define LVL_WCRAWL_WIN_UP     15
#define LVL_WCRAWL_WIN_DOWN   5
#define LVL_WCRAWL_SHOTS      3
static const s16 lvl_wcrawl_shot_dx[3] = {  30, 43, 30 };
static const s16 lvl_wcrawl_shot_dy[3] = { -30,  0, 30 };
#endif

/* Rueckfallgrafik, solange im Tool kein Sprite zugewiesen ist: das gemeinsame
   Gegner-Projektil. Sichtbar falsch, aber sichtbar ??? genau wie wp_pet_draw()
   es fuer die Sonderwaffen macht, bis das Tool eigene Sprites liefert. */
#define WCRAWL_SPR_EFF       ((u16)(LVL_WCRAWL_SPR ? (u16)LVL_WCRAWL_SPR : (u16)LVL_ENEMY_BULLET_SPR))
#define WCRAWL_SPENT_SPR_EFF ((u16)(LVL_WCRAWL_SPR_SPENT ? (u16)LVL_WCRAWL_SPR_SPENT : WCRAWL_SPR_EFF))
#define WCRAWL_CELLS   4u    /* Metasprite-Obergrenze, wie WPX_CELLS bei den Waffenmodulen */
#define WCRAWL_RING_NONE 0xFFu
/* 30.07.2026 EINBLENDSCHWELLE (Nutzerentscheidung, siehe CLAUDE.md).
   Fehlerbild: "die Plobs kleben manchmal ewig lang am oberen Bildschirmrand,
   bis sie irgendwann ins Spielfeld scrollen" - dort sind sie nicht abschiessbar
   und belegen trotzdem OAM-Slots.
   Ursache ist KEIN Portfehler, sondern eine Zahlengleichheit, die das Original
   genauso hat (nachgerechnet gegen src/game/turrets.ts updatePod, 1000:6ed1/
   70e5): Scroll 1 px / 3 Frames = 0,333 px/Frame gegen Kriechtempo
   LVL_WCRAWL_STEP 5/16 = 0,3125 px/Frame NACH OBEN. In der Aufwaertsphase hebt
   sich beides fast exakt auf (netto 0,02 px/Frame). Da wcrawl_row_in() den
   Kriecher genau dann am Anker startet, wenn seine Map-Zeile OBEN einrollt,
   steht er die ganze Aufwaertsphase (amp bis 10 Zellen = 80 px, 256 Frames
   ~ 8,5 s) praktisch still am oberen Rand.
   Nutzerentscheidung: in dieser Phase AUSBLENDEN statt kleben lassen. Der
   Kriecher kriecht weiter (die Phase laeuft also normal ab), wird aber erst
   gezeichnet und erst abschiessbar, sobald er das erste Mal WCRAWL_SHOW_Y
   erreicht - das passiert automatisch, wenn die Richtung kippt und er mit
   0,645 px/Frame nach unten laeuft. Er "scrollt" damit dicht unter dem oberen
   Rand herein statt mitten im Bild aufzutauchen. Bis dahin haelt er KEINEN
   OAM-Slot (wcrawls_draw gibt sie bei on==0 zurueck).
   Einmal eingeblendet bleibt er es (Riegel w->shown), sonst wuerde er bei jeder
   weiteren Aufwaertsphase wieder verschwinden und wieder auftauchen. */
#define WCRAWL_SHOW_Y  16    /* px, = eine Figurenhoehe unter dem oberen Rand */

typedef struct {
    u8  alive;        /* 0 = abgeschossen (bleibt es fuer den Rest des Levels) */
    u8  spent;        /* 1 = geplatzt: feuert nicht mehr, kriecht nicht mehr */
    u8  ring;         /* Ringzeile der Ankerzeile, WCRAWL_RING_NONE = nicht im Bild */
    u8  health;
    u8  flash;
    s16 off_fix;      /* Weg vom Anker nach OBEN, 12.4-Fixed (0 .. amp*8*16) */
    s8  dir;          /* +1 = hoch, -1 = zurueck zum Anker */
    u8  x, y;         /* zuletzt berechnete Bildschirmposition (Kollision/Zeichnen) */
    u8  on;           /* 1 = x/y gueltig und im Bild */
    u8  shown;        /* Riegel: hat WCRAWL_SHOW_Y schon einmal erreicht */
    u8  anim_tick, anim_frame;
    u8  cells;        /* zuletzt gezeichnete Zellenzahl */
    u8  oam[WCRAWL_CELLS][2];
    u16 last_snum[WCRAWL_CELLS];
    s16 hz_dx, hz_dy;
    u8  hz_w, hz_h;
} TWallCrawler;
static TWallCrawler g_wcrawlers[LVL_WCRAWL_COUNT];

/* Schuss mit VORGEGEBENEM Richtungsvektor (die drei Faecher-Kugeln). enemy_fire()
   kann nur "eine von 8 Zufallsrichtungen" oder "auf das Schiff gezielt". */
static void wcrawl_fire_vec(u8 ex, u8 ey, s16 vx, s16 vy) {
    u8 b;
    for (b = 0u; b < (u8)MAX_ENEMY_BULLETS; b++) {
        if (g_ebullets[b].active) continue;
        g_ebullets[b].active = 1u;
        g_busy_ebullets = 1u;   /* Frueh-Ausstieg: Flag an der Erzeugungsstelle */
        g_ebullets[b].x_fix = (s16)((s16)ex << 4);
        g_ebullets[b].y_fix = (s16)((s16)ey << 4);
        g_ebullets[b].vx = vx; g_ebullets[b].vy = vy;
        g_ebullets[b].oam = OAM_NONE;
        g_ebullets[b].spr = (u16)LVL_WCRAWL_BULLET_SPR;   /* 0 = gemeinsames Gegner-Projektil */
        return;
    }
    /* Pool voll -> diese Kugel faellt aus, kein Nachrutschen (wie enemy_fire) */
}

static void wcrawl_free_oam(u8 i) {
    TWallCrawler *w = &g_wcrawlers[i];
    u8 c;
    for (c = 0u; c < (u8)WCRAWL_CELLS; c++) {
        if (w->oam[c][0] != OAM_NONE) { UnsetSprite(w->oam[c][0]); oam_pool_free(w->oam[c][0]); w->oam[c][0] = OAM_NONE; }
        if (w->oam[c][1] != OAM_NONE) { UnsetSprite(w->oam[c][1]); oam_pool_free(w->oam[c][1]); w->oam[c][1] = OAM_NONE; }
        w->last_snum[c] = 0u;
    }
    w->cells = 0u;
    w->on = 0u;
}

/* Aus lvl1_put_row(): Ringzeile ring_row nimmt jetzt Map-Zeile map_row auf.
   1. Kriecher, die BISHER auf dieser Ringzeile sassen, sind damit aus dem Bild
      gerollt -> Slots zurueck, Position ungueltig (aber alive/spent bleiben,
      der Zustand gehoert zum Level, nicht zum Bildausschnitt).
   2. Kriecher DIESER Map-Zeile bekommen die Ringzeile und starten am Anker. */
static void wcrawl_row_in(u16 map_row, u8 ring_row) {
    u8 i;
    for (i = 0u; i < (u8)LVL_WCRAWL_COUNT; i++) {
        TWallCrawler *w = &g_wcrawlers[i];
        u8 mine = (u8)(lvl_wcrawl_row[i] == map_row);
        if (w->ring == ring_row) {
            /* WICHTIG (Messung 28.07.: Kriecher pendelte nur ueber 4 px statt 48):
               lvl1_put_row() schreibt dieselbe Zeile auch NEU, ohne dass sich am
               Bild etwas aendert - restore_terrain_row() tut das jedes Mal, wenn
               die HUD-Bar eine Ringzeile wieder freigibt. Steht dort weiterhin
               DIESELBE Map-Zeile, ist das kein Neuanfang: Laufweg und Richtung
               muessen stehenbleiben, sonst faengt der Kriecher staendig von vorn
               an und zittert am Anker. */
            if (mine) continue;
            w->ring = WCRAWL_RING_NONE;
            wcrawl_free_oam(i);
        }
        if (mine && w->alive) {
            w->ring    = ring_row;
            w->off_fix = 0;
            w->dir     = 1;
            w->shown   = 0u;   /* Einblendriegel neu setzen, siehe WCRAWL_SHOW_Y */
            w->anim_tick = 0u; w->anim_frame = 0u;
        }
    }
}

/* Trefferzone aus der GERADE GUELTIGEN Form ableiten (28.07.2026).
   Nutzerbericht "die explodierten Plobs bleiben statisch stehen und werden nicht
   gekillt": die Zone wurde bisher EINMAL beim Start aus der lebenden Form
   gerechnet - 8x16, eine Zelle breit. Die geplatzte Huelle ist aber 2x2 Zellen
   (16x16). Damit deckte die Zone nur die halbe Huelle ab: Schuesse, die sichtbar
   trafen, gingen daneben. Deshalb wird sie beim Platzen neu bestimmt.
   Die Fallback-Groesse kommt aus der Metasprite-Ausdehnung, nicht pauschal 8x8. */
static void wcrawl_hitzone(TWallCrawler *w, u16 sp, u8 right) {
    u8 fw = 8u, fh = 8u, mxc = 0u;
    if (sp & 0x1000u) {
        u16 mi = (u16)(sp & 0x01FFu);
        if (sp & 0x4000u) {
            u8 hh;
            for (hh = 0u; hh < (u8)LVL_METAANIM_COUNT; hh++)
                if (lvl_metaanim_head[hh] == mi) { mi = lvl_metaanim_frames[hh][0]; break; }
        }
        if (mi < (u16)LVL_META_COUNT) {
            u16 off = lvl_meta_off[mi];
            u8  cnt = lvl_meta_count[mi], k, mx = 0u, my = 0u;
            for (k = 0u; k < cnt; k++) {
                if ((u8)lvl_meta_dx[off + k] > mx) mx = (u8)lvl_meta_dx[off + k];
                if ((u8)lvl_meta_dy[off + k] > my) my = (u8)lvl_meta_dy[off + k];
            }
            fw = (u8)(mx + 8u); fh = (u8)(my + 8u);
            mxc = mx;
        }
    }
    hitzone_resolve(sp, fw, fh, &w->hz_dx, &w->hz_dy, &w->hz_w, &w->hz_h);
    /* 30.07.2026: an der rechten Wand ist die Figur RECHTSKANTIG (siehe die
       Rechtskant-Ausrichtung in wcrawls_draw) - die Trefferzone muss mit,
       sonst liegt sie neben der geplatzten Huelle. */
    if (right) w->hz_dx = (s16)(w->hz_dx - (s16)mxc);
}

static void wcrawls_init(void) {
    u8 i, c;
    for (i = 0u; i < (u8)LVL_WCRAWL_COUNT; i++) {
        TWallCrawler *w = &g_wcrawlers[i];
        w->alive = 1u; w->spent = 0u; w->ring = WCRAWL_RING_NONE;
        w->health = (u8)LVL_WCRAWL_HEALTH; w->flash = 0u;
        w->off_fix = 0; w->dir = 1;
        w->x = 0u; w->y = 0u; w->on = 0u; w->shown = 0u;
        w->anim_tick = 0u; w->anim_frame = 0u; w->cells = 0u;
        for (c = 0u; c < (u8)WCRAWL_CELLS; c++) {
            w->oam[c][0] = OAM_NONE; w->oam[c][1] = OAM_NONE; w->last_snum[c] = 0u;
        }
        /* Trefferzone einmal aufloesen (wie TEnemy.hz_*): sie haengt am Sprite,
           nicht an der Position. Rueckfall 8x8 = eine Kachel. */
        /* 28.07.2026: Rueckfallgroesse aus dem Metasprite ableiten statt pauschal
           8x8. Der Plob ist 8x16 (zwei Zellen uebereinander) - mit 8x8 lag die
           halbe Figur ausserhalb der Trefferzone, und die Ausloesehoehe fuers
           Platzen sass 4 px zu hoch. */
        wcrawl_hitzone(w, WCRAWL_SPR_EFF, lvl_wcrawl_side[i]);
    }
    g_busy_wcrawl = 1u;   /* siehe oam_reset_all: nach hartem Reset einmal durchlaufen lassen */
}

static void wcrawl_hit(u8 i, u8 dmg) {
    TWallCrawler *w = &g_wcrawlers[i];
    if (w->health > dmg) {
        w->health = (u8)(w->health - dmg);
        w->flash  = (u8)HIT_FLASH_FRAMES;
        return;
    }
    w->alive = 0u;
    g_score += (u16)LVL_WCRAWL_POINTS;
    /* Slots SOFORT beim Tod zurueck (Fehlerklasse Geister-Sprite, 27.07.2026 ???
       siehe enemy_take_damage): der Zeichendurchlauf allein reicht nicht. */
    wcrawl_free_oam(i);
    w->ring = WCRAWL_RING_NONE;
}

/* Platzen: den Faecher aus lvl_wcrawl_shot_dx/dy abfeuern (rechte Wand mit
   negiertem dx), danach ist der Kriecher nur noch Huelle. */
static void wcrawl_burst(u8 i, u8 cx, u8 cy) {
    TWallCrawler *w = &g_wcrawlers[i];
    u8 k;
    u8 right = lvl_wcrawl_side[i];
    w->spent = 1u;
    w->anim_tick = 0u; w->anim_frame = 0u;
    wcrawl_hitzone(w, WCRAWL_SPENT_SPR_EFF, right);   /* Huelle ist groesser als die lebende Form */
    for (k = 0u; k < (u8)LVL_WCRAWL_SHOTS; k++) {
        s16 vx = lvl_wcrawl_shot_dx[k];
        if (right) vx = (s16)(-vx);
        wcrawl_fire_vec(cx, cy, vx, lvl_wcrawl_shot_dy[k]);
    }
}

static void wcrawls_update(void) {
    u8 i;
    s16 srx, sry;
    u8  srw, srh;
    s16 ship_cy;
    u8  busy = 0u;
    ship_hitzone_rect(&srx, &sry, &srw, &srh);
    ship_cy = (s16)(sry + (s16)(srh >> 1));
    for (i = 0u; i < (u8)LVL_WCRAWL_COUNT; i++) {
        TWallCrawler *w = &g_wcrawlers[i];
        s16 anchor_y, sy;
        u8  amp_fix_hi;
        u8  frisch = 0u;   /* 1 = wird GERADE eingeblendet, siehe WCRAWL_SHOW_Y */
        if (!w->alive || w->ring == WCRAWL_RING_NONE) continue;
        busy = 1u;
        /* Ankerhoehe wie das Terrain seine Ringzeile zeichnet (u8-Wrap!), dann
           auf s16 aufziehen - >=208 heisst "ueber dem oberen Rand". */
        { u8 raw = (u8)((u8)(w->ring << 3) - g_scr1_y);
          anchor_y = (raw < 208u) ? (s16)raw : (s16)((s16)raw - 256); }
        if (!w->spent) {
            amp_fix_hi = lvl_wcrawl_amp[i];
            w->off_fix = (s16)(w->off_fix + (s16)((s16)w->dir * (s16)LVL_WCRAWL_STEP));
            if (w->off_fix >= (s16)((s16)amp_fix_hi << 7)) {        /* amp Zellen * 8 px * 16 */
                w->off_fix = (s16)((s16)amp_fix_hi << 7); w->dir = -1;
            } else if (w->off_fix <= 0) {
                w->off_fix = 0; w->dir = 1;
            }
        }
        sy = (s16)(anchor_y - (s16)(w->off_fix >> 4));
        /* 30.07.2026: Untergrenze war -8. Fuer sy in -8..-1 lieferte (u8)sy aber
           248..255 - die Figur wurde also unten aus dem Bild gezeichnet und ihre
           Trefferzone lag bei y=248 (nicht treffbar). Jetzt bei 0 abschneiden:
           optisch geht nichts verloren (bei 248 war ohnehin nichts zu sehen),
           der u8-Wrap in w->y ist damit ausgeschlossen. */
        if (sy < 0 || sy >= (s16)BAR_Y) { w->on = 0u; continue; }
        /* Einblendschwelle, siehe WCRAWL_SHOW_Y: bis zum ersten Erreichen weder
           zeichnen (wcrawls_draw gibt die OAM-Slots bei on==0 zurueck) noch
           treffbar noch platzend. Das Kriechen oben laeuft normal weiter. */
        if (!w->shown) {
            if (sy < (s16)WCRAWL_SHOW_Y) { w->on = 0u; continue; }
            w->shown = 1u;
            frisch = 1u;
        }
        w->x  = (u8)((u8)lvl_wcrawl_col[i] << 3);
        w->y  = (u8)sy;
        w->on = 1u;
        /* Ausloesen: Schiffsmitte im Fenster um die Kriechermitte + Wuerfel.
           Original 1000:6e50..6e66 (dy <= 10 && dy >= -30, rand < 0x32). */
        /* 30.07.2026: NICHT im Einblendframe platzen lassen. Die Ausloesepruefung
           sitzt direkt hinter der Sichtbarkeit - ein Kriecher konnte deshalb im
           selben Frame platzen, in dem er zum ersten Mal gezeichnet wird. Der
           Spieler sah dann nie den lebenden Plob, sondern nur eine Explosion aus
           dem Nichts (im Platz-Streifen befund5 gut zu sehen: kein einziger
           lebendiger Frame davor). Ein Frame Aufschub genuegt. */
        if (!w->spent && !frisch) {
            s16 cy = (s16)((s16)w->y + w->hz_dy + (s16)(w->hz_h >> 1));
            s16 d  = (s16)(cy - ship_cy);
            if (d >= (s16)(-(s16)LVL_WCRAWL_WIN_UP) && d <= (s16)LVL_WCRAWL_WIN_DOWN &&
                (u8)(QRandom() & 0xFFu) < (u8)LVL_WCRAWL_CHANCE) {
                u8 mx = lvl_wcrawl_side[i] ? (u8)(w->x - 2u) : (u8)(w->x + 8u);
                wcrawl_burst(i, mx, (u8)cy);
            }
        }
    }
    if (busy) g_busy_wcrawl = 1u;
}

/* Bildposition in der laufenden Folge. LEBENDIG laeuft sie endlos im Kreis,
   GEPLATZT genau EINMAL und bleibt dann auf dem letzten Bild stehen - so macht
   es auch das Original (die Alive-Folge CS:0x64c1 hat loop 0, die geplatzte
   CS:0x64d7 loop 2 = kein Rueckstart). Ohne die Unterscheidung wuerde die
   Platz-Animation endlos weiterploppen, obwohl der Kriecher schon leer ist. */
static u8 wcrawl_frame_pos(const TWallCrawler *w, u8 len) {
    if (len == 0u) return 0u;
    if (w->spent) return (u8)((w->anim_frame >= len) ? (u8)(len - 1u) : w->anim_frame);
    return (u8)(w->anim_frame % len);
}

/* Zeichnen. Loest die Sprite-Referenz genauso auf wie die Waffenmodule
   (Standbild / animierter Streifen / Metasprite / Metaanim, siehe g_wpx_*) und
   spiegelt die rechte Wand per H-Flip, wenn LVL_WCRAWL_MIRROR gesetzt ist. */
static void wcrawls_draw(void) {
    u8 i, still = 0u;
    if (!g_busy_wcrawl) return;   /* Frueh-Ausstieg, siehe g_busy_* */
    for (i = 0u; i < (u8)LVL_WCRAWL_COUNT; i++) {
        TWallCrawler *w = &g_wcrawlers[i];
        u16 spr, n, s_num[WCRAWL_CELLS];
        s8  c_dx[WCRAWL_CELLS], c_dy[WCRAWL_CELLS];
        u8  cnt = 0u, c, flip;
        u8  xoff = 0u;   /* Rechtskant-Ausgleich, siehe unten */
        u8  c_fl[WCRAWL_CELLS];
        u8  speed = 1u, len = 0u;   /* Bildtempo/-zahl der aktiven Folge, siehe unten */
        if (!w->alive || !w->on) { if (w->cells) wcrawl_free_oam(i); continue; }
        still = 1u;
        spr = w->spent ? WCRAWL_SPENT_SPR_EFF : WCRAWL_SPR_EFF;
        n   = (u16)(spr & 0x01FFu);
        if (spr & 0x1000u) {                      /* Metasprite bzw. Metaanim */
            u16 mi = n;
            if (spr & 0x4000u) {
                u8 h;
                for (h = 0u; h < (u8)LVL_METAANIM_COUNT; h++)
                    if (lvl_metaanim_head[h] == n) break;
                if (h < (u8)LVL_METAANIM_COUNT) {
                    speed = lvl_metaanim_speed[h];
                    len   = lvl_metaanim_len[h];
                    mi    = lvl_metaanim_frames[h][wcrawl_frame_pos(w, len)];
                }
            }
            if (mi < (u16)LVL_META_COUNT) {
                u16 off = lvl_meta_off[mi];
                cnt = lvl_meta_count[mi];
                if (cnt > (u8)WCRAWL_CELLS) cnt = (u8)WCRAWL_CELLS;
                for (c = 0u; c < cnt; c++) {
                    s_num[c] = lvl_meta_num[off + c];
                    c_dx[c]  = lvl_meta_dx[off + c];
                    c_dy[c]  = lvl_meta_dy[off + c];
                    c_fl[c]  = META_FLIP_OF(off + c);
                }
            }
        } else if (spr & 0x4000u) {               /* animierter Einzelstreifen */
            u8 ai = sspr_anim_find(n);
            if (ai != 0xFFu) {
                speed = lvl_sspr_anim_speed[ai];
                len   = lvl_sspr_anim_len[ai];
                s_num[0] = lvl_sspr_anim_frames[ai][wcrawl_frame_pos(w, len)];
            } else {
                s_num[0] = n;
            }
            c_dx[0] = 0; c_dy[0] = 0; c_fl[0] = 0u; cnt = 1u;
        } else {                                  /* Standbild */
            s_num[0] = n; c_dx[0] = 0; c_dy[0] = 0; c_fl[0] = 0u; cnt = 1u;
        }
        /* Animationstakt: eigener Zaehler je Instanz (mehrere Kriecher duerfen
           phasenversetzt laufen), Tempo aus dem Streifen/Metaanim selbst - NICHT
           fest verdrahtet, sonst ignoriert der Kriecher als einziges System die
           im Tool eingestellte Bildrate. Bei len==0 (kein Anim gefunden) bleibt
           speed 1 und der Zaehler laeuft ins Leere, was nichts kostet. */
        w->anim_tick++;
        if (w->anim_tick >= speed) {
            w->anim_tick = 0u;
            /* Die GEPLATZTE Folge laeuft EINMAL durch - danach ist der Kriecher
               WEG (Nutzerwunsch 29.07.2026: "die Dinger zerplatzen, laufen die
               drei Bilder durch und weg ist alles").
               BEWUSSTE ABWEICHUNG VOM ORIGINAL: dort bleibt die leere Huelle als
               Kulisse haengen und gibt ihre 250 Punkte erst, wenn man sie
               abschiesst (CS:0x64d7 loop-Feld 2 = kein Rueckstart, HP 2). Hier
               verschwindet sie von selbst und gibt keine Punkte - wer sie noch
               waehrend der Platz-Animation trifft, bekommt sie ueber
               wcrawl_hit() weiterhin. */
            if (w->spent && len && w->anim_frame >= (u8)(len - 1u)) {
                w->alive = 0u;
                wcrawl_free_oam(i);
                w->ring = WCRAWL_RING_NONE;
                continue;                  /* Slots sind zurueck, nichts mehr zu zeichnen */
            }
            w->anim_frame++;
        }

        /* nicht mehr gebrauchte Zellen abgeben (Bildwechsel auf weniger Zellen) */
        for (c = cnt; c < w->cells; c++) {
            if (w->oam[c][0] != OAM_NONE) { UnsetSprite(w->oam[c][0]); oam_pool_free(w->oam[c][0]); w->oam[c][0] = OAM_NONE; }
            if (w->oam[c][1] != OAM_NONE) { UnsetSprite(w->oam[c][1]); oam_pool_free(w->oam[c][1]); w->oam[c][1] = OAM_NONE; }
            w->last_snum[c] = 0u;
        }
        w->cells = cnt;
        /* 28.07.2026 (Nutzerbericht "alle Plobs sind falschrum - sie zeigen mit
           der OBERSEITE zur Wand statt mit der Unterseite"): die gezeichnete
           Grafik ist gegenueber der Annahme seitenverkehrt. Bisher wurde nur die
           gespiegelte Wand geflippt, jetzt zusaetzlich die Grundlage - also
           genau umgekehrt. Als XOR geschrieben, damit LVL_WCRAWL_MIRROR weiter
           steuert, welche Wand die gespiegelte ist. */
        flip = (u8)((((LVL_WCRAWL_MIRROR && lvl_wcrawl_side[i]) ? 1u : 0u) ^ 1u)
                    ? SPR_HFLIP : 0u);
        /* 28.07.2026 (Nutzerbericht "die Plobs sehen falsch aus"): bei gespiegelter
           Wand muss auch die ZELLENANORDNUNG kippen, nicht nur der Inhalt jeder
           Zelle. Die lebende Form ist 1 Zelle breit (dx immer 0) - da faellt es
           nicht auf; die GEPLATZTE ist 2x2 (dx 0 und 8), und ohne Spaltentausch
           landet die gespiegelte linke Haelfte links statt rechts. */
        { u8 k, mx = 0u;
          for (k = 0u; k < cnt; k++) if ((u8)c_dx[k] > mx) mx = (u8)c_dx[k];
          if (flip) for (k = 0u; k < cnt; k++) c_dx[k] = (s8)(mx - (u8)c_dx[k]);
          /* 30.07.2026 (Nutzer: "die Explosion des Plobs sitzt vermutlich 8 px
             zu weit rechts") - nachgemessen: lebendig x 144..152, geplatzt
             x 144..160. Die lebende Form ist EINE Zelle breit, die geplatzte
             ZWEI, und beide wurden linkskantig an w->x gesetzt. Die Huelle
             wuchs damit immer nach RECHTS: an der linken Wand in den Gang
             (richtig, dort ist Platz), an der rechten Wand in den FELS.
             Regel jetzt: an der rechten Wand rechtskantig ausrichten, die
             Huelle waechst also von der Wand weg in den Gang - fuer die
             lebende Form (mx = 0) aendert das nichts. */
          xoff = 0u;
          if (lvl_wcrawl_side[i]) xoff = mx; }
        /* ===== OAM: ALLES ODER NICHTS (28.07.2026) =====
           Nutzerbericht "die Plobs sehen falsch aus". Gemessen: in der Kriecher-
           Zone sind bis zu ACHT gleichzeitig im Bild und der freie Pool faellt auf
           NULL (48 Slots, geteilt mit Schuessen und Gegnern). Bisher holte sich
           JEDE ZELLE einzeln ihre Slots - wer nur einen Teil bekam, wurde halb
           gezeichnet, und genau das sieht kaputt aus.
           Jetzt wird erst der Gesamtbedarf bestimmt und nur belegt, wenn er ganz
           gedeckt ist: im Engpass fehlt lieber ein ganzer Kriecher, als dass
           mehrere zerrupft dastehen. Zusaetzlich holt eine Zelle OHNE b-Overlay
           keinen zweiten Slot mehr - die geplatzte Form braucht damit 4 bis 6
           statt immer 8. */
        { u8 brauche = 0u, habe = 0u;
          for (c = 0u; c < cnt; c++) {
              u16 bx = lvl_sspr_b_idx[s_num[c] - 1u];
              u8 needb = (u8)(bx != 0u && bx != 0xFFFFu);
              if (w->oam[c][0] == OAM_NONE) brauche++; else habe++;
              if (needb && w->oam[c][1] == OAM_NONE) brauche++;
              if (!needb && w->oam[c][1] != OAM_NONE) {   /* b nicht mehr noetig -> zurueck */
                  UnsetSprite(w->oam[c][1]); oam_pool_free(w->oam[c][1]); w->oam[c][1] = OAM_NONE;
              }
          }
          if (brauche > oam_pool_free_count() && habe < cnt) continue;
        }
        for (c = 0u; c < cnt; c++) {
            u8 px = (u8)((u8)(w->x + c_dx[c]) - xoff);   /* xoff: siehe Rechtskant-Block */
            u8 py = (u8)(w->y + c_dy[c]);
            u16 bx = lvl_sspr_b_idx[s_num[c] - 1u];
            u8 needb = (u8)(bx != 0u && bx != 0xFFFFu);
            if (w->oam[c][0] == OAM_NONE) {
                w->oam[c][0] = oam_pool_alloc();
                if (w->oam[c][0] == OAM_NONE) continue;            /* Pool leer */
                w->last_snum[c] = 0u;
            }
            if (needb && w->oam[c][1] == OAM_NONE) {
                w->oam[c][1] = oam_pool_alloc();
                w->last_snum[c] = 0u;
            }
            if (s_num[c] == w->last_snum[c]) {    /* gleiches Bild -> nur Position */
                SetSpritePosition(w->oam[c][0], px, py);
                if (w->oam[c][1] != OAM_NONE) SetSpritePosition(w->oam[c][1], px, py);
            } else {
                if (w->oam[c][1] != OAM_NONE)
                    spr_draw_s2(w->oam[c][0], w->oam[c][1], s_num[c], px, py);
                else
                    spr_draw_s_single(w->oam[c][0], s_num[c], px, py);
                /* 28.07.2026: Wandspiegelung UND Zellen-Spiegelung zusammen -
                   zweimal h-gespiegelt hebt sich auf, deshalb XOR statt ODER. */
                { u8 fm = (u8)(flip ^ META_FLIP_MASK(c_fl[c]));
                  if (fm) {
                      SpriteControl(w->oam[c][0], SPR_FRONT, fm);
                      if (w->oam[c][1] != OAM_NONE) SpriteControl(w->oam[c][1], SPR_FRONT, fm);
                  } }
                w->last_snum[c] = s_num[c];
            }
        }
        /* Treffer-Flash ueber die OAM-Palettenzuordnung, wie bei den Gegnern
           (ueberlebt den Positions-Schnellpfad, trifft nur dieses Sprite). */
        if (w->flash) {
            u8 *sc = SPRITE_COLOUR;   /* cc900: kein Subscript direkt auf den Cast (siehe library.c) */
            for (c = 0u; c < cnt; c++) {
                if (w->oam[c][0] != OAM_NONE) sc[w->oam[c][0]] = (u8)FLASH_PAL;
                if (w->oam[c][1] != OAM_NONE) sc[w->oam[c][1]] = (u8)FLASH_PAL;
            }
            w->flash--;
            if (w->flash == 0u) for (c = 0u; c < cnt; c++) w->last_snum[c] = 0u;
        }
    }
    g_busy_wcrawl = still;
}

/* Kollisionen: Spielerschuesse gegen die Kriecher und (optional) Schiffskontakt.
   Wird aus check_collisions() aufgerufen, damit die Schiffs-Hitzone dort nur
   einmal aufgeloest werden muss. */
static void wcrawls_collide(s16 srx, s16 sry, u8 srw, u8 srh, u8 ship_vuln) {
    u8 i, b;
    for (i = 0u; i < (u8)LVL_WCRAWL_COUNT; i++) {
        TWallCrawler *w = &g_wcrawlers[i];
        if (!w->alive || !w->on) continue;
        for (b = 0u; b < (u8)MAX_BULLETS; b++) {
            if (!g_bullets[b].active) continue;
            if (rects_overlap_cached((s16)g_bullets[b].x, (s16)g_bullets[b].y,
                                      BULLET_HIT_W, BULLET_HIT_H + BULLET_SWEEP,   /* Strecken-Pruefung, siehe BULLET_SWEEP */
                                      w->x, w->y, w->hz_dx, w->hz_dy, w->hz_w, w->hz_h)) {
                g_bullets[b].active = 0u;
                wcrawl_hit(i, main_gun_damage());
                break;
            }
        }
#if LVL_WCRAWL_SHIP_DMG
        if (!w->alive || !ship_vuln) continue;
        if (rects_overlap_cached(srx, sry, srw, srh,
                                  w->x, w->y, w->hz_dx, w->hz_dy, w->hz_w, w->hz_h)) {
            g_dmg_src = 8u; player_damage((u8)DMG_ENEMY_CONTACT);   /* 8 = Wandkriecher (siehe TELEMETRY t[20]) */
        }
#else
        (void)srx; (void)sry; (void)srw; (void)srh; (void)ship_vuln;
#endif
    }
}

/* SMART BOMB (DOS-Pickup 18, Handler 1000:2ad0/2add): loescht ALLE Gegner auf dem
   Schirm (mit Punkten wie ein normaler Abschuss), raeumt Gegner- und Objekt-
   Schuesse weg und leert die Wandwuermer. Der Endboss bleibt (Spezialentity). */
static void smart_bomb_detonate(void) {
    u8 j, w, k;
    for (j = 0u; j < (u8)MAX_ENEMIES; j++)
        if (g_enemies[j].active) {
            g_score += (u16)lvl_spawn_points[g_enemies[j].spawn_idx];
            g_enemies[j].active = 0u;
        }
    for (j = 0u; j < (u8)MAX_METAENEMIES; j++)
        if (g_metaenemies[j].active) {
            g_score += (u16)lvl_spawn_points[g_metaenemies[j].spawn_idx];
            g_metaenemies[j].active = 0u;
        }
    for (w = 0u; w < (u8)MAX_WORMS; w++)
        if (g_worms[w].active) {
            for (k = 0u; k < g_worms[w].num_segs; k++) g_worms[w].seg[k].alive = 0u;
            g_worms[w].active = 0u;
        }
    for (j = 0u; j < (u8)MAX_ENEMY_BULLETS; j++)   g_ebullets[j].active = 0u;
    for (j = 0u; j < (u8)MAX_MAPOBJ_BULLETS; j++)  g_mo_bullets[j].active = 0u;
}

/* SUPER NASHWAN POWER (DOS 1000:2b22 / Ablauf 472d, upgrades.md §2 / weapons.md §4):
   fuer begrenzte Zeit ein volles, maximal aufgepowertes Waffen-Loadout; bei Ablauf
   faellt exakt das vorherige Loadout zurueck. Timer 170 Ticks -> 280 Frames
   (x1,648, s0.4d). Waehrend Nashwan laeuft, wird der Checkpoint-Snapshot
   uebersprungen (DOS 1000:35eb) - ein Tod restauriert also das VOR-Nashwan-Loadout. */
#define NASHWAN_FRAMES 280u
/* Kit = die maechtigen Waffen, die wir haben (DOS: double, 2x cannon, 2x laser,
   drone, side shot): Side(1)+Cannon(2)+Laser(3)+Launcher(4)+Drone(7). */
#define NASHWAN_KIT ((u16)((1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<7)))
static void nashwan_activate(void) {
    if (g_nashwan_timer == 0u) {   /* Loadout nur beim START sichern, nicht bei Nachtrigger */
        g_nash_weapons  = g_player.weapons_active;
        g_nash_power    = g_player.power_stage;
        g_nash_firerate = g_player.firerate_stage;
    }
    g_player.weapons_active = (u16)NASHWAN_KIT;
    g_player.power_stage    = (u8)(LVL_POWER_STAGE_COUNT - 1u);
    g_player.firerate_stage = (u8)(LVL_FIRERATE_STAGE_COUNT - 1u);
    g_nashwan_timer = (u16)NASHWAN_FRAMES;
}
static void nashwan_update(void) {
    if (g_nashwan_timer == 0u) return;
    g_nashwan_timer--;
    if (g_nashwan_timer == 0u) {   /* Ablauf: vorheriges Loadout zurueck */
        g_player.weapons_active = g_nash_weapons;
        g_player.power_stage    = g_nash_power;
        g_player.firerate_stage = g_nash_firerate;
    }
}

/* Pickup-Wirkung (Schritt 15). kind: 0=Geld (value bereits fertiger
   Geldbetrag), 1=Feuerrate-Stufe, 2=Power-Stufe (siehe Hinweis bei TPlayer:
   steuert den S153/154-Schussflacker, NICHT lvl_power_stage_bullet_spr),
   3=Waffe (value=Bitindex in LVL_WEAPON_COUNT), 4=Speedup.
   23.07.2026: 5=Energie (+value, gedeckelt auf PLAYER_MAX_ENERGY - DOS-Pickup
   2/3 = +20/+40 von 39), 6=Smart Bomb (loescht alle Gegner, DOS-Pickup 18).
   Damit die beiden im Spiel erscheinen, muss das Tool einem Belohnungs-/Pickup
   den kind 5 bzw. 6 zuweisen (wie bei den Waffen-Sprites - reine Tool-Eintragung,
   der Code ist bereit). */
static void apply_pickup(u8 kind, u16 value) {
    switch (kind) {
    case 0:
        /* 20.07.2026: kind 0 heisst laut Spec "Geld" und zahlt jetzt auch Geld
           statt Punkte (vorher g_score += value). Punkte kommen weiterhin aus
           lvl_spawn_points beim Abschuss (siehe dort) ??? die beiden Waehrungen
           sind ab hier getrennt, wie im Original. */
        g_cash += (u32)value;
        break;
    case 1:
        g_player.firerate_stage = (u8)(g_player.firerate_stage + value);
        if (g_player.firerate_stage > (u8)(LVL_FIRERATE_STAGE_COUNT - 1u))
            g_player.firerate_stage = (u8)(LVL_FIRERATE_STAGE_COUNT - 1u);
        break;
    case 2:
        g_player.power_stage = (u8)(g_player.power_stage + value);
        if (g_player.power_stage > (u8)(LVL_POWER_STAGE_COUNT - 1u))
            g_player.power_stage = (u8)(LVL_POWER_STAGE_COUNT - 1u);
        break;
    case 3:
        g_player.weapons_active |= (u16)((u16)1u << value);
        break;
    case 4:  /* Speedup (DOS-Pickup-Typ 0, "S"): Schiffstempo +value Stufen, gedeckelt */
        g_player.speed_stage = (u8)(g_player.speed_stage + value);
        if (g_player.speed_stage > (u8)PLAYER_SPEED_STAGE_MAX)
            g_player.speed_stage = (u8)PLAYER_SPEED_STAGE_MAX;
        g_tune_speed = (u8)(PLAYER_SPEED_STEP + (u8)(g_player.speed_stage * PLAYER_SPEED_STAGE_STEP));
        break;
    case 5:  /* Energie auffuellen (DOS +20/+40 von 39), auf Maximum gedeckelt */
        if ((u16)g_player.energy + value >= (u16)PLAYER_MAX_ENERGY)
            g_player.energy = (u8)PLAYER_MAX_ENERGY;
        else
            g_player.energy = (u8)(g_player.energy + (u8)value);
        bar_set_energy();
        break;
    case 6:  /* Smart Bomb: alle Gegner loeschen */
        smart_bomb_detonate();
        break;
    case 7:  /* Super Nashwan Power: volles Loadout auf Zeit */
        nashwan_activate();
        break;
    default:
        break;
    }
}

/* Pickup-Bewegung (Schritt 15, Nutzerkorrektur 10.07.2026): SCREEN-ABSOLUT,
   komplett unabhaengig vom Terrain-Scroll (vorher driftete das Pickup nur
   passiv mit scroll_update()/scroll_back_px() mit ??? folgte NICHT dem in
   map.h definierten Pfad). Phase 0 (Anflug) laeuft homing_frames Bilder,
   danach exaktes Einrasten auf den Anker (beseitigt Rundungsdrift aus der
   Ganzzahl-Division), dann Phase 1 auf lvl_pickup_path_data bis
   LVL_PICKUP_PATH_LEN. Kollision mit dem Schiff jeden Frame, in BEIDEN
   Phasen (wie in der ANWEISUNG gefordert). */
static void pickups_update(void) {
    u8 p;
    u8 any = 0u;   /* 23.07.2026: laeuft noch ein Token? -> g_boss_rain-Fenster */
    g_pickup_anim_tick++;
    for (p = 0u; p < (u8)MAX_PICKUPS; p++) {
        if (!g_pickups[p].active) continue;
        g_busy_pickups = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
        if (g_pickups[p].state == 0u) {
            g_pickups[p].x_fix = (s16)(g_pickups[p].x_fix + g_pickups[p].homing_dx);
            g_pickups[p].y_fix = (s16)(g_pickups[p].y_fix + g_pickups[p].homing_dy);
            g_pickups[p].homing_tick++;
            if (g_pickups[p].homing_tick >= g_pickups[p].homing_frames) {
                g_pickups[p].x_fix = (s16)((s16)PICKUP_SEEK_X << 4);
                g_pickups[p].y_fix = (s16)((s16)PICKUP_SEEK_Y << 4);
                g_pickups[p].path_frame = 0u;
                g_pickups[p].state = 1u;
            }
        } else if (g_pickups[p].state == 1u) {
            g_pickups[p].x_fix = (s16)(g_pickups[p].x_fix + pickup_circ_dx[g_pickups[p].dir]);
            g_pickups[p].y_fix = (s16)(g_pickups[p].y_fix + pickup_circ_dy[g_pickups[p].dir]);
            g_pickups[p].path_frame++;
            if ((g_pickups[p].path_frame % (u16)PICKUP_CIRCLE_ROT) == 0u)
                g_pickups[p].dir = (u8)((g_pickups[p].dir + 1u) & 7u);
            if (g_pickups[p].path_frame >= (u16)PICKUP_CIRCLE_TICKS)
                g_pickups[p].state = 2u;
        } else {
            g_pickups[p].y_fix = (s16)(g_pickups[p].y_fix + PICKUP_FALL_STEP);
        }
        g_pickups[p].x = (u8)(g_pickups[p].x_fix >> 4);
        g_pickups[p].y = (u8)(g_pickups[p].y_fix >> 4);
        if (g_pickups[p].y >= (u8)BAR_Y) { g_pickups[p].active = 0u; continue; }
        /* BUGFIX 21.07.2026 ("Hitzone zu klein, schwer einzusammeln"): collide()
           vergleicht ECKE gegen ECKE. Das Schiff ist 24x24, das Item 8x8 - die
           Fangzone lag dadurch rund 8 px nach oben links versetzt, man musste mit
           der linken oberen Schiffsecke treffen statt mit der Mitte. Jetzt Mitte
           gegen Mitte; echte Ueberlappung waere (24+8)/2 = 16, PICKUP_GRAB_R ist
           bewusst etwas grosszuegiger (in Shmups ueblich). */
        if (collide((u8)(g_pickups[p].x + 4u), (u8)(g_pickups[p].y + 4u),
                    (u8)(g_player.x + 12u),    (u8)(g_player.y + 12u),
                    (u8)PICKUP_GRAB_R)) {
            apply_pickup(g_pickups[p].kind, g_pickups[p].value);
            g_pickups[p].active = 0u;
        }
        if (g_pickups[p].active) any = 1u;
    }
    /* 23.07.2026: Cash-Regen-Fenster schliessen, sobald kein Token mehr faellt
       -> Unverwundbarkeit endet (Original: [0x9164] auf 0). */
    if (g_boss_rain && !any) g_boss_rain = 0u;
}

/* --- Waffen-Pickup (Schritt 15, Zusatzwaffe) ---
   Modul-Grafik animiert MIT b-Overlay (Bugfix 11.07.2026) ???
   siehe SPR_WPMODULE-Kommentar bei den OAM-Slots. */
typedef struct {
    u8 active; s16 x_fix, y_fix;
    u8 oam;   /* Nutzerkorrektur 14.07.2026: dynamisch aus oam_pool_*() statt SPR_WPBULLET_0 */
    u8 w;     /* 22.07.2026: welche Waffe hat ihn abgefeuert (Index in lvl_weapon_*) */
    s16 vx, vy;   /* 23.07.2026: eigene Geschwindigkeit (1/16 px/Frame). Standard =
                     lvl_weapon_bullet_dx/dy[w]; Homing lenkt sie, Flamer driftet. */
    u16 last_snum;  /* 27.07.2026: zuletzt geschriebenes Bild - animierte Projektile
                       (Anim-Bit in lvl_weapon_bullet_spr) muessen bei jedem
                       Bildwechsel neu geschrieben werden, nicht nur einmal. */
} TWpBullet;
static TWpBullet g_wp_bullets[MAX_WPBULLETS];

/* ===== Waffen-Sondermechanik (23.07.2026) =====
   Zuordnung Waffen-Index -> Verhalten, hergeleitet aus dem Shop-Katalog
   (map.h lvl_shop_ref/lvl_shop_price/lvl_shop_desc) gegen die DOS-Doku
   (upgrades.md/weapons.md, aus XENON2.EXE): Preis + Beschreibung pinnen den
   Namen. DIES IST DIE EINZIGE WAHRHEITSQUELLE - aendert sich der Tool-Katalog
   (andere Waffen-Indizes/Reihenfolge), MUSS diese Tabelle nachgezogen werden.
     0 REAR . 1 SIDE SHOT . 2/4 CANNON bzw. MISSILE LAUNCHER (Preis 4000, aus
     den Daten NICHT unterscheidbar - beide vorerst Standard) . 3 LASER .
     5 MINE . 6 ELECTRO BALL . 7 DRONE . 8 HOMING . 9 FLAMER . 10 BOMB.
   Umgesetzt: HOMING (8), FLAMER (9). PIERCE ist verdrahtet, aber noch keiner
   Waffe zugewiesen (Cannon-Identitaet offen). LASER/MINE/BOMB/ELECTRO/DRONE
   brauchen eigene Entity-Typen und bleiben vorerst Standard. */
#define WPB_STD    0u   /* geradliniger Schuss (bullet_dx/dy[w]) */
#define WPB_HOMING 1u   /* lenkt jeden Frame auf den naechsten Gegner */
#define WPB_FLAME  2u   /* zwei driftende Partikel je Schuss */
#define WPB_PIERCE 3u   /* durchschlagend: Treffer verbraucht den Schuss NICHT */
#define WPB_LASER  4u   /* Dauerstrahl: schadet jedem Gegner in der Spalte je Frame */
#define WPB_MINE   5u   /* steuerbarer Pod, legt Minen, Flaechen-Blast bei Loslassen */
#define WPB_BOMB   6u   /* Bogenwurf nach oben, Zeitzuender, Flaechen-Blast */
#define WPB_ELECTRO 7u  /* getetherter Ball, weggeschleudert, kehrt zurueck */
#define WPB_DRONE  8u   /* Salve von Schuessen je Schussintervall */
static const u8 wp_behavior[LVL_WEAPON_COUNT] = {
    /*0*/WPB_STD, /*1*/WPB_STD, /*2*/WPB_STD, /*3*/WPB_LASER, /*4*/WPB_STD, /*5*/WPB_MINE,
    /*6*/WPB_ELECTRO, /*7*/WPB_DRONE, /*8*/WPB_HOMING, /*9*/WPB_FLAME, /*10*/WPB_BOMB
};
/* SKALIERUNG DOS->NGPC (s0.4d): Pixel halbieren, Ticks->Frames x1,648, damit
   px/Tick -> px/Frame ~ x1/3 (das Tool macht DOS 9px/Tick -> 3px/Frame). Schaden
   und HP bleiben unveraendert. Diese Faktoren gelten fuer ALLE Werte unten. */
#define HOMING_ACC  6    /* Lenk-Beschleunigung je Achse (1/16 px/Frame^2) */
#define HOMING_MAX  32   /* Grenzgeschw. je Achse ~2 px/Frame (DOS 4-5 px/Tick x1/3) */

/* naechsten aktiven, sichtbaren Gegner zu (cx,cy) suchen; MAPOBJ_NONE-artiger
   Rueckgabewert 0xFF = keiner gefunden. Liefert Mittelpunkt ueber out-Zeiger. */
static u8 wp_nearest_enemy(s16 cx, s16 cy, s16 *ox, s16 *oy) {
    u8 e, best = 0xFFu;
    u16 bestd = 0xFFFFu;
    for (e = 0u; e < (u8)MAX_ENEMIES; e++) {
        s16 dx, dy; u16 d;
        if (!g_enemies[e].active || g_enemies[e].y >= (s16)CLIP_Y) continue;
        dx = (s16)((s16)g_enemies[e].x + 8 - cx);
        dy = (s16)((s16)g_enemies[e].y + 8 - cy);
        if (dx < 0) dx = (s16)-dx;
        if (dy < 0) dy = (s16)-dy;
        d = (u16)((u16)dx + (u16)dy);   /* Manhattan reicht zur Zielwahl */
        if (d < bestd) { bestd = d; best = e; *ox = (s16)((s16)g_enemies[e].x + 8); *oy = (s16)((s16)g_enemies[e].y + 8); }
    }
    return best;
}

/* Einen Modulschuss der Waffe i mit Startgeschwindigkeit (vx,vy) einreihen.
   Startposition = Schiff + Waffen-Offset (wie bisher), OAM erst beim Zeichnen. */
static void wp_spawn(u8 i, s16 vx, s16 vy) {
    u8 b;
    for (b = 0u; b < (u8)MAX_WPBULLETS; b++) {
        if (g_wp_bullets[b].active) continue;
        g_wp_bullets[b].active = 1u;
        g_busy_wpbul = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
        /* Muendung = Modulposition + eigener Versatz (siehe WPX_MUZZLE_*). */
        g_wp_bullets[b].x_fix  = (s16)(((s16)g_player.x + wpx_dx(i) + WPX_MUZZLE_DX(i)) << 4);
        g_wp_bullets[b].y_fix  = (s16)(((s16)g_player.y + wpx_dy(i) + WPX_MUZZLE_DY(i)) << 4);
        g_wp_bullets[b].oam    = OAM_NONE;
        g_wp_bullets[b].w      = i;
        g_wp_bullets[b].vx     = vx;
        g_wp_bullets[b].vy     = vy;
        g_wp_bullets[b].last_snum = 0u;
        return;
    }
}

/* --- BOMB (Waffe 10): Bogenwurf nach oben, Zeitzuender, Flaechen-Blast ---
   DOS (weapons.md): steigt 6 px/Tick fuer ((y-0xc)>>4)+4 Ticks, haelt 3 Ticks
   Zuender, dann 8 Schaden im +-0x20-Kasten; nur EINE Bombe gleichzeitig.
   SKALIERT (s0.4d): Steigen 6->2 px/Frame, Box +-0x20(32)->+-16 px, Dauer x1,648
   (Ticks->Frames). Unser y>>3 entspricht DOS y>>4 (halbe Aufloesung, 8px-Kacheln).
   Schaden 8 bleibt (HP unskaliert). */
typedef struct { u8 active; s16 x_fix, y_fix; u8 timer; u8 phase; u8 oam; } TBomb;
static TBomb g_bomb;

/* LASER (Waffe 3): angehaengter Dauerstrahl, Sichtbar als Sprite-Saeule ueber dem
   Schiff. LASER_SEGS Glieder, ~20 px Abstand. */
#define LASER_SEGS 7
static u8 g_laser_on;
static u8 g_laser_oam[LASER_SEGS];
static void bomb_launch(u8 i) {
    g_bomb.active = 1u;
    g_busy_wpent = 1u;   /* Frueh-Ausstieg: Flag an der Erzeugungsstelle */
    g_bomb.x_fix  = (s16)(((s16)g_player.x + lvl_weapon_dx[i]) << 4);
    g_bomb.y_fix  = (s16)(((s16)g_player.y + lvl_weapon_dy[i]) << 4);
    /* DOS ((y>>4)+4) Ticks -> ((y>>3)+4)*1,648 Frames (~5/3). */
    g_bomb.timer  = (u8)((((u16)(g_player.y >> 3) + 4u) * 5u) / 3u);
    g_bomb.phase  = 0u;   /* 0 = steigen, 1 = Zuender */
    g_bomb.oam    = OAM_NONE;
}

/* Nutzerkorrektur 10.07.2026: die Zusatzwaffe soll NUR feuern, waehrend der
   Spieler selbst gerade feuert (J_A gedrueckt) ??? vorher schoss sie
   automatisch auf eigenem Cooldown, unabhaengig vom Feuerknopf. Cooldown
   tickt deshalb auch nur waehrend J_A gehalten wird (kein "Nachladen im
   Hintergrund", sonst wuerde nach langer Pause sofort beim naechsten
   Feuerknopf-Druck abgefeuert).
   Bugfix 10.07.2026 (Nacht): "Schuesse stoppen wenn ich aufhoere zu
   schiessen" ??? der fruehere Early-Return bei losgelassenem J_A hat NICHT
   nur das Nachspawnen/den Cooldown geschuetzt, sondern auch die Bewegungs-
   /Kollisions-Schleife fuer BEREITS FLIEGENDE g_wp_bullets komplett
   uebersprungen, die dadurch mitten in der Luft einfroren. Genau wie die
   normalen Schiffs-Schuesse (siehe bullets_update: kein J_A-Gate auf der
   Bewegung) muessen bereits abgefeuerte Schuesse JEDEN Frame weiterfliegen
   ??? nur das Erzeugen NEUER Schuesse (und der Cooldown) bleibt an J_A
   gebunden. Update 19.07.2026 (DOS-Abgleich, Nutzerwunsch): Feuerrate- UND
   Power-Pickups wirken jetzt AUCH auf die Heckwaffe ??? Cooldown via
   firerate_stage (Autofire-Latch), Schaden via power_stage (siehe
   rear_gun_damage / die Autofire-Kopplung weiter unten). */
static void weapon_update(void) {
    u8 i;
    u8 shot_fired = 0u;
    u8 edge, firing;
    /* BUGFIX 22.07.2026 (Nutzerbericht "Schnellfeuern geht bei der Heckwaffe
       nicht, schnelles A-Druecken feuert nur sporadisch"):
       Der Nachladezaehler lief NUR, solange A gehalten wurde - er stand mit im
       if(firing)-Block. Beim Tippen bekam er also kaum Frames zum Herunter-
       zaehlen, Tippen feuerte dadurch LANGSAMER als Halten. Die Hauptkanone
       macht es seit dem 17.07. richtig: sie zaehlt jeden Frame herunter, und
       eine frische Tastenflanke darf den Nachladezaehler ueberspringen.
       Dasselbe jetzt fuer ALLE Modulwaffen (Nutzerwunsch 22.07.).
       Der frueher hier genannte Grund gegen das Herunterzaehlen im Hintergrund
       ("sonst wird nach langer Pause sofort abgefeuert") ist damit gegenstands-
       los - genau das ist ja beim Antippen erwuenscht. */
    for (i = 0u; i < (u8)LVL_WEAPON_COUNT; i++)
        if (g_player.weapon_cooldown[i] > 0u) g_player.weapon_cooldown[i]--;

    edge   = (u8)((g_pad_pressed & J_A) ? 1u : 0u);
    firing = (u8)(g_player.weapons_active && ((g_pad & J_A) || edge));
    if (firing) {
        for (i = 0u; i < (u8)LVL_WEAPON_COUNT; i++) {
            u8 beh;
            if (!(g_player.weapons_active & ((u16)1u << i))) continue;
            beh = wp_behavior[i];
            /* Dauerwaffen wirken JEDEN Frame waehrend des Feuerns und haben eigene
               Update-Funktionen (laser/electro/mine_update), die das selbst
               pruefen - hier nicht ueber den Nachladetakt behandeln. */
            if (beh == (u8)WPB_LASER || beh == (u8)WPB_ELECTRO || beh == (u8)WPB_MINE) continue;
            if (g_player.weapon_cooldown[i] == 0u || edge) {
                u8 wr;
                s16 svx = (s16)lvl_weapon_bullet_dx[i];
                s16 svy = (s16)lvl_weapon_bullet_dy[i];
                /* 23.07.2026: Spawn je nach Waffen-Verhalten (siehe wp_behavior). */
                if (beh == (u8)WPB_FLAME) {
                    /* Drift ~+-0,7 px/Frame (DOS +-2 px/Tick x1/3). Klettern (svy)
                       bleibt die Tool-Bahn. */
                    wp_spawn(i, (s16)(((s16)GetRandom(5) - 2) * 6), svy);
                    wp_spawn(i, (s16)(((s16)GetRandom(5) - 2) * 6), svy);
                } else if (beh == (u8)WPB_DRONE) {
                    /* Salve: DOS 4/8/12 je Power; hier vom freien Poolplatz
                       begrenzt (MAX_WPBULLETS). Faecher nach vorn. */
                    u8 shots = (u8)(4u + (u8)(g_player.power_stage * 2u));
                    u8 s;
                    for (s = 0u; s < shots; s++)
                        wp_spawn(i, (s16)(((s16)(s % 5u) - 2) * 24), svy);
                } else if (beh == (u8)WPB_BOMB) {
                    if (!g_bomb.active) bomb_launch(i);   /* nur eine Bombe gleichzeitig */
                } else {
                    wp_spawn(i, svx, svy);   /* STD / HOMING / PIERCE */
                }
                /* Autofire-Kopplung 19.07.2026 (DOS: front/rear/side feuern auf DEMSELBEN
                   Autofire-Latch [0x9130], weapons.ts): die Heckwaffe (Index 0) nutzt die
                   Autofire-Rate lvl_firerate_stage[firerate_stage] statt ihrer festen
                   lvl_weapon_rate -> das Autofire-Pickup beschleunigt sie mit. Andere
                   (spaetere) Waffen behalten ihre eigene lvl_weapon_rate (DOS: BOMB/HOMING
                   haben eigene Perioden). >>1 = 30-fps-Cap, siehe fire_cd. */
                if (i == 0u) wr = lvl_firerate_stage[g_player.firerate_stage];
                else         wr = lvl_weapon_rate[i];
                g_player.weapon_cooldown[i] = (u8)(wr >> 1);
                /* Modul-Animation dieser Waffe neu starten und ueber das gerade
                   gesetzte Intervall strecken (siehe g_wp_adiv). */
                { u8 al = g_wp_alen[i];
                  u8 iv = (u8)(wr >> 1);
                  u8 dv = (u8)(al ? (iv / al) : 1u);
                  if (dv == 0u) dv = 1u;
                  g_wp_adiv[i] = dv; g_wp_afrm[i] = 0u; g_wp_atck[i] = 0u; }
                shot_fired = 1u;
            }
        }
        {
            /* Muzzle-Flash an die ECHTE Feuerrate koppeln (Nutzerbefund 19.07.2026:
               Anim passte nicht zur Schussfrequenz). Vorher lief die Anim mit der
               generischen lvl_sspr_anim_speed[9]=10 -> 10*5=50-Frame-Zyklus, waehrend
               alle 5 Frames geschossen wird = 10x zu langsam. DOS: Rear-Muzzle =
               "1-tick frames" (timing.md). Jetzt: bei jedem Schuss auf Frame 0
               zuruecksetzen und 1 Frame/Game-Frame vorwaerts -> die 5-Frame-Anim
               spielt exakt einmal pro Schussintervall ab (haelt auf dem letzten
               Frame, falls das Intervall laenger wird, z.B. bei anderer Feuerrate).
               g_wpmod_aidx kommt aus game_start() (lvl_weapon_spr[0] konstant). */
            /* 27.07.2026 (Nutzer: "Animation beim Schiessen zu schnell"): der
               Zaehler lief mit EINEM Bild je Spielframe. Bei der 4-Bild-Kanone
               waren das 4 Frames = 0,13 s, also ein Zucken statt eines Rueckstosses.
               Jetzt bestimmt die im Tool gesetzte Bildrate das Tempo:
               lvl_sspr_anim_speed ist in 1/60-Frames angegeben, das Spiel laeuft
               mit 30-fps-Deckel -> >>1. Damit ist die Geschwindigkeit dort
               einstellbar, wo die Animation gezeichnet wird, statt hier im Code.
               Der Neustart bei jedem Schuss bleibt (Muzzle-Flash sitzt auf dem
               Schuss, Befund vom 19.07.2026). */
            u8 aw;
            (void)shot_fired;
            for (aw = 0u; aw < (u8)LVL_WEAPON_COUNT; aw++) {
                u8 al = g_wp_alen[aw];
                if (al < 2u) continue;                      /* Standbild */
                if (g_wp_adiv[aw] == 0u) g_wp_adiv[aw] = 1u;
                g_wp_atck[aw]++;
                if (g_wp_atck[aw] >= g_wp_adiv[aw]) {
                    g_wp_atck[aw] = 0u;
                    if ((u8)(g_wp_afrm[aw] + 1u) < al) g_wp_afrm[aw]++;
                }
            }
            /* Der alte Zeichenpfad von Waffe 0 liest weiter g_wpmod_frame. */
            g_wpmod_frame = g_wp_afrm[0];
        }
    } else {
        u8 aw;
        g_wpmod_tick = 0u;
        g_wpmod_frame = 0u;
        for (aw = 0u; aw < (u8)LVL_WEAPON_COUNT; aw++) {
            g_wp_afrm[aw] = 0u; g_wp_atck[aw] = 0u;
        }
    }
    for (i = 0u; i < (u8)MAX_WPBULLETS; i++) {
        s16 x, y;
        u8  e;
        if (!g_wp_bullets[i].active) continue;
        /* 23.07.2026: Homing lenkt vx/vy Frame fuer Frame auf den naechsten
           Gegner zu (DOS: HOMING MISSILE dreht schrittweise zum Ziel). Ohne Ziel
           behaelt der Schuss seine bisherige Bahn. */
        if (wp_behavior[g_wp_bullets[i].w] == (u8)WPB_HOMING) {
            s16 cx = (s16)((g_wp_bullets[i].x_fix >> 4) + 4);
            s16 cy = (s16)((g_wp_bullets[i].y_fix >> 4) + 4);
            s16 tx = 0, ty = 0;
            if (wp_nearest_enemy(cx, cy, &tx, &ty) != 0xFFu) {
                if      (tx > cx) g_wp_bullets[i].vx = (s16)(g_wp_bullets[i].vx + HOMING_ACC);
                else if (tx < cx) g_wp_bullets[i].vx = (s16)(g_wp_bullets[i].vx - HOMING_ACC);
                if      (ty > cy) g_wp_bullets[i].vy = (s16)(g_wp_bullets[i].vy + HOMING_ACC);
                else if (ty < cy) g_wp_bullets[i].vy = (s16)(g_wp_bullets[i].vy - HOMING_ACC);
                if (g_wp_bullets[i].vx >  HOMING_MAX) g_wp_bullets[i].vx =  HOMING_MAX;
                if (g_wp_bullets[i].vx < -HOMING_MAX) g_wp_bullets[i].vx = -HOMING_MAX;
                if (g_wp_bullets[i].vy >  HOMING_MAX) g_wp_bullets[i].vy =  HOMING_MAX;
                if (g_wp_bullets[i].vy < -HOMING_MAX) g_wp_bullets[i].vy = -HOMING_MAX;
            }
        }
        /* 23.07.2026: eigene Geschwindigkeit statt fester Tool-Bahn (vx/vy werden
           beim Spawn aus lvl_weapon_bullet_dx/dy gesetzt, von Homing/Flamer
           veraendert). Frueher wurde hier lvl_weapon_bullet_dx/dy[w] direkt
           gelesen; das trug keine Sondermechanik. */
        g_busy_wpbul = 1u;   /* Frueh-Ausstieg: fliegender Schuss haelt das System wach */
        g_wp_bullets[i].x_fix = (s16)(g_wp_bullets[i].x_fix + g_wp_bullets[i].vx);
        g_wp_bullets[i].y_fix = (s16)(g_wp_bullets[i].y_fix + g_wp_bullets[i].vy);
        x = (s16)(g_wp_bullets[i].x_fix >> 4);
        y = (s16)(g_wp_bullets[i].y_fix >> 4);
        if (x < 0 || x >= (s16)SCR_W || y < 0 || y >= (s16)BAR_Y) {
            g_wp_bullets[i].active = 0u;
            continue;
        }
        /* Bugfix 10.07.2026, Nacht: Map-Objekte ("Blumen") wurden hier nie
           geprueft ??? nur die Schiffs-Schuesse trafen sie (bullets_update).
           Gleiches Muster wie dort: Treffer-Punkt 4px rechts/2px runter vom
           Schuss-Ursprung (Sprite-Mitte), siehe bullets_update-Kommentar. */
        {
            u8 obj = mapobj_hit_test((u8)(x + 4), (u8)(y + 2));
            if (obj != MAPOBJ_NONE) {
                mapobj_wilt(obj);
                g_wp_bullets[i].active = 0u;
                continue;
            }
        }
        /* BUGFIX 22.07.2026: der Endboss wurde hier gar nicht geprueft - Auge und
           Tentakel kannten nur die Hauptkanone (bullets_update). Modul- und
           Heckschuesse flogen glatt hindurch, womit am Boss die halbe Aufruestung
           wirkungslos war. Reihenfolge wie im Original: erst die Glieder (die
           schlucken den Schuss ohne Schaden), dann das Augenfeld. */
        {
            u8 bcx = (u8)(x + 4), bcy = (u8)(y + 2);
            if (boss_seg_hit(bcx, bcy)) { g_wp_bullets[i].active = 0u; continue; }
            if (boss_eye_hit(bcx, bcy)) {
                boss_take_damage(wp_gun_damage(g_wp_bullets[i].w));
                g_wp_bullets[i].active = 0u;
                continue;
            }
        }
        for (e = 0u; e < (u8)MAX_ENEMIES; e++) {
            /* Performance-Fix 11.07.2026: ausserhalb des Bildschirms (z. B.
               waehrend ENEMY_OFFSCREEN_GRACE) kann ein Gegner unmoeglich mit
               einem Schuss/dem Schiff ueberlappen (beide bleiben immer
               y<CLIP_Y) ??? gleicher CLIP_Y-Test wie in draw_sprites() schon
               fuers Zeichnen, hier zusaetzlich fuer die Kollision. */
            {
            s16 dyv;
            if (!g_enemies[e].active || g_enemies[e].y >= CLIP_Y) continue;
            dyv = (s16)(y - (s16)g_enemies[e].y);
            if (dyv > (s16)BULLET_YFILTER || dyv < (s16)-BULLET_YFILTER) continue;
            if (rects_overlap_cached(x, y, BULLET_HIT_W, BULLET_HIT_H,
                                      g_enemies[e].x, g_enemies[e].y,
                                      g_enemies[e].hz_dx, g_enemies[e].hz_dy,
                                      g_enemies[e].hz_w, g_enemies[e].hz_h)) {
                enemy_take_damage(e, wp_gun_damage(g_wp_bullets[i].w));
                /* 23.07.2026: durchschlagende Waffen (WPB_PIERCE, z.B. Cannon)
                   werden vom Treffer NICHT verbraucht - sie fliegen weiter. */
                if (wp_behavior[g_wp_bullets[i].w] != (u8)WPB_PIERCE) g_wp_bullets[i].active = 0u;
                break;
            }
            }
        }
        /* Bugfix 10.07.2026, Nacht: Metasprite-Gegner (g_metaenemies) wurden
           hier nie geprueft ??? nur normale g_enemies. Waffen-Schuesse gingen
           dadurch wirkungslos durch Boss-/Mehrzellen-Gegner hindurch
           ("Schuesse machen den Gegnern nichts"). check_collisions() prueft
           fuer die Schiffs-Schuesse beide Listen, hier fehlte die zweite. */
        if (g_wp_bullets[i].active) {
            u8 m;
            for (m = 0u; m < (u8)MAX_METAENEMIES; m++) {
                s16 dyv;
                if (!g_metaenemies[m].active || g_metaenemies[m].y >= CLIP_Y) continue;
                dyv = (s16)(y - (s16)g_metaenemies[m].y);
                if (dyv > (s16)BULLET_YFILTER || dyv < (s16)-BULLET_YFILTER) continue;
                if (rects_overlap_cached(x, y, BULLET_HIT_W, BULLET_HIT_H,
                                          g_metaenemies[m].x, g_metaenemies[m].y,
                                          g_metaenemies[m].hz_dx, g_metaenemies[m].hz_dy,
                                          g_metaenemies[m].hz_w, g_metaenemies[m].hz_h)) {
                    metaenemy_take_damage(m, wp_gun_damage(g_wp_bullets[i].w));
                    if (wp_behavior[g_wp_bullets[i].w] != (u8)WPB_PIERCE) g_wp_bullets[i].active = 0u;
                    break;
                }
            }
        }
        /* Wurm-Segmente (Schritt 12) ??? gleiches Muster wie die beiden
           Schleifen oben, gecachte Hitzone gilt fuer ALLE Segmente eines
           Wurms (siehe TWorm.hz_dx-Kommentar). */
        if (g_wp_bullets[i].active) {
            u8 w, k, hit = 0u;
            for (w = 0u; w < (u8)MAX_WORMS && !hit; w++) {
                if (!g_worms[w].active) continue;
                for (k = 0u; k < g_worms[w].num_segs && !hit; k++) {
                    s16 dyv;
                    if (!g_worms[w].seg[k].alive || g_worms[w].seg[k].y >= CLIP_Y) continue;
                    dyv = (s16)(y - (s16)g_worms[w].seg[k].y);
                    if (dyv > (s16)BULLET_YFILTER || dyv < (s16)-BULLET_YFILTER) continue;
                    if (rects_overlap_cached(x, y, BULLET_HIT_W, BULLET_HIT_H,
                                              g_worms[w].seg[k].x, g_worms[w].seg[k].y,
                                              g_worms[w].hz_dx, g_worms[w].hz_dy,
                                              g_worms[w].hz_w, g_worms[w].hz_h)) {
                        worm_seg_hit(w, k, 1u);
                        if (wp_behavior[g_wp_bullets[i].w] != (u8)WPB_PIERCE) g_wp_bullets[i].active = 0u;
                        hit = 1u;
                    }
                }
            }
        }
    }
}

/* ===== Komplexe Sonderwaffen (23.07.2026) =====
   Alle Werte skaliert (s0.4d): px/Tick x1/3 -> px/Frame, Ticks x1,648 -> Frames,
   Boxmasse halbiert, Schaden/HP unveraendert. */

static s16 iabs16(s16 v) { return (s16)(v < 0 ? -v : v); }

/* Flaechenschaden: alle Gegner/Meta/Wurmsegmente in der +-r-Box um (cx,cy). */
static void area_damage(s16 cx, s16 cy, u8 r, u8 dmg) {
    u8 e, w, k;
    for (e = 0u; e < (u8)MAX_ENEMIES; e++) {
        if (!g_enemies[e].active || g_enemies[e].y >= (s16)CLIP_Y) continue;
        if (iabs16((s16)((s16)g_enemies[e].x + 8 - cx)) <= (s16)r &&
            iabs16((s16)((s16)g_enemies[e].y + 8 - cy)) <= (s16)r)
            { enemy_take_damage(e, dmg); g_dbg_area++; }
    }
    for (e = 0u; e < (u8)MAX_METAENEMIES; e++) {
        if (!g_metaenemies[e].active || g_metaenemies[e].y >= (s16)CLIP_Y) continue;
        if (iabs16((s16)((s16)g_metaenemies[e].x + 8 - cx)) <= (s16)r &&
            iabs16((s16)((s16)g_metaenemies[e].y + 8 - cy)) <= (s16)r)
            { metaenemy_take_damage(e, dmg); g_dbg_area++; }
    }
    for (w = 0u; w < (u8)MAX_WORMS; w++) {
        if (!g_worms[w].active) continue;
        for (k = 0u; k < g_worms[w].num_segs; k++) {
            if (!g_worms[w].seg[k].alive || g_worms[w].seg[k].y >= (s16)CLIP_Y) continue;
            if (iabs16((s16)((s16)g_worms[w].seg[k].x + 4 - cx)) <= (s16)r &&
                iabs16((s16)((s16)g_worms[w].seg[k].y + 4 - cy)) <= (s16)r)
                worm_seg_hit(w, k, 1u);
        }
    }
}

/* Pet-Sprite (Bombe/Ball/Mine/Drohnen brauchen keins - sie nutzen den Pool; hier
   fuer Einzelentities): Rueckfall-Projektilsprite der Heckwaffe, da die Tool-Daten
   fuer diese Waffen noch kein eigenes Sprite haben. */
static void wp_pet_draw(u8 *oam, u8 x, u8 y) {
    u16 wspr = lvl_weapon_bullet_spr[0];
    u16 wb_n = (u16)((wspr & 0x01FFu) - 1u);
    if (*oam == OAM_NONE) {
        *oam = oam_pool_alloc();
        if (*oam == OAM_NONE) return;
        SetSprite(*oam, spr_vram(lvl_sspr_a_idx[wb_n]), 0, x, y, lvl_sspr_a_pal[wb_n]);
        SpriteControl(*oam, SPR_FRONT, 0);
    } else {
        SetSpritePosition(*oam, x, y);
    }
}
static void wp_pet_hide(u8 *oam) {
    if (*oam != OAM_NONE) { UnsetSprite(*oam); oam_pool_free(*oam); *oam = OAM_NONE; }
}

/* BOMB: steigt, zuendet, Flaechen-Blast (siehe bomb_launch). */
static void bomb_update(void) {
    if (!g_bomb.active) return;
    g_busy_wpent = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
    if (g_bomb.phase == 0u) {
        g_bomb.y_fix = (s16)(g_bomb.y_fix - 32);   /* 2 px/Frame hoch (DOS 6/Tick x1/3) */
        if (g_bomb.timer > 0u) g_bomb.timer--;
        if (g_bomb.timer == 0u) { g_bomb.phase = 1u; g_bomb.timer = 5u; }  /* 3 Ticks x1,648 ~ 5 Frames */
    } else {
        if (g_bomb.timer > 0u) g_bomb.timer--;
        if (g_bomb.timer == 0u) {
            s16 bx = (s16)((g_bomb.x_fix >> 4) + 4), by = (s16)((g_bomb.y_fix >> 4) + 4);
            area_damage(bx, by, 16u, 8u);   /* +-16 px (DOS +-0x20 halbiert), 8 Schaden */
            wp_pet_hide(&g_bomb.oam);
            g_bomb.active = 0u;
        }
    }
}

/* LASER: solange gefeuert wird, schadet der Strahl JEDEN Frame jedem Gegner in
   der Spalte ueber dem Schiff. Schaden (Power+1)*3 = 3/6/9 (DOS, HP unskaliert);
   Strahlbreite 2*Power+4 px (DOS 4*Power+8 halbiert). Kein Projektil - reine
   Streifen-Pruefung ("hitscan"). */
static void laser_update(void) {
    u8 firing = (u8)((g_player.weapons_active & ((u16)1u << 3)) && (g_pad & J_A));
    if (!firing) { g_laser_on = 0u; return; }
    g_laser_on = 1u;
    g_busy_wpent = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
    {
        s16 cx  = (s16)((s16)g_player.x + 12);                 /* Schiffsmitte */
        s16 top = (s16)g_player.y;                             /* nur oberhalb des Schiffs */
        u8  hw  = (u8)((u8)(g_player.power_stage + 2u) + 8u);  /* Strahl-Halbbreite + halbe Gegnerbreite */
        u8  dmg = (u8)(((u8)g_player.power_stage + 1u) * 3u);
        u8  e, w, k;
        for (e = 0u; e < (u8)MAX_ENEMIES; e++) {
            if (!g_enemies[e].active || g_enemies[e].y >= (s16)CLIP_Y) continue;
            if ((s16)((s16)g_enemies[e].y + 8) >= top) continue;
            if (iabs16((s16)((s16)g_enemies[e].x + 8 - cx)) <= (s16)hw)
                { enemy_take_damage(e, dmg); g_dbg_area++; }
        }
        for (e = 0u; e < (u8)MAX_METAENEMIES; e++) {
            if (!g_metaenemies[e].active || g_metaenemies[e].y >= (s16)CLIP_Y) continue;
            if ((s16)((s16)g_metaenemies[e].y + 8) >= top) continue;
            if (iabs16((s16)((s16)g_metaenemies[e].x + 8 - cx)) <= (s16)hw)
                { metaenemy_take_damage(e, dmg); g_dbg_area++; }
        }
        for (w = 0u; w < (u8)MAX_WORMS; w++) {
            if (!g_worms[w].active) continue;
            for (k = 0u; k < g_worms[w].num_segs; k++) {
                if (!g_worms[w].seg[k].alive || g_worms[w].seg[k].y >= (s16)CLIP_Y) continue;
                if ((s16)((s16)g_worms[w].seg[k].y + 4) >= top) continue;
                if (iabs16((s16)((s16)g_worms[w].seg[k].x + 4 - cx)) <= (s16)hw)
                    { worm_seg_hit(w, k, 1u); g_dbg_area++; }
            }
        }
        /* Boss-Auge im Strahl */
        if (boss_eye_hit((u8)cx, (u8)(top > 20 ? top - 20 : 0)))
            { boss_take_damage(dmg); }
    }
}

/* ELECTRO BALL (Waffe 6): dockt unter dem Schiff. Solange gefeuert wird, schnellt
   der Ball nach vorn (DOS: 4x Schiffs-Tempo; hier 4 px/Frame hoch) und mahlt den
   ersten beruehrten Gegner um 4 Schaden/Frame (nicht verbraucht). Beim Loslassen
   gleitet er zurueck ans Dock. */
typedef struct { u8 active; s16 x_fix, y_fix; u8 oam; } TElectro;
static TElectro g_electro;
static void electro_update(void) {
    s16 dockx, docky;
    if (!(g_player.weapons_active & ((u16)1u << 6))) { g_electro.active = 0u; return; }
    g_busy_wpent = 1u;   /* Frueh-Ausstieg: siehe g_busy_* */
    dockx = (s16)((s16)g_player.x + 8);
    docky = (s16)((s16)g_player.y + 20);
    if (!g_electro.active) { g_electro.active = 1u; g_electro.x_fix = (s16)(dockx << 4); g_electro.y_fix = (s16)(docky << 4); g_electro.oam = OAM_NONE; }
    if (g_pad & J_A) {
        s16 bx, by; u8 e;
        g_electro.y_fix = (s16)(g_electro.y_fix - 64);   /* 4 px/Frame hoch */
        bx = (s16)(g_electro.x_fix >> 4); by = (s16)(g_electro.y_fix >> 4);
        for (e = 0u; e < (u8)MAX_ENEMIES; e++) {
            if (!g_enemies[e].active || g_enemies[e].y >= (s16)CLIP_Y) continue;
            if (rects_overlap_cached(bx, by, 8, 8, g_enemies[e].x, g_enemies[e].y,
                                     g_enemies[e].hz_dx, g_enemies[e].hz_dy, g_enemies[e].hz_w, g_enemies[e].hz_h))
                { enemy_take_damage(e, 4u); g_dbg_area++; break; }   /* nur der erste, 4/Frame */
        }
        if (by < 0) { g_electro.x_fix = (s16)(dockx << 4); g_electro.y_fix = (s16)(docky << 4); }  /* oben raus -> zurueck ans Dock */
    } else {
        /* heimgleiten, <=3 px/Frame je Achse (DOS <=8 px/Tick x1/3) */
        s16 bx = (s16)(g_electro.x_fix >> 4), by = (s16)(g_electro.y_fix >> 4);
        s16 dx = (s16)(dockx - bx), dy = (s16)(docky - by);
        if (dx >  3) dx =  3; else if (dx < -3) dx = -3;
        if (dy >  3) dy =  3; else if (dy < -3) dy = -3;
        g_electro.x_fix = (s16)(g_electro.x_fix + (dx << 4));
        g_electro.y_fix = (s16)(g_electro.y_fix + (dy << 4));
    }
}

/* MINE (Waffe 5): solange gefeuert wird, steigt ein Pod nach oben und laesst sich
   dann mit dem Steuerkreuz lenken; alle ~16 px legt er eine Mine (bis MINE_MAX).
   Beim LOSLASSEN zaehlen die Zuender herunter (gestaffelt) und die Minen
   explodieren der Reihe nach mit Flaechenschaden. DOS: Aufstieg 12->4 px/Frame,
   Lenken 8->3 px/Frame, Abstand 0x20->16 px, Box +-0x10/0x18 -> +-8/12 px,
   Schaden 4 + 2*Power. */
#define MINE_MAX 8
typedef struct { u8 active; s16 x_fix, y_fix; u8 oam; u8 fuse; } TMine;
typedef struct { u8 active; s16 x_fix, y_fix; u8 oam; u16 travel; } TMinePod;
static TMinePod g_minepod;
static TMine    g_mines[MINE_MAX];

static void mine_lay(s16 xf, s16 yf) {
    u8 k, n = 0u;
    for (k = 0u; k < (u8)MINE_MAX; k++) if (g_mines[k].active) n++;
    if (n >= (u8)MINE_MAX) return;
    for (k = 0u; k < (u8)MINE_MAX; k++) {
        if (g_mines[k].active) continue;
        g_mines[k].active = 1u;
        g_busy_wpent = 1u;   /* Frueh-Ausstieg: Flag an der Erzeugungsstelle */
        g_mines[k].x_fix  = xf; g_mines[k].y_fix = yf;
        g_mines[k].oam    = OAM_NONE;
        g_mines[k].fuse   = (u8)(5u + n * 5u);   /* gestaffelt -> Reihen-Explosion */
        return;
    }
}
static void mine_update(void) {
    u8 firing = (u8)((g_player.weapons_active & ((u16)1u << 5)) && (g_pad & J_A));
    u8 owns   = (u8)((g_player.weapons_active & ((u16)1u << 5)) != 0u);
    u8 k;
    if (owns) g_busy_wpent = 1u;   /* Frueh-Ausstieg: Pod + gelegte Minen, siehe g_busy_* */
    if (owns) {
        s16 dockx = (s16)((s16)g_player.x + 8), docky = (s16)((s16)g_player.y + 20);
        if (!g_minepod.active) { g_minepod.active = 1u; g_minepod.x_fix = (s16)(dockx << 4); g_minepod.y_fix = (s16)(docky << 4); g_minepod.travel = 0u; g_minepod.oam = OAM_NONE; }
        if (firing) {
            s16 py = (s16)(g_minepod.y_fix >> 4);
            if (py > 16) {
                g_minepod.y_fix = (s16)(g_minepod.y_fix - 64);   /* 4 px/Frame hoch */
            } else {
                s16 mx = 0, my = 0;
                if (g_pad & J_LEFT) mx = -3; else if (g_pad & J_RIGHT) mx = 3;
                if (g_pad & J_UP)   my = -3; else if (g_pad & J_DOWN)  my = 3;
                g_minepod.x_fix = (s16)(g_minepod.x_fix + (mx << 4));
                g_minepod.y_fix = (s16)(g_minepod.y_fix + (my << 4));
                { s16 px = (s16)(g_minepod.x_fix >> 4);
                  if (px < 4)   g_minepod.x_fix = (s16)(4 << 4);
                  if (px > 150) g_minepod.x_fix = (s16)(150 << 4); }
                g_minepod.travel = (u16)(g_minepod.travel + (u16)(iabs16(mx) + iabs16(my)));
            }
            if (g_minepod.travel >= 16u) {
                g_minepod.travel = (u16)(g_minepod.travel - 16u);
                mine_lay(g_minepod.x_fix, g_minepod.y_fix);
            }
        } else {
            /* Pod gleitet heim */
            s16 bx = (s16)(g_minepod.x_fix >> 4), by = (s16)(g_minepod.y_fix >> 4);
            s16 dx = (s16)(dockx - bx), dy = (s16)(docky - by);
            if (dx >  3) dx =  3; else if (dx < -3) dx = -3;
            if (dy >  3) dy =  3; else if (dy < -3) dy = -3;
            g_minepod.x_fix = (s16)(g_minepod.x_fix + (dx << 4));
            g_minepod.y_fix = (s16)(g_minepod.y_fix + (dy << 4));
            g_minepod.travel = 0u;
        }
    } else {
        g_minepod.active = 0u;
    }
    /* Gelegte Minen: Zuender laeuft NUR bei losgelassenem Feuer (DOS 1000:26de);
       bei 0 explodiert die Mine mit Flaechenschaden. */
    for (k = 0u; k < (u8)MINE_MAX; k++) {
        if (!g_mines[k].active) continue;
        if (!firing) {
            if (g_mines[k].fuse > 0u) g_mines[k].fuse--;
            if (g_mines[k].fuse == 0u) {
                u8 r   = (u8)(8u + (u8)(g_player.power_stage * 4u));   /* +-8 / +-12 px */
                u8 dmg = (u8)(4u + (u8)(g_player.power_stage * 2u));   /* 4 / 6 */
                area_damage((s16)((g_mines[k].x_fix >> 4) + 4), (s16)((g_mines[k].y_fix >> 4) + 4), r, dmg);
                wp_pet_hide(&g_mines[k].oam);
                g_mines[k].active = 0u;
            }
        }
    }
}

// --- Kollisionen --
/* Kollision Wandwuermer (Schritt 19): Schiffs-Schuesse (Haupt + Modul) und
   Schiffskoerper gegen Wurmteile. Kopf ist NICHT dabei (unsichtbar/unverwundbar).
   8x8-Box je Teil (Tile-Oben-Links). Nach check_collisions() aufgerufen, weil es
   g_wp_bullets nutzt (dort deklariert). */
static void wallworms_collide(void) {
    u8 w, k, i;
    for (w = 0u; w < (u8)WALLWORM_SLOTS; w++) {
        TWallWorm *ww = &g_wallworms[w];
        s16 holey, holex;
        if (!ww->active) continue;
        holey = wallworm_hole_screen_y_ww(ww);
        holex = (s16)((s16)WALLWORM_EXITS[ww->exit_idx].col * 8);
        for (k = 0u; k < ww->part_count; k++) {
            TWallWormPart *p = &ww->part[k];
            s16 sx = (s16)(holex + p->wx);
            s16 sy = (s16)(holey + p->wy);
            u8 hit = 0u;
            if (!p->alive || sy < 0 || sy >= (s16)CLIP_Y || sx < 0 || sx >= (s16)SCR_W) continue;
            for (i = 0u; i < (u8)MAX_BULLETS && !hit; i++) {
                if (!g_bullets[i].active) continue;
                if (rects_overlap((s16)g_bullets[i].x, (s16)g_bullets[i].y, BULLET_HIT_W, BULLET_HIT_H + BULLET_SWEEP,
                                  sx, sy, 8u, 8u)) {   /* +SWEEP: Strecken-Pruefung, siehe BULLET_SWEEP */
                    g_bullets[i].active = 0u; hit = 1u;
                }
            }
            for (i = 0u; i < (u8)MAX_WPBULLETS && !hit; i++) {
                s16 bx, by;
                if (!g_wp_bullets[i].active) continue;
                bx = (s16)(g_wp_bullets[i].x_fix >> 4);
                by = (s16)(g_wp_bullets[i].y_fix >> 4);
                /* Waffen-Schuesse fliegen in beliebige Richtung (Homing) - bei
                   halbierter Pruefung die Box ringsum um den halben Zwei-Frame-
                   Weg (±4 px) aufweiten statt gerichtet zu verlaengern. */
#if TEST_COLL_HALF
                if (rects_overlap((s16)(bx - 4), (s16)(by - 4), BULLET_HIT_W + 8u, BULLET_HIT_H + 8u, sx, sy, 8u, 8u)) {
#else
                if (rects_overlap(bx, by, BULLET_HIT_W, BULLET_HIT_H, sx, sy, 8u, 8u)) {
#endif
                    g_wp_bullets[i].active = 0u; hit = 1u;
                }
            }
#if BENCH_NOKILL
            if (hit) continue;   /* Benchmark: Wandwurm-Segment ueberlebt (Bullet verbraucht) */
#else
            if (hit) { wallworm_seg_hit(w, k); k--; continue; }
#endif
            if (rects_overlap((s16)g_player.x, (s16)g_player.y, 22u, 21u, sx, sy, 8u, 8u))
                { g_dmg_src = 6u; player_hit(); }
        }
    }
}

static void check_collisions(void) {
    u8 i, j;

    for (i = 0; i < MAX_BULLETS; i++) {
        s16 brx, bry;   /* Schuss-Rechteck EINMAL pro Schuss aufgeloest (aspr=0 -> feste 8x8-Box) */
        u8  by_ref;
        if (!g_bullets[i].active) continue;
        /* Kollisions-Opt 16.07.2026: Schuss-Rahmen einmal aufloesen +
           rects_overlap_cached (wie beim Schiff) statt hit_test_cached pro
           Gegner + Y-Distanz-Prefilter -- siehe BULLET_YFILTER. */
        brx = (s16)g_bullets[i].x;
        bry = (s16)g_bullets[i].y;
        by_ref = g_bullets[i].y;
        /* 25.07.2026 ZEIGER-ITERATION (Nutzermessung: Schuesse allein am Deckel,
           Gegner allein am Deckel, ZUSAMMEN 70-90 VBlanks). Diese Schleife ist
           das N-mal-M-Stueck des Frames: jeder Schuss gegen jeden Gegner. Sie
           griff pro Paar rund achtmal ueber g_enemies[j] zu, und cc900 rechnet
           bei JEDEM dieser Zugriffe j * sizeof(TEnemy) neu aus. Mit einem
           mitlaufenden Zeiger wird daraus ein einziges Inkrement je Runde.
           Gleiche Semantik, nur ohne die Adressrechnerei. */
        { TEnemy *e = &g_enemies[0];
        for (j = 0; j < MAX_ENEMIES; j++, e++) {
            s16 dyv;
            if (!e->active || e->y >= CLIP_Y) continue;
            dyv = (s16)((s16)by_ref - (s16)e->y);
            if (dyv > (s16)BULLET_YFILTER || dyv < (s16)-BULLET_YFILTER) continue;
            if (rects_overlap_cached(brx, bry, BULLET_HIT_W, BULLET_HIT_H + BULLET_SWEEP,
                                      e->x, e->y, e->hz_dx, e->hz_dy,
                                      e->hz_w, e->hz_h)) {
                g_bullets[i].active = 0;
                enemy_take_damage(j, main_gun_damage());
                break;
            }
        } }
        if (!g_bullets[i].active) continue;
        { TMetaEnemy *m = &g_metaenemies[0];   /* Zeiger-Iteration, siehe oben */
        for (j = 0; j < MAX_METAENEMIES; j++, m++) {
            s16 dyv;
            if (!m->active || m->y >= CLIP_Y) continue;
            dyv = (s16)((s16)by_ref - (s16)m->y);
            if (dyv > (s16)BULLET_YFILTER || dyv < (s16)-BULLET_YFILTER) continue;
            if (!rects_overlap_cached(brx, bry, BULLET_HIT_W, BULLET_HIT_H + BULLET_SWEEP,
                                       m->x, m->y, m->hz_dx, m->hz_dy,
                                       m->hz_w, m->hz_h)) continue;
            g_bullets[i].active = 0;
            metaenemy_take_damage(j, main_gun_damage());
            break;   /* Bugfix: sonst toetet EIN Schuss mehrere Kettenglieder ("Welle weg") */
        } }
        if (!g_bullets[i].active) continue;
        {
            u8 w, k, hit = 0u;
            for (w = 0u; w < (u8)MAX_WORMS && !hit; w++) {
                if (!g_worms[w].active) continue;
                for (k = 0u; k < g_worms[w].num_segs && !hit; k++) {
                    s16 dyv;
                    if (!g_worms[w].seg[k].alive || g_worms[w].seg[k].y >= CLIP_Y) continue;
                    dyv = (s16)((s16)by_ref - (s16)g_worms[w].seg[k].y);
                    if (dyv > (s16)BULLET_YFILTER || dyv < (s16)-BULLET_YFILTER) continue;
                    if (rects_overlap_cached(brx, bry, BULLET_HIT_W, BULLET_HIT_H + BULLET_SWEEP,
                                              g_worms[w].seg[k].x, g_worms[w].seg[k].y,
                                              g_worms[w].hz_dx, g_worms[w].hz_dy,
                                              g_worms[w].hz_w, g_worms[w].hz_h)) {
                        g_bullets[i].active = 0;
                        worm_seg_hit(w, k, 1u);
                        hit = 1u;
                    }
                }
            }
        }
    }

    /* 28.07.2026 Wandkriecher: VOR dem inv_cd-Ausstieg, damit Schusstreffer auch
       waehrend der Unverwundbarkeit nach einem Lebensverlust zaehlen (die
       Schiffskontakt-Haelfte schaltet ship_vuln selbst ab). Das g_busy-Flag haelt
       den Block bei "kein Kriecher im Bild" komplett aus dem Frame heraus. */
    if (g_busy_wcrawl) {
        s16 wsrx, wsry; u8 wsrw, wsrh;
        ship_hitzone_rect(&wsrx, &wsry, &wsrw, &wsrh);
        wcrawls_collide(wsrx, wsry, wsrw, wsrh, (u8)(g_player.inv_cd == 0u));
    }

    if (g_player.inv_cd > 0) return;
    {
        /* Performance-Fix 11.07.2026: Schiffs-Hitzone einmal pro Frame per
           O(1)-Tabelle aufgeloest (ship_hitzone_rect(), keine 17er-Suche
           mehr) statt bis zu 14x (MAX_ENEMIES+MAX_METAENEMIES), siehe
           rects_overlap_cached-Kommentar. */
        s16 srx, sry;
        u8  srw, srh;
        ship_hitzone_rect(&srx, &sry, &srw, &srh);
        { TEnemy *e = &g_enemies[0];   /* Zeiger-Iteration, siehe Schuss-Schleife */
        for (j = 0; j < MAX_ENEMIES; j++, e++) {
            if (!e->active || e->y >= CLIP_Y) continue;
            if (rects_overlap_cached(srx, sry, srw, srh,
                                      e->x, e->y, e->hz_dx, e->hz_dy,
                                      e->hz_w, e->hz_h)) {
                u8 dmg = contact_damage_for(e->spawn_idx);   /* 8, zaeh 16 */
                e->active = 0;
                enemy_killed(j, 0u);
                g_dmg_src=3u; player_damage(dmg);
            }
        } }
        { TMetaEnemy *m = &g_metaenemies[0];
        for (j = 0; j < MAX_METAENEMIES; j++, m++) {
            if (!m->active || m->y >= CLIP_Y) continue;
            if (rects_overlap_cached(srx, sry, srw, srh,
                                      m->x, m->y, m->hz_dx, m->hz_dy,
                                      m->hz_w, m->hz_h)) {
                u8 dmg = contact_damage_for(m->spawn_idx);   /* 8, zaeh 16 */
                m->active = 0;
                metaenemy_killed(j, 0u);
                g_dmg_src=4u; player_damage(dmg);
            }
        } }
        {
            u8 w, k;
            for (w = 0u; w < (u8)MAX_WORMS; w++) {
                if (!g_worms[w].active) continue;
                for (k = 0u; k < g_worms[w].num_segs; k++) {
                    if (!g_worms[w].seg[k].alive || g_worms[w].seg[k].y >= CLIP_Y) continue;
                    if (rects_overlap_cached(srx, sry, srw, srh,
                                              g_worms[w].seg[k].x, g_worms[w].seg[k].y,
                                              g_worms[w].hz_dx, g_worms[w].hz_dy,
                                              g_worms[w].hz_w, g_worms[w].hz_h)) {
                        worm_seg_hit(w, k, 0u);
                        g_dmg_src = 5u; player_hit();
                    }
                }
            }
        }
    }

    /* Schiff beruehrt ein Map-Objekt (??13): verwelkt es (einmalig) und zieht
       zusaetzlich ein Leben ab, falls lvl_mapobj_ship_damage gesetzt ist. */
    {
        u8 obj = ship_hits_mapobj(g_player.x, g_player.y);
        if (obj != MAPOBJ_NONE) {
            u8 was_wilted = g_mapobj_wilted[obj];
            mapobj_wilt(obj);
            if (!was_wilted && lvl_mapobj_ship_damage[obj]) { g_dmg_src = 7u; player_hit(); }
        }
    }
}

// --- Sprites zeichnen --
/* Unverwundbarkeit: frueher ein kurzer Weiss-Flash per Paletten-Umschaltung
   (ship_pals_flash/normal). Das ging nur, weil Schiff-Paletten exklusiv
   waren ??? im vereinheitlichten Sprite-System teilt sich das Schiff Paletten
   mit Schuss/Ziffern/Gegner (z.B. Palette 1/2), ein Flash wuerde also auch
   Schuss/Score/Gegner mit-weiss faerben. Stattdessen blinkt das Schiff jetzt
   waehrend der gesamten Unverwundbarkeits-Dauer (jede 4 Frames aus/an). */
static void draw_sprites(void) {
    u8 i;
    /* Basis + Power-Stufen direkt aus dem Export (Schritt 15) ??? kein
       Grafik-Umschalt-Flacker mehr noetig (Nutzerkorrektur 07.07.2026: der
       korrekte Schuss ist S149, nicht S153/154). Der SICHTBARKEITS-Flacker
       (g_flicker, siehe Deklaration) ist ein separater, spaeterer Wunsch. */
    u16 bullet_s = (u16)(lvl_power_stage_bullet_spr[g_player.power_stage] & 0x01FFu);
    g_flicker ^= 1u;

    /* OAM-PRIORITAET fuer Belohnungen (2026-07-19, Nutzerbericht "beim 4. Container
       fehlt das Braun"): Pickups + Heckwaffe reservieren ihre 2 OAM-Slots (a+b-
       Overlay) ZUERST ??? vor Gegnern/Wuermern/Schuessen weiter unten. Sonst geht bei
       Gedraenge der 2. Slot (b-Overlay) verloren und die braunen Details fehlen.
       Hier NUR reservieren (oam0/oam1 setzen); gezeichnet wird unten an alter Stelle,
       das dortige `if (oam0==OAM_NONE) alloc` ueberspringt dann die Zuteilung -> das
       Layering bleibt unveraendert. */
    if (!PROF_OFF(20))   /* Sammel-Teilblock 20, siehe unten bei den Schuessen */
    {
        u8 pk;
        if (g_player.weapons_active && g_wpmod_oam0 == OAM_NONE) {
            g_wpmod_oam0 = oam_pool_alloc();
            if (g_wpmod_oam0 != OAM_NONE) {
                g_wpmod_oam1 = oam_pool_alloc();
                if (g_wpmod_oam1 == OAM_NONE) { oam_pool_free(g_wpmod_oam0); g_wpmod_oam0 = OAM_NONE; }
                else g_wpmod_last_s = 0u;
            }
        }
        for (pk = 0u; pk < (u8)MAX_PICKUPS; pk++) {
            if (g_pickups[pk].active && g_pickups[pk].spr != 0u && g_pickups[pk].oam0 == OAM_NONE) {
                g_pickups[pk].oam0 = oam_pool_alloc();
                if (g_pickups[pk].oam0 != OAM_NONE) {
                    g_pickups[pk].oam1 = oam_pool_alloc();
                    if (g_pickups[pk].oam1 == OAM_NONE) { oam_pool_free(g_pickups[pk].oam0); g_pickups[pk].oam0 = OAM_NONE; }
                }
            }
        }
    }

    /* Nutzerkorrektur 14.07.2026 ("sprite slots immer dynamisch fuellen"):
       Schubduesen-Flammen (links/rechts, je 1 Slot) kommen jetzt aus dem
       OAM-Pool statt SPR_THRUST_0 ??? kleine lokale Hilfsfunktion, da es sich
       (anders als Gegner/Wurm/etc.) um zwei Singleton-Variablen statt ein
       Array handelt. */
    if (!PROF_OFF(21)) {   /* Teilblock 21: Schiff-Blinken + Schubduesen (goto bleibt innerhalb) */
    if (g_player.inv_cd > 0 && (g_player.inv_cd & 4u)) {
        ship_hide();
        if (g_thrust_oam0 != OAM_NONE) { UnsetSprite(g_thrust_oam0); oam_pool_free(g_thrust_oam0); g_thrust_oam0 = OAM_NONE; }
        if (g_thrust_oam1 != OAM_NONE) { UnsetSprite(g_thrust_oam1); oam_pool_free(g_thrust_oam1); g_thrust_oam1 = OAM_NONE; }
        g_thrust_last_s = 0u;
        g_thrust_last_back = 0xFFu;
    } else {
        if (!PROF_OFF(13))   /* Profiling-Teilblock: Schiff zeichnen */
        ship_draw_meta(tilt_by_level[g_tilt_level + 2], g_player.x, g_player.y);
        /* Schubduesen NUR bei gedruecktem HOCH/RUNTER (Nutzerkorrektur
           07.07.2026, Nacht: liefen vorher dauerhaft) ??? g_thrust_dir kommt
           aus thrust_update(), das im selben Frame vorher lief. */
        if (g_thrust_dir != 0u) {
            u8  back  = (g_thrust_dir == 2u);
            /* Performance-Fix 11.07.2026: g_thrust_aidx kommt schon aus
               thrust_update() (lief im selben Frame vorher mit demselben
               g_thrust_dir/head) ??? spart die zweite 12-Koepfe-Suche hier,
               siehe g_thrust_aidx-Kommentar. */
            /* 21.07.2026: leere Bilder ueberspringen statt sie zu zeichnen ???
               sonst blinkt die Flamme weg, wenn der Export ein Bild verworfen hat. */
            u16 t_s   = sspr_anim_frame_s(g_thrust_aidx, g_thrust_frame);
            /* Links/rechts haben seit der 10.07.2026-Korrektur je Richtung
               eigene, voneinander unabhaengige X-Offsets (kein gemeinsamer
               tdx+R_DX mehr) ??? siehe THRUST_*_DX/THRUST_*_R_DX-Kommentar. */
            u8  ldx   = back ? (u8)THRUST_BACK_DX   : (u8)THRUST_FWD_DX;
            u8  rdx   = back ? (u8)THRUST_BACK_R_DX : (u8)THRUST_FWD_R_DX;
            u8  ty    = (u8)(g_player.y + (back ? (u8)THRUST_BACK_DY : (u8)THRUST_FWD_DY));
            u8  lx    = (u8)(g_player.x + ldx);
            u8  rx    = (u8)(g_player.x + rdx);

            if (g_thrust_oam0 == OAM_NONE) {
                g_thrust_oam0 = oam_pool_alloc();
                if (g_thrust_oam0 == OAM_NONE) goto thrust_done;   /* Pool leer, naechsten Frame erneut */
                g_thrust_oam1 = oam_pool_alloc();
                if (g_thrust_oam1 == OAM_NONE) { oam_pool_free(g_thrust_oam0); g_thrust_oam0 = OAM_NONE; goto thrust_done; }
                g_thrust_last_s = 0u;
            }

            /* Performance-Fix 11.07.2026 (siehe ship_draw_meta-Kommentar):
               gleiches Tile wie letzten Frame -> nur Position schreiben.
               Map-Update 13.07.2026: Flip haengt jetzt auch von der Richtung
               ab (siehe SPR_S_THRUST_HEAD-Kommentar), NICHT mehr nur vom
               Slot -- Fastpath deshalb zusaetzlich gegen g_thrust_last_back
               pruefen, sonst bleibt bei einem Richtungswechsel mit zufaellig
               gleichem t_s das alte Flip-Bit stehen (Schub sieht seitenverkehrt
               falsch aus). */
            if (t_s == g_thrust_last_s && back == g_thrust_last_back) {
                SetSpritePosition(g_thrust_oam0, lx, ty);
                SetSpritePosition(g_thrust_oam1, rx, ty);
            } else {
                /* Tool-Screenshot (Screenshots\S9.png, 11.07.) bestaetigt: die
                   im neuen Export ueberlebende Animation IST die RUECKWAERTS-
                   Variante (der geloeschte Kopf 157 war vorwaerts) ??? Rueckwaerts
                   bleibt deshalb ungeflippt (native Grafik), vorwaerts bekommt
                   SPR_VFLIP (nicht umgekehrt wie in der ersten Fassung). */
                u8 vflip = back ? 0u : SPR_VFLIP;
                spr_draw_s_single_flip(g_thrust_oam0, t_s, lx, ty, vflip);
                spr_draw_s_single_flip(g_thrust_oam1, t_s, rx, ty, (u8)(vflip | SPR_HFLIP));
                g_thrust_last_s = t_s;
                g_thrust_last_back = back;
            }
        } else {
            if (g_thrust_oam0 != OAM_NONE) { UnsetSprite(g_thrust_oam0); oam_pool_free(g_thrust_oam0); g_thrust_oam0 = OAM_NONE; }
            if (g_thrust_oam1 != OAM_NONE) { UnsetSprite(g_thrust_oam1); oam_pool_free(g_thrust_oam1); g_thrust_oam1 = OAM_NONE; }
            g_thrust_last_s = 0u;
            g_thrust_last_back = 0xFFu;
        }
    }
    thrust_done: ;
    }   /* Ende Teilblock 21 */

    /* Vorwaerts-Schuesse: SOLIDE, kein Flacker mehr (Fix 23.07.2026, Nutzerbericht
       "nach Respawn UND nach dem Shop keine Schuesse sichtbar" - auch die Grund-
       kanone). Der Bug ist HARDWARE-ONLY (im Emulator feuert + zeichnet es in JEDEM
       Fall sauber, §7e: der Emulator modelliert die HW-OAM-Effekte nicht). Ursache-
       Klasse ist das bisherige Alloc/Free-GEFLACKER: der Schuss gab seinen OAM-Pool-
       Slot JEDEN OFF-Frame frei und holte ihn im ON-Frame neu (FLICKER_VIS). Verliert
       er den Slot beim Neu-Holen nach einem schweren Reload (respawn_do/shop_resume
       setzen Pool + Caches zurueck), bleibt er unsichtbar - genau die Slot-Verlust-
       Klasse aus §00.XIII. Jetzt haelt jeder aktive Schuss seinen Slot fuer die
       gesamte Lebensdauer (holen beim ersten Zeichnen, freigeben nur beim Despawn)
       und wird JEDEN Frame gezeichnet. Kein Churn, kein Flackern, immer sichtbar.
       4 Slots dauerhaft sind im 48er-Pool problemlos, und die Schuesse werden VOR
       den Gegnern gezeichnet -> bekommen ihren Slot zuerst. */
    if (!PROF_OFF(20) && g_busy_bullets) {   /* Sammel-Teilblock 20 + Frueh-Ausstieg */
    u8 still = 0u;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (g_bullets[i].active) still = 1u;   /* auch unter der Bar: kommt wieder hoch */
#if TEST_FLICKER_ALLBULLETS
        /* Messbuild ROM A: zurueck aufs Flacker-Multiplexing (OFF-Phase gibt den
           Slot frei) - absichtlich der alte Churn, siehe TEST_FLICKER_ALLBULLETS. */
        if (!g_bullets[i].active || g_bullets[i].y >= BAR_Y || !FLICKER_VIS(i)) {
#else
        if (!g_bullets[i].active || g_bullets[i].y >= BAR_Y) {
#endif
            if (g_bullets[i].oam != OAM_NONE) {
                UnsetSprite(g_bullets[i].oam); oam_pool_free(g_bullets[i].oam);
                g_bullets[i].oam = OAM_NONE; g_bullets[i].last_snum = 0u;
            }
            continue;
        }
        still = 1u;
        if (g_bullets[i].oam == OAM_NONE) {
            g_bullets[i].oam = oam_pool_alloc();
            if (g_bullets[i].oam == OAM_NONE) continue;   /* Pool leer -> naechsten Frame erneut */
            g_bullets[i].last_snum = 0u;
        }
        if (bullet_s == g_bullets[i].last_snum) {
            SetSpritePosition(g_bullets[i].oam, g_bullets[i].x, g_bullets[i].y);
        } else {
            spr_draw_s_single(g_bullets[i].oam, bullet_s, g_bullets[i].x, g_bullets[i].y);
            g_bullets[i].last_snum = bullet_s;
        }
    }
    g_busy_bullets = still;   /* erst JETZT loeschen - dieser Durchlauf hat die Slots freigegeben */
    }
    /* Nutzerkorrektur 14.07.2026 ("sprite slots immer dynamisch fuellen"):
       jeder Gegner haelt seinen eigenen OAM-Slot (TEnemy.oam0/oam1) fuer
       seine gesamte Lebensdauer ??? Zuteilung beim ersten Zeichnen nach Spawn,
       Freigabe sobald er inaktiv/unsichtbar wird. Wird zuerst gezeichnet
       (vor Metasprite-Gegnern/Wurm/etc., siehe oam_pool_*()-Kommentar),
       bekommt also bei Slot-Knappheit bevorzugt Nachschub, sobald irgendwo
       im Pool ein Slot frei wird. oam1 wird nur allokiert, wenn das aktuelle
       Tile wirklich einen b-Overlay hat (spart Pool-Slots) ??? WICHTIG:
       OAM_NONE (0xFF) darf NIE an SetSprite/UnsetSprite durchgereicht
       werden (waere Slot 255, weit ausserhalb 0-63), deshalb hier alles
       explizit statt spr_draw_s2 zu benutzen. */
    /* 25.07.2026 ZEIGER-HOISTING, siehe Metasprite-Schleife weiter unten: 53
       Zugriffe ueber g_enemies[i] wurden zu m-> mit EINER Adressrechnung. */
    if (!PROF_OFF(14) && g_busy_enemies) {   /* Teilblock 14 + Frueh-Ausstieg */
    u8 still = 0u;
    for (i = 0u; i < MAX_ENEMIES; i++) {
        TEnemy *e = &g_enemies[i];
        u8  show = (u8)(e->active && e->y < CLIP_Y);
        u16 s_num, n;
        u8  needs_b;
        /* Ein Gegner UNTER der Sichtgrenze ist nicht "weg" - er kann wieder
           hochkommen. Deshalb haelt schon e->active das System wach, nicht show. */
        if (e->active) still = 1u;
        if (!show) {
            if (e->oam0 != OAM_NONE) {
                UnsetSprite(e->oam0);
                oam_pool_free(e->oam0);
                if (e->oam1 != OAM_NONE) { UnsetSprite(e->oam1); oam_pool_free(e->oam1); }
                e->oam0 = e->oam1 = OAM_NONE;
                e->last_snum = 0u;
                e->chain_b = 0u;   /* 25.07.2026: Chaining-Zustand des Vornutzers verwerfen */
            }
            continue;
        }
        s_num = e->is_static
            ? e->spr_num
            : lvl_sspr_anim_frames[e->anim_idx][e->anim_frame];
        /* 22.07.2026: nur bei Sprite-Wechsel aus dem ROM lesen - auf dem
           Positions-Schnellpfad ist der Wert unveraendert. */
        if (s_num != e->last_snum) {
            n = (u16)(s_num - 1u);
            e->c_needs_b = (u8)(lvl_sspr_b_idx[n] != 0u);
        }
        needs_b = e->c_needs_b;
        if (e->oam0 == OAM_NONE) {
            e->oam0 = oam_pool_alloc();
            if (e->oam0 == OAM_NONE) continue;   /* Pool leer, naechsten Frame erneut versuchen */
            e->last_snum = 0u;   /* neuer Slot -> voller Redraw erzwingen */
        }
        /* b-Overlay-Slot LAZY holen ??? sobald IRGENDEIN Frame b braucht, nicht nur der
           allererste. BUGFIX 2026-07-19 (Nutzerbericht "beim 4. Container fehlt Braun"):
           vorher wurde oam1 NUR beim ersten Frame geholt (wenn der zufaellig b hatte).
           Der Container S100 hat mit S104 einen Frame OHNE b (b_idx=0) ??? startete die
           Anim beim Spawn dort, blieb oam1 fuer immer OAM_NONE, und ALLE spaeteren
           b-Frames zeichneten a-only -> Braun fehlte die ganze Lebensdauer. Traf je
           nach Anim-Phase beim Spawn nur manche Container -> exakt "nur der 4.". */
        if (needs_b && e->oam1 == OAM_NONE) {
            /* 25.07.2026 CHAINING: bevorzugt den DIREKT folgenden Slot nehmen -
               dann haengt das b-Overlay per Chain-Bit am a-Sprite und die
               Bewegung kostet einen Schreibzugriff statt zwei. Ist er belegt,
               irgendeinen anderen (unverkettet, wie bisher). */
            if (oam_pool_take((u8)(e->oam0 + 1u))) { e->oam1 = (u8)(e->oam0 + 1u); e->chain_b = 1u; }
            else { e->oam1 = oam_pool_alloc(); e->chain_b = 0u; }
            if (e->oam1 != OAM_NONE) e->last_snum = 0u;   /* voller Redraw MIT b erzwingen */
            /* Alloc fehlgeschlagen -> diesen Frame a-only, naechsten Frame erneut. */
        }
        /* Performance-Fix 11.07.2026: gleiches Tile wie letzter Frame -> nur Position. */
        if (s_num == e->last_snum) {
            SetSpritePosition(e->oam0, e->x, e->y);
            /* verkettetes b folgt automatisch - kein zweiter Schreibzugriff */
            if (needs_b && !e->chain_b && e->oam1 != OAM_NONE) SetSpritePosition(e->oam1, e->x, e->y);
        } else if (needs_b && e->chain_b && e->oam1 != OAM_NONE) {
            spr_draw_chain_cell(e->oam0, s_num, e->x, e->y, 0u);   /* a absolut, b als Delta (0,0) */
            e->last_snum = s_num;
        } else if (needs_b && e->oam1 != OAM_NONE) {
            spr_draw_s2(e->oam0, e->oam1, s_num, e->x, e->y);
            e->last_snum = s_num;
        } else {
            spr_draw_s_single(e->oam0, s_num, e->x, e->y);
            if (e->oam1 != OAM_NONE) UnsetSprite(e->oam1);   /* No-b-Frame: altes b-Overlay ausblenden */
            e->last_snum = s_num;
        }
        /* 23.07.2026: Treffer-Flash. Die OAM-Palettenzuordnung (0x8C00) wird nach
           dem Zeichnen ueberschrieben - das ueberlebt den Positions-Schnellpfad
           (der die Palette nicht neu schreibt) und trifft NUR dieses Sprite. Beim
           Flash-Ende erzwingt last_snum=0 einen Vollredraw mit der echten Palette. */
        if (e->flash) {
            u8 *sc = SPRITE_COLOUR;   /* cc900: kein Subscript direkt auf den Cast-Ausdruck (siehe library.c) */
            sc[e->oam0] = (u8)FLASH_PAL;
            if (e->oam1 != OAM_NONE) sc[e->oam1] = (u8)FLASH_PAL;
            e->flash--;
            if (e->flash == 0u) e->last_snum = 0u;
        }
    }
    g_busy_enemies = still;
    }
    /* Nutzerkorrektur 14.07.2026 ("sprite slots immer dynamisch fuellen"):
       kein Flicker-Multiplexing mehr ??? jede aktive Blumen-Schuss-Instanz
       haelt ihren eigenen OAM-Slot (TMapobjBullet.oam) fuer ihre Lebensdauer.
       spr_draw_s_single: S99 hat kein b-Overlay. */
    if (!PROF_OFF(20) && g_busy_mobullets) {   /* Sammel-Teilblock 20 + Frueh-Ausstieg */
    u8 still = 0u;
    for (i = 0; i < MAX_MAPOBJ_BULLETS; i++) {
        u8 idx = i;
        if (g_mo_bullets[idx].active) still = 1u;
        if (!g_mo_bullets[idx].active || !FLICKER_VIS(i)) {   /* OFF-Phase: Slot freigeben (Flacker 19.07.2026) */
            if (g_mo_bullets[idx].oam != OAM_NONE) {
                UnsetSprite(g_mo_bullets[idx].oam);
                oam_pool_free(g_mo_bullets[idx].oam);
                g_mo_bullets[idx].oam = OAM_NONE;
                g_mo_bullets[idx].last_snum = 0u;
            }
            continue;
        }
        if (g_mo_bullets[idx].oam == OAM_NONE) {
            g_mo_bullets[idx].oam = oam_pool_alloc();
            if (g_mo_bullets[idx].oam == OAM_NONE) continue;   /* Pool leer, naechsten Frame erneut versuchen */
            g_mo_bullets[idx].last_snum = 0u;
        }
        {
            u8 oam = g_mo_bullets[idx].oam;
            u8 mx = (u8)(g_mo_bullets[idx].x_fix >> 4);
            u8 my = (u8)(g_mo_bullets[idx].y_fix >> 4);
            /* Performance-Fix 11.07.2026: alle Blumen-Schuesse teilen
               dasselbe Tile (S99) -> praktisch immer gleich wie letzten
               Frame, nur Position schreiben. */
            if (g_mo_bullets[idx].spr == g_mo_bullets[idx].last_snum) {
                SetSpritePosition(oam, mx, my);
            } else {
                spr_draw_s_single(oam, g_mo_bullets[idx].spr, mx, my);
                /* 30.07.2026 Spiegelung + Prioritaet, siehe MAPOBJ_SHOT_*.
                   NUR hier im Vollpfad: SetSpritePosition oben schreibt nur x/y,
                   das Steuerbyte ueberlebt den Schnellpfad also. Beim Recyceln
                   eines Slots setzt mapobj_fire last_snum auf 0 - der naechste
                   Schuss laeuft damit zwangslaeufig wieder hier durch und holt
                   sich seine eigene Spiegelung/Prioritaet. */
                SpriteControl(oam, g_mo_bullets[idx].prio, g_mo_bullets[idx].flip);
                g_mo_bullets[idx].last_snum = g_mo_bullets[idx].spr;
            }
        }
    }
    g_busy_mobullets = still;
    }
    /* Nutzerkorrektur 14.07.2026 ("sprite slots immer dynamisch fuellen"):
       jede Metasprite-Gegner-Zelle haelt ihren eigenen OAM-Slot
       (TMetaEnemy.oam[c]) fuer die Lebensdauer der Instanz ??? gleiches
       Prinzip wie bei den normalen Gegnern, siehe dortigen Kommentar.
       spr_draw_s_single: S80-85 haben laut Export nie einen b-Overlay. */
    /* 25.07.2026 ZEIGER-HOISTING (HW-fps, Nutzermessung "ohne Zeichnen 26 -> 52
       fps"): diese Schleife griff 57x je Gegner ueber g_metaenemies[i] zu. Jeder
       dieser Zugriffe laesst cc900 i * sizeof(TMetaEnemy) (~80 Byte) neu
       ausrechnen - bei 7 Kaefern x 4 Zellen sind das hunderte ueberfluessige
       Adressrechnungen pro Frame. EINMAL einen Zeiger holen und ueber m->
       arbeiten, wie es der Wandwurm-Zeichenpfad laengst tut (TWallWormPart *p).
       Reine Adressarithmetik-Ersparnis, identische Semantik. */
    if (g_busy_metas) {   /* Frueh-Ausstieg, siehe g_busy_* */
    u8 still = 0u;
    for (i = 0u; i < MAX_METAENEMIES; i++) {
        TMetaEnemy *m = &g_metaenemies[i];
        u8  show = (u8)(m->active && m->y < CLIP_Y);
        u8  cnt = 0u, c;
        if (m->active) still = 1u;   /* wie bei den Gegnern: active, nicht show */
        if (show) {
            /* 25.07.2026 Zell-Cache (siehe TMetaEnemy): die ROM-Tabellen NUR bei
               Bildwechsel auswerten, nicht jeden Frame. */
            if (m->c_cached_idx != m->meta_idx) {
                u16 off2 = lvl_meta_off[m->meta_idx];
                u8  n2   = lvl_meta_count[m->meta_idx];
                u8  k2;
                if (n2 > (u8)META_CELLS) n2 = (u8)META_CELLS;
                m->c_cnt = n2;
                for (k2 = 0u; k2 < n2; k2++) {
                    u16 sn = lvl_meta_num[off2 + k2];
                    u16 bi = sn ? lvl_sspr_b_idx[sn - 1u] : 0u;
                    m->c_dx[k2]  = (u8)lvl_meta_dx[off2 + k2];
                    m->c_dy[k2]  = (u8)lvl_meta_dy[off2 + k2];
                    m->c_num[k2] = sn;
                    m->c_needb[k2] = (u8)(sn && bi != 0u && bi != 0xFFFFu);
                    m->c_flip[k2]  = META_FLIP_OF(off2 + k2);
                }
                m->c_cached_idx = m->meta_idx;
            }
            cnt = m->c_cnt;
        }
        /* 25.07.2026 SPRITE-CHAINING (siehe spr_draw_chain_cell / oam_pool_alloc_run):
           beim ERSTEN Zeichnen einen zusammenhaengenden Block aus cnt*2 Slots
           holen. Klappt das, kostet Bewegung genau EINEN Positionsschreibzugriff
           statt cnt*2 - bei 4 Zellen also 1 statt 8. Klappt es nicht (Pool zu
           zerstueckelt/voll), bleibt alles beim Alten: chain_base = OAM_NONE
           schaltet unten auf die Einzelslot-Vergabe zurueck. */
        if (PROF_OFF(15)) continue;   /* Profiling-Teilblock: Metasprite-Gegner zeichnen */
        if (show && cnt && m->chain_base == OAM_NONE && m->oam[0] == OAM_NONE) {
            u8 base = oam_pool_alloc_run((u8)(cnt * 2u));
            if (base != OAM_NONE) {
                m->chain_base  = base;
                m->chain_cells = cnt;
                for (c = 0u; c < cnt; c++) {
                    m->oam[c]      = (u8)(base + c * 2u);
                    m->oam_b[c]    = (u8)(base + c * 2u + 1u);
                    m->last_snum[c] = 0u;   /* Vollredraw erzwingen (setzt Chain-Bits + Deltas) */
                }
            }
        }
        /* Zellzahl gewachsen (Metaanim-Bild mit mehr Zellen als beim Blockkauf)
           -> Block passt nicht mehr, sauber auf den alten Pfad zurueckfallen. */
        if (m->chain_base != OAM_NONE && cnt > m->chain_cells) {
            for (c = 0u; c < (u8)META_CELLS; c++) {
                if (m->oam[c] != OAM_NONE) { UnsetSprite(m->oam[c]); oam_pool_free(m->oam[c]); m->oam[c] = OAM_NONE; }
                if (m->oam_b[c] != OAM_NONE) { UnsetSprite(m->oam_b[c]); oam_pool_free(m->oam_b[c]); m->oam_b[c] = OAM_NONE; }
                m->last_snum[c] = 0u;
            }
            m->chain_base = OAM_NONE; m->chain_cells = 0u;
        }
        if (m->chain_base != OAM_NONE) {
            u8 base = m->chain_base;
            if (!show) {
                /* Ganze Instanz weg: Block zurueckgeben (Slots sind normale
                   Pool-Slots, also ganz normal einzeln freigeben). */
                for (c = 0u; c < m->chain_cells; c++) {
                    UnsetSprite((u8)(base + c * 2u));
                    UnsetSprite((u8)(base + c * 2u + 1u));
                    oam_pool_free((u8)(base + c * 2u));
                    oam_pool_free((u8)(base + c * 2u + 1u));
                    m->oam[c] = m->oam_b[c] = OAM_NONE;
                    m->last_snum[c] = 0u;
                }
                m->chain_base = OAM_NONE; m->chain_cells = 0u;
                continue;
            }
            for (c = 0u; c < m->chain_cells; c++) {
                if (c >= cnt) {                      /* Bild mit weniger Zellen: Rest verstecken */
                    UnsetSprite((u8)(base + c * 2u));
                    UnsetSprite((u8)(base + c * 2u + 1u));
                    m->last_snum[c] = 0u;
                    continue;
                }
                if (m->c_num[c] != m->last_snum[c]) {
                    /* Zelle 0 haelt die absolute Position (kein Chain-Bit auf a),
                       alle weiteren das Delta zur VORIGEN Zelle. */
                    if (c == 0u)
                        spr_draw_chain_cell_f((u8)(base), m->c_num[0],
                                            (u8)(m->x + m->c_dx[0]), (u8)(m->y + m->c_dy[0]), 0u,
                                            m->c_flip[0]);
                    else
                        spr_draw_chain_cell_f((u8)(base + c * 2u), m->c_num[c],
                                            (u8)(m->c_dx[c] - m->c_dx[c - 1u]),
                                            (u8)(m->c_dy[c] - m->c_dy[c - 1u]), 1u,
                                            m->c_flip[c]);
                    m->last_snum[c] = m->c_num[c];
                }
            }
            /* DER Gewinn: eine einzige Position fuer den ganzen Metasprite. */
            SetSpritePosition(base, (u8)(m->x + m->c_dx[0]), (u8)(m->y + m->c_dy[0]));
            if (m->flash) {
                u8 *sc = SPRITE_COLOUR;
                for (c = 0u; c < m->chain_cells; c++) {
                    sc[(u8)(base + c * 2u)]      = (u8)FLASH_PAL;
                    sc[(u8)(base + c * 2u + 1u)] = (u8)FLASH_PAL;
                }
                m->flash--;
                if (m->flash == 0u)
                    for (c = 0u; c < (u8)META_CELLS; c++) m->last_snum[c] = 0u;
            }
            continue;
        }
        for (c = 0u; c < (u8)META_CELLS; c++) {
            if (!show || c >= cnt) {
                if (m->oam[c] != OAM_NONE) {
                    UnsetSprite(m->oam[c]);
                    oam_pool_free(m->oam[c]);
                    m->oam[c] = OAM_NONE;
                    m->last_snum[c] = 0u;
                }
                if (m->oam_b[c] != OAM_NONE) {
                    UnsetSprite(m->oam_b[c]);
                    oam_pool_free(m->oam_b[c]);
                    m->oam_b[c] = OAM_NONE;
                }
                continue;
            }
            if (m->oam[c] == OAM_NONE) {
                m->oam[c] = oam_pool_alloc();
                if (m->oam[c] == OAM_NONE) continue;   /* Pool leer, naechsten Frame erneut versuchen */
                m->last_snum[c] = 0u;
            }
            {
                /* 25.07.2026: alle vier Werte aus dem RAM-Zell-Cache statt aus dem ROM */
                u8  sx = (u8)(m->x + m->c_dx[c]);
                u8  sy = (u8)(m->y + m->c_dy[c]);
                u16 s_num = m->c_num[c];
                u8  need_b = m->c_needb[c];
                if (need_b) {
                    if (m->oam_b[c] == OAM_NONE) {
                        m->oam_b[c] = oam_pool_alloc();
                        m->last_snum[c] = 0u;   /* Neuzeichnen erzwingen */
                    }
                } else if (m->oam_b[c] != OAM_NONE) {
                    UnsetSprite(m->oam_b[c]);
                    oam_pool_free(m->oam_b[c]);
                    m->oam_b[c] = OAM_NONE;
                }
                /* Performance-Fix 11.07.2026: gleiches Tile wie letzten
                   Frame an dieser Zelle -> nur Position. */
                if (s_num == m->last_snum[c]) {
                    SetSpritePosition(m->oam[c], sx, sy);
                    if (m->oam_b[c] != OAM_NONE)
                        SetSpritePosition(m->oam_b[c], sx, sy);
                } else {
                    if (m->oam_b[c] != OAM_NONE)
                        spr_draw_s2(m->oam[c], m->oam_b[c], s_num, sx, sy);
                    else
                        spr_draw_s_single(m->oam[c], s_num, sx, sy);
                    if (m->c_flip[c]) {              /* 28.07.2026: gespiegelte Zelle */
                        u8 fm = META_FLIP_MASK(m->c_flip[c]);
                        SpriteControl(m->oam[c], SPR_FRONT, fm);
                        if (m->oam_b[c] != OAM_NONE) SpriteControl(m->oam_b[c], SPR_FRONT, fm);
                    }
                    m->last_snum[c] = s_num;
                }
            }
        }
        /* 23.07.2026: Treffer-Flash ueber ALLE Zellen (a + b), siehe enemy-Draw. */
        if (show && m->flash) {
            u8 *sc = SPRITE_COLOUR;   /* cc900: kein Subscript direkt auf den Cast-Ausdruck */
            for (c = 0u; c < (u8)META_CELLS; c++) {
                if (m->oam[c]   != OAM_NONE) sc[m->oam[c]]   = (u8)FLASH_PAL;
                if (m->oam_b[c] != OAM_NONE) sc[m->oam_b[c]] = (u8)FLASH_PAL;
            }
            m->flash--;
            if (m->flash == 0u)
                for (c = 0u; c < (u8)META_CELLS; c++) m->last_snum[c] = 0u;
        }
    }
    g_busy_metas = still;
    }

    /* Nutzerkorrektur 14.07.2026 ("sprite slots immer dynamisch fuellen"):
       jedes Wurm-Segment haelt seinen eigenen OAM-Slot (TWormSeg.oam) statt
       g_worm_oam_slot[]. spr_vram() wird direkt auf den ROHEN
       lvl_rotset_idx-Index angewendet (NICHT ueber lvl_sspr_a_idx ???
       Rotationssaetze liefern den Rohindex bereits direkt, siehe Anweisung
       Schritt 12). lvl_rotset_flip (Bit0=H/Bit1=V): dasselbe Rohtile wird
       fuer mehrere Richtungen wiederverwendet, je mit anderem Flip ???
       Fastpath muss deshalb AUCH das Flip vergleichen, nicht nur das
       Rohtile (TWormSeg.last_flip). */
    if (!PROF_OFF(16) && g_busy_worms)   /* Teilblock 16 + Frueh-Ausstieg */
    {
        u8 w, k, still = 0u;
        for (w = 0u; w < (u8)MAX_WORMS; w++) {
            for (k = 0u; k < (u8)MAX_WORM_SEGS; k++) {
                TWormSeg *sg = &g_worms[w].seg[k];
                u8 show = (u8)(g_worms[w].active && k < g_worms[w].num_segs &&
                                sg->alive && sg->y < CLIP_Y);
                if (g_worms[w].active && k < g_worms[w].num_segs && sg->alive) still = 1u;
                if (!show) {
                    if (sg->oam != OAM_NONE) {
                        UnsetSprite(sg->oam);
                        oam_pool_free(sg->oam);
                        sg->oam = OAM_NONE;
                        sg->last_snum = 0u;
                        sg->last_flip = 0xFFu;
                    }
                    continue;
                }
                {
                    u8  dir  = worm_seg_dir(&g_worms[w], k);
                    u16 raw  = lvl_rotset_idx[g_worms[w].rot][dir];
                    u8  rf   = lvl_rotset_flip[g_worms[w].rot][dir];
                    u8  flip = (u8)(((rf & 1u) ? SPR_HFLIP : 0u) | ((rf & 2u) ? SPR_VFLIP : 0u));
                    if (raw == 0xFFFFu) {
                        if (sg->oam != OAM_NONE) {
                            UnsetSprite(sg->oam);
                            oam_pool_free(sg->oam);
                            sg->oam = OAM_NONE;
                            sg->last_snum = 0u;
                            sg->last_flip = 0xFFu;
                        }
                        continue;
                    }
                    if (sg->oam == OAM_NONE) {
                        sg->oam = oam_pool_alloc();
                        if (sg->oam == OAM_NONE) continue;   /* Pool leer, naechsten Frame erneut versuchen */
                        sg->last_snum = 0u;
                        sg->last_flip = 0xFFu;
                    }
                    if (raw == sg->last_snum && flip == sg->last_flip) {
                        SetSpritePosition(sg->oam, sg->x, sg->y);
                    } else {
                        SetSprite(sg->oam, spr_vram(raw), 0, sg->x, sg->y,
                                  lvl_rotset_pal[g_worms[w].rot][dir]);
                        SpriteControl(sg->oam, SPR_FRONT, flip);
                        sg->last_snum = raw;
                        sg->last_flip = flip;
                    }
                }
            }
        }
        g_busy_worms = still;
    }
    if (!PROF_OFF(17) && g_busy_wworms) {   /* Teilblock 17 + Frueh-Ausstieg */
        wallworms_draw();   /* Schritt 19: Wandwuermer + wegfliegende Baelle (eigene OAM-Slots) */
        /* wallworms_draw() gibt die Slots seiner inaktiven Wuermer/Baelle selbst
           frei; das Flag setzen wallworm_tick()/die Ball-Schleife im naechsten
           Frame neu, solange noch etwas lebt. Hier reicht deshalb ein einfaches
           Loeschen - der Durchlauf, der aufraeumt, hat schon stattgefunden. */
        g_busy_wworms = 0u;
    }
    wcrawls_draw();   /* 28.07.2026: Wandkriecher - loescht g_busy_wcrawl selbst am Ende */
    /* Nutzerkorrektur 14.07.2026 ("sprite slots immer dynamisch fuellen"):
       Waffen-Modul (a+b-Overlay, Singleton) kommt jetzt aus dem OAM-Pool
       statt SPR_WPMODULE. */
    /* 27.07.2026: an WAFFE 0 gebunden statt an "irgendeine Waffe aktiv". Vorher
       erschien Modul 0 auch dann am Schiff, wenn der Spieler ausschliesslich eine
       andere Waffe gekauft hatte - ein Geistermodul. */
    if (!PROF_OFF(22))   /* Teilblock 22: Waffenmodul-Sprite */
    if (g_player.weapons_active & 1u) {
        /* Animation (Nutzerwunsch 10.07.2026, Nacht): S144 hat laut map.h
           eine 5-Frame-Animation (siehe g_wpmod_frame-Deklaration) ??? laeuft
           nur waehrend g_wpmod_frame in weapon_update() tatsaechlich
           voranschreitet (nur beim Feuern), sonst bleibt Frame 0 stehen. */
        /* g_wpmod_aidx kommt aus game_start() statt hier neu gesucht zu
           werden (Performance-Fix 11.07.2026, lvl_weapon_spr[0] ist konstant). */
        u16 wmod_s = lvl_sspr_anim_frames[g_wpmod_aidx][g_wpmod_frame];
        u8  wx = (u8)(g_player.x + lvl_weapon_dx[0]);
        u8  wy = (u8)(g_player.y + lvl_weapon_dy[0]);
        if (g_wpmod_oam0 == OAM_NONE) {
            g_wpmod_oam0 = oam_pool_alloc();
            if (g_wpmod_oam0 != OAM_NONE) {
                g_wpmod_oam1 = oam_pool_alloc();
                if (g_wpmod_oam1 == OAM_NONE) { oam_pool_free(g_wpmod_oam0); g_wpmod_oam0 = OAM_NONE; }
                else g_wpmod_last_s = 0u;
            }
        }
        if (g_wpmod_oam0 != OAM_NONE) {
            /* Performance-Fix 11.07.2026: gleiches Frame wie letzten Frame
               (Animation laeuft nur beim Feuern und dann nur alle paar Frames
               weiter) -> nur Position schreiben statt spr_draw_s (a+b-Overlay,
               siehe SPR_WPMODULE-Kommentar). */
            if (wmod_s == g_wpmod_last_s) {
                SetSpritePosition(g_wpmod_oam0, wx, wy);
                SetSpritePosition(g_wpmod_oam1, wx, wy);
            } else {
                spr_draw_s2(g_wpmod_oam0, g_wpmod_oam1, wmod_s, wx, wy);
                g_wpmod_last_s = wmod_s;
            }
        }   /* sonst: Pool leer, naechsten Frame erneut versuchen */
    } else {
        if (g_wpmod_oam0 != OAM_NONE) { UnsetSprite(g_wpmod_oam0); oam_pool_free(g_wpmod_oam0); g_wpmod_oam0 = OAM_NONE; }
        if (g_wpmod_oam1 != OAM_NONE) { UnsetSprite(g_wpmod_oam1); oam_pool_free(g_wpmod_oam1); g_wpmod_oam1 = OAM_NONE; }
        g_wpmod_last_s = 0u;
    }
    /* ---- Module der Waffen 1..n (27.07.2026, siehe g_wpx_* oben) ----
       Waffe 0 laeuft weiter ueber den Block darueber. Die Animation folgt
       g_wpmod_frame, das weapon_update() NUR beim Feuern weiterzaehlt - bei der
       Kanone sitzt das Muendungsfeuer damit genau auf dem Schuss. */
    if (!PROF_OFF(22))
    { u8 w;
      g_wpx_tick++;
      for (w = 1u; w < (u8)LVL_WEAPON_COUNT; w++) {
        u16 spr = lvl_weapon_spr[w];
        u16 s_num[WPX_CELLS];
        s8  c_dx[WPX_CELLS], c_dy[WPX_CELLS];
        u8  c_fl[WPX_CELLS];
        u8  cnt = 0u, c;
        if ((g_player.weapons_active & ((u16)1u << w)) && spr != 0u) {
            u16 n = (u16)(spr & 0x01FFu);
            if (spr & 0x1000u) {                 /* Metasprite bzw. Metaanim */
                /* ZWEI Takte je Metaanim-Modul (27.07.2026, Nutzerwunsch):
                   die ERSTE Zelle (bei der Kanone das Rohr, S250-253) folgt
                   g_wpmod_frame und bewegt sich damit NUR beim Feuern - dort
                   sitzt das Muendungsfeuer. Alle weiteren Zellen (Unterbau,
                   S254-257) laufen frei ueber g_wpx_tick, also auch im
                   Leerlauf. Beide Bilder haben dieselbe Zellenzahl, deshalb
                   genuegt eine gemeinsame Zaehlung. */
                u16 mi_fire = n, mi_free = n;
                if (spr & 0x4000u) {
                    u8 h, len;
                    for (h = 0u; h < (u8)LVL_METAANIM_COUNT; h++)
                        if (lvl_metaanim_head[h] == n) break;
                    if (h >= (u8)LVL_METAANIM_COUNT) { cnt = 0u; goto wpx_done; }
                    len = lvl_metaanim_len[h];
                    mi_fire = lvl_metaanim_frames[h][(u8)(g_wp_afrm[w] % len)];
                    mi_free = lvl_metaanim_frames[h][(u8)((g_wpx_tick / lvl_metaanim_speed[h]) % len)];
                }
                if (mi_fire >= (u16)LVL_META_COUNT || mi_free >= (u16)LVL_META_COUNT) {
                    cnt = 0u; goto wpx_done;
                }
                cnt = lvl_meta_count[mi_fire];
                if (cnt > (u8)WPX_CELLS) cnt = (u8)WPX_CELLS;
                for (c = 0u; c < cnt; c++) {
                    u16 off = lvl_meta_off[(c == 0u) ? mi_fire : mi_free];
                    s_num[c] = lvl_meta_num[off + c];
                    c_dx[c]  = lvl_meta_dx[off + c];
                    c_dy[c]  = lvl_meta_dy[off + c];
                    c_fl[c]  = META_FLIP_OF(off + c);
                }
            } else if (spr & 0x4000u) {          /* animierter Einzelstreifen */
                u8 ai = sspr_anim_find(n);
                s_num[0] = (ai != 0xFFu)
                             ? lvl_sspr_anim_frames[ai][(u8)(g_wp_afrm[w] % lvl_sspr_anim_len[ai])]
                             : n;
                c_dx[0] = 0; c_dy[0] = 0; c_fl[0] = 0u; cnt = 1u;
            } else {                             /* Standbild */
                s_num[0] = n; c_dx[0] = 0; c_dy[0] = 0; c_fl[0] = 0u; cnt = 1u;
            }
        }
wpx_done:
        /* nicht mehr gebrauchte Zellen freigeben (verkauft, Bild kuerzer, inaktiv) */
        for (c = cnt; c < g_wpx_n[w]; c++) {
            if (g_wpx_oam[w][c][0] != OAM_NONE) {
                UnsetSprite(g_wpx_oam[w][c][0]); oam_pool_free(g_wpx_oam[w][c][0]);
                g_wpx_oam[w][c][0] = OAM_NONE;
            }
            if (g_wpx_oam[w][c][1] != OAM_NONE) {
                UnsetSprite(g_wpx_oam[w][c][1]); oam_pool_free(g_wpx_oam[w][c][1]);
                g_wpx_oam[w][c][1] = OAM_NONE;
            }
            g_wpx_last[w][c] = 0u;
        }
        g_wpx_n[w] = cnt;
        for (c = 0u; c < cnt; c++) {
            u8 wx = (u8)(g_player.x + wpx_dx(w) + c_dx[c]);
            u8 wy = (u8)(g_player.y + wpx_dy(w) + c_dy[c]);
            if (g_wpx_oam[w][c][0] == OAM_NONE) {
                g_wpx_oam[w][c][0] = oam_pool_alloc();
                if (g_wpx_oam[w][c][0] == OAM_NONE) continue;      /* Pool leer */
                g_wpx_oam[w][c][1] = oam_pool_alloc();
                if (g_wpx_oam[w][c][1] == OAM_NONE) {
                    oam_pool_free(g_wpx_oam[w][c][0]); g_wpx_oam[w][c][0] = OAM_NONE; continue;
                }
                g_wpx_last[w][c] = 0u;
            }
            if (s_num[c] == g_wpx_last[w][c]) {       /* gleiches Bild -> nur Position */
                SetSpritePosition(g_wpx_oam[w][c][0], wx, wy);
                SetSpritePosition(g_wpx_oam[w][c][1], wx, wy);
            } else {
                spr_draw_s2(g_wpx_oam[w][c][0], g_wpx_oam[w][c][1], s_num[c], wx, wy);
                if (c_fl[c]) {                       /* 28.07.2026: gespiegelte Zelle */
                    u8 fm = META_FLIP_MASK(c_fl[c]);
                    SpriteControl(g_wpx_oam[w][c][0], SPR_FRONT, fm);
                    SpriteControl(g_wpx_oam[w][c][1], SPR_FRONT, fm);
                }
                g_wpx_last[w][c] = s_num[c];
            }
        }
      }
    }
    /* 23.07.2026, Nutzerwunsch: Treffer-Flash fuer Schiff, Schubduesen UND die
       angebaute Waffe. Wie bei den Gegnern per OAM-Palettenzuordnung nach dem
       Zeichnen (ueberlebt die Positions-Schnellpfade). Schiff = feste Slots
       SPR_SHIP..+13 (7 Zellen x a+b; auf abgeschaltete b-Slots zu schreiben ist
       harmlos), Schub/Modul = ihre dynamischen Pool-Slots. Beim Flash-Ende
       Vollredraw erzwingen, damit die echten Farben zurueckkommen. */
    if (g_hit_flash) {
        u8 *sc = SPRITE_COLOUR;   /* cc900: kein Subscript direkt auf den Cast-Ausdruck */
        u8 fc;
        for (fc = 0u; fc < (u8)(SHIP_CELLS * 2u); fc++)
            sc[(u8)(SPR_SHIP + fc)] = (u8)FLASH_PAL;
        if (g_thrust_oam0 != OAM_NONE) sc[g_thrust_oam0] = (u8)FLASH_PAL;
        if (g_thrust_oam1 != OAM_NONE) sc[g_thrust_oam1] = (u8)FLASH_PAL;
        if (g_wpmod_oam0  != OAM_NONE) sc[g_wpmod_oam0]  = (u8)FLASH_PAL;
        if (g_wpmod_oam1  != OAM_NONE) sc[g_wpmod_oam1]  = (u8)FLASH_PAL;
        g_hit_flash--;
        if (g_hit_flash == 0u) {
            g_ship_last_meta = 0xFFu;   /* echte Schiff-Palette per Vollredraw zurueck */
            g_thrust_last_s = 0u; g_thrust_last_back = 0xFFu;
            g_wpmod_last_s = 0u;
        }
    }
    /* Nutzerkorrektur 14.07.2026: Waffen-Schuesse (1 Slot, feste Grafik)
       kommen jetzt aus dem OAM-Pool statt SPR_WPBULLET_0. */
    /* 25.07.2026 SOLIDE WAFFEN-SCHUESSE (Nutzerbericht: "bei maximaler Feuerrate
       geht das Spiel in die Knie, wenn etliche Bullets zu sehen sind").
       Vorher gab jeder Schuss seinen OAM-Slot JEDEN zweiten Frame frei und holte
       ihn im naechsten neu (FLICKER_VIS). Damit war jeder ON-Frame ein
       VOLLREDRAW: alloc + SetSprite (zwei Wortschreibzugriffe + Palettenbyte) +
       SpriteControl (Read-Modify-Write) - statt eines einzigen
       Positionsschreibzugriffs. Bei sechs Schuessen summiert sich das.
       Jetzt haelt jeder aktive Schuss seinen Slot fuer die Lebensdauer, genau
       wie die Grundkanone seit 23.07. (die wurde aus demselben Grund
       umgestellt - dort war der Slot-Verlust beim Neu-Holen sogar die Ursache
       fuer unsichtbare Schuesse nach einem Reload).
       Preis: bis zu 6 Pool-Slots dauerhaft belegt statt ~3. Der Pool hat 48,
       und eine fehlgeschlagene Zuteilung wird wie bisher im naechsten Frame
       erneut versucht. Sichtbare Folge: die Waffen-Schuesse flackern nicht mehr. */
    if (!PROF_OFF(18) && g_busy_wpbul) {   /* Teilblock 18 + Frueh-Ausstieg */
    u8 still = 0u;
    for (i = 0; i < MAX_WPBULLETS; i++) {
        if (g_wp_bullets[i].active) still = 1u;
#if TEST_FLICKER_ALLBULLETS
        if (!g_wp_bullets[i].active || !FLICKER_VIS(i)) {   /* Messbuild ROM A: Flacker-Churn, siehe oben */
#else
        if (!g_wp_bullets[i].active) {   /* nur beim Despawn freigeben, nicht jeden zweiten Frame */
#endif
            if (g_wp_bullets[i].oam != OAM_NONE) { UnsetSprite(g_wp_bullets[i].oam); oam_pool_free(g_wp_bullets[i].oam); g_wp_bullets[i].oam = OAM_NONE; }
            continue;
        }
        {
            u8 wx = (u8)(g_wp_bullets[i].x_fix >> 4);
            u8 wy = (u8)(g_wp_bullets[i].y_fix >> 4);
            /* Performance-Fix 11.07.2026: Tile/Palette/Flip sind fuer JEDEN
               Waffen-Schuss identisch (feste Daten, kein Wechsel) ??? sobald
               dieser Slot einmal (seit der letzten Zuteilung) korrekt
               gesetzt wurde, reicht Position-only fuer den Rest seiner
               Flugzeit. */
            /* 27.07.2026 (Nutzer: "die Raketen der Kanone sind nicht animiert"):
               lvl_weapon_bullet_spr traegt bei der Kanone das ANIM-Bit (0xC102),
               der Zeichenpfad hat es aber nie ausgewertet - er setzte das Sprite
               genau einmal beim Zuteilen des Slots und danach nur noch die
               Position. Jetzt wird bei animierten Projektilen das Bild je Frame
               bestimmt und der Slot bei jedem Bildwechsel neu geschrieben. */
            { u8 wsrc0 = g_wp_bullets[i].w;
              u16 sp0  = lvl_weapon_bullet_spr[wsrc0];
              if ((sp0 & 0x01FFu) == 0u) sp0 = lvl_weapon_bullet_spr[0];
              if ((sp0 & 0x4000u) && g_wp_bullets[i].oam != OAM_NONE) {
                  u8 ai0 = g_wp_bai[wsrc0];
                  if (ai0 != 0xFFu) {
                      u8 fp0 = (u8)((g_wpx_tick / lvl_sspr_anim_speed[ai0]) % lvl_sspr_anim_len[ai0]);
                      u16 fs0 = sspr_anim_frame_s(ai0, fp0);
                      if (fs0 != 0u && fs0 != g_wp_bullets[i].last_snum) {
                          u16 bn = (u16)(fs0 - 1u);
                          u8  bf = lvl_weapon_bullet_flip[wsrc0];
                          SetSprite(g_wp_bullets[i].oam, spr_vram(lvl_sspr_a_idx[bn]), 0, wx, wy,
                                    lvl_sspr_a_pal[bn]);
                          SpriteControl(g_wp_bullets[i].oam, SPR_FRONT,
                                        (u8)(((bf & 1u) ? SPR_HFLIP : 0u) | ((bf & 2u) ? SPR_VFLIP : 0u)));
                          g_wp_bullets[i].last_snum = fs0;
                      }
                  }
              } }
            if (g_wp_bullets[i].oam == OAM_NONE) {
                g_wp_bullets[i].oam = oam_pool_alloc();
                if (g_wp_bullets[i].oam == OAM_NONE) continue;   /* Pool leer, naechsten Frame erneut */
                /* Nutzerkorrektur 10.07.2026, Nacht: lvl_weapon_bullet_flip[0]
                   (aus map.h, Bit0=horizontal/Bit1=vertikal spiegeln ??? siehe
                   ANWEISUNG Schritt "Waffen-Pickup") wurde bisher komplett
                   ignoriert. Rueckwaerts-Waffe nutzt dieselbe Projektil-Grafik
                   wie eine Vorwaerts-Waffe, gespiegelt statt neuem Tile. */
                {
                    /* 22.07.2026: Sprite je Waffe. Die Waffen 1-10 haben im Export
                       noch KEIN eigenes Projektil-Sprite (lvl_weapon_bullet_spr = 0)
                       - dann faellt es auf das der Heckwaffe zurueck, sonst waeren
                       ihre Schuesse unsichtbar (draw ueberspringt spr 0). Sobald im
                       Tool eines zugewiesen wird, greift es ohne Codeaenderung. */
                    u8  wsrc = g_wp_bullets[i].w;
                    u16 wspr = lvl_weapon_bullet_spr[wsrc];
                    u8  wflp = lvl_weapon_bullet_flip[wsrc];
                    u16 wb_n;
                    u8  wb_flip;
                    if ((wspr & 0x01FFu) == 0u) { wspr = lvl_weapon_bullet_spr[0]; wflp = lvl_weapon_bullet_flip[0]; }
                    wb_n = (u16)((wspr & 0x01FFu) - 1u);
                    wb_flip = (u8)(((wflp & 1u) ? SPR_HFLIP : 0u) |
                                   ((wflp & 2u) ? SPR_VFLIP : 0u));
                    SetSprite(g_wp_bullets[i].oam, spr_vram(lvl_sspr_a_idx[wb_n]), 0, wx, wy,
                              lvl_sspr_a_pal[wb_n]);
                    SpriteControl(g_wp_bullets[i].oam, SPR_FRONT, wb_flip);
                }
            } else {
                SetSpritePosition(g_wp_bullets[i].oam, wx, wy);
            }
        }
    }
    g_busy_wpbul = still;
    }
    /* 23.07.2026: Sonderwaffen-Entities zeichnen (Rueckfall-Sprite, s. wp_pet_draw). */
    if (!PROF_OFF(19) && g_busy_wpent) {   /* Teilblock 19 + Frueh-Ausstieg */
    u8 still = 0u;
    if (g_bomb.active) { wp_pet_draw(&g_bomb.oam, (u8)(g_bomb.x_fix >> 4), (u8)(g_bomb.y_fix >> 4)); still = 1u; }
    else               wp_pet_hide(&g_bomb.oam);
    {   /* LASER-Saeule ueber dem Schiff */
        u8 s;
        if (g_laser_on) {
            u8 lx = (u8)(g_player.x + 8);   /* Mitte(12) - halbe Sprite(4) */
            still = 1u;
            for (s = 0u; s < (u8)LASER_SEGS; s++) {
                s16 sy = (s16)((s16)g_player.y - 4 - (s16)(s * 20));
                if (sy < -8) { wp_pet_hide(&g_laser_oam[s]); continue; }
                wp_pet_draw(&g_laser_oam[s], lx, (u8)(sy < 0 ? 0 : sy));
            }
        } else {
            for (s = 0u; s < (u8)LASER_SEGS; s++) wp_pet_hide(&g_laser_oam[s]);
        }
    }
    /* ELECTRO-Ball, Minen-Pod und gelegte Minen */
    if (g_electro.active) { wp_pet_draw(&g_electro.oam, (u8)(g_electro.x_fix >> 4), (u8)(g_electro.y_fix >> 4)); still = 1u; }
    else                  wp_pet_hide(&g_electro.oam);
    if (g_minepod.active) { wp_pet_draw(&g_minepod.oam, (u8)(g_minepod.x_fix >> 4), (u8)(g_minepod.y_fix >> 4)); still = 1u; }
    else                  wp_pet_hide(&g_minepod.oam);
    { u8 mk;
      for (mk = 0u; mk < (u8)MINE_MAX; mk++) {
          if (g_mines[mk].active) { wp_pet_draw(&g_mines[mk].oam, (u8)(g_mines[mk].x_fix >> 4), (u8)(g_mines[mk].y_fix >> 4)); still = 1u; }
          else                    wp_pet_hide(&g_mines[mk].oam);
      }
    }
    g_busy_wpent = still;   /* dieser Durchlauf hat alle inaktiven Entities versteckt */
    }   /* Ende Profiling-Teilblock 19 */
    /* Nutzerkorrektur 14.07.2026 ("Powerups auch nicht mehr flackern, nur
       die Schuesse"): das bisherige g_flicker-Blinken (60Hz Sparkle-Effekt)
       ist entfernt ??? Pickup wird jetzt genau wie alle anderen dynamischen
       Objekte durchgehend gezeichnet, solange aktiv. Nur die Spielerschuesse
       bleiben (wie vom Nutzer explizit gewuenscht) beim echten
       Flicker-Multiplexing. Map-Update 13.07.2026: lvl_group_reward_spr[]
       kann 0x0000 sein (Tool hat das Icon-Tile fuer Gruppe 3/AS129
       geloescht, siehe map.h-Header-Warnung) ??? spr==0 MUSS vor spr_draw_s2
       abgefangen werden, sonst rechnet s_num-1 zu 0xFFFF und liest
       lvl_sspr_a_idx[0xFFFF] weit ausserhalb des Arrays. Belohnung bleibt
       bis zum Tool-Fix unsichtbar, aber weiterhin normal einsammelbar
       (Kollision haengt nicht am Zeichnen). Dynamischer Slot aus dem
       OAM-Pool statt SPR_PICKUP, siehe ENEMY_PHYS_SLOTS-Kommentar-Historie. */
    /* 18.07.2026: ALLE Pickup-Slots zeichnen (vorher nur [0]) ??? mit MAX_PICKUPS>1
       koennen mehrere Belohnungen gleichzeitig fliegen (dichte Container am
       Levelanfang, row 0/2/9/18 ??? sonst "kam beim 2. Container nichts raus",
       weil das erste Pickup den einzigen Slot noch belegte). */
    if (!PROF_OFF(23) && g_busy_pickups)   /* Teilblock 23 + Frueh-Ausstieg */
    {
        u8 pk, still = 0u;
        for (pk = 0u; pk < (u8)MAX_PICKUPS; pk++) {
            if (g_pickups[pk].active) still = 1u;
            if (g_pickups[pk].active && g_pickups[pk].spr != 0u) {
                if (g_pickups[pk].oam0 == OAM_NONE) {
                    g_pickups[pk].oam0 = oam_pool_alloc();
                    if (g_pickups[pk].oam0 != OAM_NONE) {
                        g_pickups[pk].oam1 = oam_pool_alloc();
                        if (g_pickups[pk].oam1 == OAM_NONE) { oam_pool_free(g_pickups[pk].oam0); g_pickups[pk].oam0 = OAM_NONE; }
                    }
                }
                if (g_pickups[pk].oam0 != OAM_NONE) {
                    /* Animierte Belohnungs-Icons (Nutzerwunsch): Kopf-S-Nummer ueber
                       g_pickup_anim_tick durch ihre Frames laufen lassen. Kein Anim-
                       Kopf -> Standbild. spr_draw_s2: a+b-Overlay, dyn. OAM-Slots.
                       (Die 234 Anim-Tiles sind unbedenklich ??? der gr??n/weiss-Bug lag
                       am Bar-Fix, nicht an der Tile-Menge, siehe CLAUDE.md 7e.) */
                    u16 icon = g_pickups[pk].spr;
                    u8  ai;
                    if (g_pickups[pk].manim != 0xFFu) {
                        /* Waffen-Container: erste Zelle des aktuellen Metaanim-Bildes */
                        u8  mh2 = g_pickups[pk].manim;
                        u8  fp2 = (u8)((g_pickup_anim_tick / lvl_metaanim_speed[mh2]) % lvl_metaanim_len[mh2]);
                        u16 mi2 = lvl_metaanim_frames[mh2][fp2];
                        if (mi2 < (u16)LVL_META_COUNT && lvl_meta_count[mi2] != 0u)
                            icon = lvl_meta_num[lvl_meta_off[mi2]];
                    }
                    ai = sspr_anim_find(icon);
                    if (g_pickups[pk].manim == 0xFFu && ai != 0xFFu) {
                        u8 fpos = (u8)((g_pickup_anim_tick / lvl_sspr_anim_speed[ai]) % lvl_sspr_anim_len[ai]);
                        u16 fs = sspr_anim_frame_s(ai, fpos);   /* leere Bilder ueberspringen */
                        if (fs != 0u) icon = fs;
                    }
                    spr_draw_s2(g_pickups[pk].oam0, g_pickups[pk].oam1, icon, g_pickups[pk].x, g_pickups[pk].y);
                }
            } else if (g_pickups[pk].oam0 != OAM_NONE) {
                UnsetSprite(g_pickups[pk].oam0);
                UnsetSprite(g_pickups[pk].oam1);
                oam_pool_free(g_pickups[pk].oam0);
                oam_pool_free(g_pickups[pk].oam1);
                g_pickups[pk].oam0 = g_pickups[pk].oam1 = OAM_NONE;
            }
        }
        g_busy_pickups = still;
    }

}

/* Odometer: der angezeigte Score zaehlt in +10-Schritten auf den echten zu ???
   die Ziffern WECHSELN hart (kein Pixel-Rotieren mehr, auf Nutzerwunsch
   zurueckgebaut; die Zehnerstelle klackert trotzdem sichtbar 0-9 durch). */
#define SCORE_ROLL_FRAMES 4u   /* ein +10-Schritt alle 4 Frames (200 Pkt ~ 1.3 s) */
/* Zehnerpotenzen fuer die divisionsfreie Ziffernausgabe, siehe score_draw(). */
static const u32 SCORE_POW10[7] = { 1uL, 10uL, 100uL, 1000uL, 10000uL, 100000uL, 1000000uL };
static void score_roll_update(void) {
    if (g_score_shown == g_score) {
        g_score_roll_p = 0u;
        return;
    }
    g_score_roll_p++;
    if (g_score_roll_p >= (u8)SCORE_ROLL_FRAMES) {
        g_score_roll_p = 0u;
        /* 22.07.2026: u32-Differenz. Der Odometer laeuft nur aufwaerts; faellt
           g_score (Level-Neustart nach Game Over), zieht der Anzeigewert direkt
           nach, statt rueckwaerts zu klackern. */
        if (g_score_shown > g_score) g_score_shown = g_score;
        else if ((u32)(g_score - g_score_shown) >= 10uL) g_score_shown += 10uL;
        else g_score_shown = g_score;
    }
}

// --- Punktestand anzeigen (SPRITES) --
/* Ziffern sind Sprites, keine BG-Tiles mehr (spart 140 VRAM-Tiles fuer die
   frueheren Shift-Varianten ??? Nutzerentscheidung). Riesiger Nebengewinn:
   Sprites sind bildschirmfest, der ganze dy-Sub-Pixel-Zirkus entfaellt fuer
   die Ziffern komplett. Ziffern wechseln HART (Pixel-Rotation entfernt,
   Nutzerwunsch) ??? der Zaehleffekt kommt allein vom +10-Odometer. */
/* Nutzerkorrektur 11.07.2026: Flicker-Multiplexing entfernt ("als HUD echt
   nervig") ??? 7 feste Slots (SPR_DIGIT_0, siehe dortigen Kommentar), keine
   Blink-Umschaltung mehr. Da Position UND Tile pro Stelle nur bei einer
   echten Wertaenderung wechseln, reicht die "letzte Ziffer"-Cache-Technik
   aus draw_sprites() (siehe g_enemy_phys_last_snum) hier sogar aus, um den
   Slot bei unveraenderter Ziffer komplett in Ruhe zu lassen (nicht mal eine
   Positions-Schreibt noetig, die Spalten-Position ist ohnehin fix). */
/* ??5.4 (18.07.2026): Ziffern als BG-Tiles statt Sprites -> spart die 7 OAM-Slots
   (frueher aus dem OAM-Pool). Die Bar sitzt per Raster-Split bildschirmfest, ihre
   Ring-Zeile g_bar_vrow wandert aber beim Scrollen und wird alle 8px per
   bar_draw_at() neu gezeichnet ??? die Ziffern werden deshalb in DIESELBE Ringzeile
   (Spalten 8..2) geschrieben und nach jedem Bar-(Neu)Zeichnen neu aufgesetzt
   (bar_draw_at invalidiert g_score_last_shown). Digit-Grafik = dieselben VRAM-
   Tiles wie die frueheren Ziffern-Sprites (spr_vram(lvl_sspr_a_idx[...])), Palette
   = logische Map-SCR2-Palette 2 (lvl_pal_scr2[2], Ziffernfarben) -> HW-Slot
   scr2_pal_hw[2]=6. Zeigt bis final die Live-FPS (fps_tick); final: g_fps_shown ->
   g_score_shown. */
/* Aufgeschobener Bar-Rahmen-Redraw: laeuft am FRAME-ENDE (vor score_draw), also
   zum gleichen sicheren Zeitpunkt wie die Ziffern (die nie flackern). Behebt das
   restliche "zweite Bar blitzt auf". bar_draw_at invalidiert den Ziffern-Cache ->
   score_draw setzt die Ziffern direkt danach neu auf die (evtl. verschobene) Zeile. */
static void bar_redraw_flush(void) {
    /* RUECKWAERTS-FLACKERN (20.07.2026, Nutzer-HW "Leiste flackert beim
       Zurueckfliegen"): der Terrain-Restore der VERLASSENEN Bar-Zeile darf nicht
       im selben Frame laufen wie der Zeilenwechsel. Die DMA-Split-Tabelle dieses
       Frames wurde im VBlank noch mit der ALTEN Ringzeile gebaut ??? der Beam zeigt
       unten also weiterhin die alte Zeile. restore_terrain_row() schreibt dort
       Terrain rein -> Bar fuer den Rest des Bildes komplett weg (Emulator: exakt
       am 8px-Zeilenwechsel, 1x pro 64 Frames; auf HW als Flackern sichtbar).
       Vorwaerts (g_bar_redraw==1) gab es das nie, weil dort GAR NICHT restauriert
       wird (die verlassene Zeile ueberschreibt spaeter das Streaming).
       Loesung: Restore einen Frame spaeter ??? dann zeigt die Tabelle laengst auf
       die neue Zeile und die alte ist off-screen. */
    if (g_bar_restore_pending != 0xFFu) {
        restore_terrain_row(g_bar_restore_pending);
        g_bar_restore_pending = 0xFFu;
    }
    if (g_bar_redraw == 0u) return;
    if (g_bar_redraw == 2u) g_bar_restore_pending = g_bar_redraw_old;   /* rueckwaerts: alte Zeile wird NAECHSTEN Frame Terrain */
    /* Fester Split (18.07.2026): KEIN Strip-Clear mehr ??? die Bar-Ringzeile liegt
       jetzt eine Zeile tiefer (off-screen im Terrain-Scroll), die Zeile ueber der
       Bar (g_bar_vrow-1) ist sichtbares Terrain bei y=143 und darf NICHT geleert
       werden. Die verlassene Bar-Zeile (vorwaerts, off-screen unten) ueberschreibt
       das Streaming spaeter oben. */
    bar_draw_at(g_bar_vrow);
    g_bar_redraw = 0u;
}
static void score_draw(void) {
#if ENERGY_TEST_MODE
    u16 s = (u16)((u16)g_player.energy * 100u + (u16)g_player.lives);
#elif FPS_PROFILE_MODE
    u16 s = g_vbc_sum;    /* VBlanks je 30 Spielframes: 60 = 30 fps, 90 = 20 fps */
#elif BOSS_TEST_MODE
    /* aktive Pickups * 100 + Boss-HP: zeigt, ob der Cash-Regen beim Tod wirklich
       Token setzt (erwartet 12) und wie schnell sie wieder verfallen. */
    u16 s;
    u8  pc, pi;
#elif ROW_TEST_MODE
    u16 s = (u16)g_scroll_y;   /* Testphase 22.07.: reiner Vorwaerts-Distanzzaehler in px */
#elif CASH_TEST_MODE
    u16 s = (u16)g_cash;  /* Testphase Shop: gesammeltes Geld statt FPS -> Preise gegen echte Einnahmen pruefen */
#elif WORM_TEST_MODE
    u16 s = g_cur_wave;   /* Testphase: aktuelle Wellen-ID (Spawn-Index 0-112) statt FPS -> zum Melden welche Welle falsch ist */
#else
    /* 22.07.2026: Standard ist jetzt der PUNKTESTAND. Bis heute zeigte auch der
       Normalfall g_fps_shown - der Kommentar bei score_draw sagte seit dem
       18.07. "final: g_fps_shown -> g_score_shown", umgestellt wurde es nie.
       Folge: der Punktestand war nie zu sehen, weshalb auch der ueberlaufende
       Score-Faktor (siehe spawn_killed) niemandem auffallen konnte. FPS gibt es
       weiterhin, aber ueber FPS_PROFILE_MODE. */
    u32 s = g_score_shown;
#endif
    u8 digit[7];
    u8 d, top;
    u32 v;
    u8  k;

#if BOSS_TEST_MODE
    /* C89: erst alle Deklarationen, dann Anweisungen - deshalb hier und nicht oben. */
    pc = 0u;
    for (pi = 0u; pi < (u8)MAX_PICKUPS; pi++) if (g_pickups[pi].active) pc++;
    s = (u16)((u16)pc * 100u + (u16)g_boss.hp);
#endif

    /* Nur neu zeichnen, wenn sich die Zahl geaendert hat ODER die Bar-Zeile
       gerade (neu) gezeichnet wurde (bar_draw_at hat den Cache invalidiert).
       22.07.2026: der Diagnosewert der beiden linken Stellen (FPS bzw.
       Palettenfehler) muss in den Vergleich mit hinein - sonst blieb er
       stehen, solange sich der Punktestand nicht aendert, und man liest eine
       veraltete Bildrate ab. */
#if FPS_IN_SCORE
    {
        u16 aux;
#if PAL_SELFCHECK && PAL_SHOW_ON_OPTION
        /* KORREKTUR 22.07.2026: hier stand g_pal_worst, also das MAXIMUM seit
           Levelstart. Damit blieb die Zahl stehen, auch wenn die Paletten laengst
           wieder stimmten - der Nutzer sah nach dem Shop korrekte Farben und
           trotzdem den alten Hoechstwert. Jetzt der AKTUELLE Stand des letzten
           Durchlaufs; als Hoechstwert dient g_pal_worst nur noch intern. */
        /* 22.07.2026: OPTION zeigt jetzt die Zahl der NACHLADUNGEN, nicht den
           Momentanstand. Der ist mit eingeschalteter Selbstheilung naemlich fast
           immer 0 (repariert wird ja sofort) und sagt daher nichts. Die Zahl der
           Heilungen unterscheidet dagegen genau das, was wir wissen muessen:
             1 (und bleibt 1) -> einmalige Verfaelschung beim Erst-Upload
             steigt weiter    -> die Palette wird laufend zerschossen */
        aux = (u16)g_fps_avg;   /* Durchschnitt - siehe fps_tick() */
#if PAL_SELFCHECK && PAL_SHOW_ON_OPTION && !PROFILE_MODE
        /* Im PROFILE_MODE belegt OPTION die Blockauswahl - dann keine
           Doppelbelegung, die FPS muessen waehrend des Umschaltens lesbar sein. */
        if (g_pad & J_OPTION) aux = g_pal_heals;
#endif
#else
        aux = (u16)g_fps_avg;   /* Durchschnitt - siehe fps_tick() */
#endif
#if PROFILE_MODE || FPS_VBC_DISPLAY
        /* Die Anzeige muss auch dann nachziehen, wenn sich weder Punktestand
           noch FPS aendern - der VBlank-Summenwert tut es. */
#if PROFILE_MODE
        if (s == g_score_last_shown && aux == g_score_aux_last
            && g_vbc_sum == g_prof_last_vbc && g_prof_sel == g_prof_last_sel) return;
        g_prof_last_sel = g_prof_sel;
#else
        if (s == g_score_last_shown && aux == g_score_aux_last
            && g_vbc_sum == g_prof_last_vbc) return;
#endif
        g_prof_last_vbc = g_vbc_sum;
#else
        if (s == g_score_last_shown && aux == g_score_aux_last) return;
#endif
        g_score_aux_last = aux;
    }
#else
    if (s == g_score_last_shown) return;
#endif
    g_score_last_shown = s;

    /* Ziffern OHNE Division: die Toolchain kann 32-Bit weder teilen noch
       modulo rechnen (C9H_divlu/C9H_remlu fehlen beim Linken). Fortlaufende
       Subtraktion je Stelle - hoechstens 9 Durchlaeufe pro Stelle, und das
       auch nur, wenn sich die Zahl ueberhaupt geaendert hat (Cache oben). */
    v = (u32)s;
    if (v > (u32)SCORE_MAX) v = (u32)SCORE_MAX;   /* 7 Stellen, dann stehenbleiben */
    for (d = 7u; d > 0u; d--) {
        u32 p = SCORE_POW10[d - 1u];
        k = 0u;
        while (v >= p) { v -= p; k++; }
        digit[d - 1u] = k;
    }
#if FPS_IN_SCORE
    /* Die beiden LINKEN Stellen als eigenes Feld: Punktestand nutzt dann noch
       fuenf Stellen (bis 99999), links steht der Diagnosewert. */
    {
        u16 aux = g_score_aux_last;   /* FPS, oder bei gehaltenem OPTION die
                                         Zahl der abweichenden Farbwoerter */
        if (aux > 99u) aux = 99u;
        digit[6] = (u8)(aux / 10u);
        digit[5] = (u8)(aux % 10u);
#if PROFILE_MODE
        /* Anzeige im Profiler:  [FPS][FPS] [Block][Block] [VBlanks je 30 Frames]
           Die FPS-Zahl (60/schlechtester Frame) kennt nur 30/20/15 - viel zu grob,
           um kleine Verbesserungen zu sehen. Die drei rechten Stellen zeigen
           deshalb g_vbc_sum: die SUMME der VBlanks ueber 30 Spielframes.
             060 = saubere 30 fps      090 = durchgehend 20 fps
           Alles dazwischen ist linear ablesbar, z.B. 072 = jeder dritte Frame
           braucht einen VBlank mehr. Damit laesst sich der Beitrag jedes Blocks
           beziffern statt nur "besser/schlechter" zu sagen. */
        digit[4] = (u8)(g_prof_sel / 10u);
        digit[3] = (u8)(g_prof_sel % 10u);
#elif FPS_VBC_DISPLAY
        /* 29.07.2026 (Nutzerwunsch): links steht jetzt der DURCHSCHNITT der
           letzten ~30 s (g_vbc_avg30, drei Stellen) statt der groben fps-Zahl,
           rechts weiter der aktuelle Fensterwert:  [avg avg avg] 0 [vbc vbc vbc] */
        { u16 av = g_vbc_avg30; if (av > 999u) av = 999u;
          digit[6] = (u8)(av / 100u);
          digit[5] = (u8)((av / 10u) % 10u);
          digit[4] = (u8)(av % 10u); }
        digit[3] = 0u;
#endif
#if PROFILE_MODE || FPS_VBC_DISPLAY
        { u16 vs = g_vbc_sum; if (vs > 999u) vs = 999u;
          digit[2] = (u8)(vs / 100u);
          digit[1] = (u8)((vs / 10u) % 10u);
          digit[0] = (u8)(vs % 10u); }
#endif
        /* Stellen 0..4 bleiben der Punktestand - die oben berechneten Ziffern 5/6
           des Score-Werts werden bewusst ueberschrieben. */
    }
#endif
    /* Fuehrende Nullen weglassen (unterdrueckte Stellen zeigen das Bar-Tile). */
    top = 6u;
    while (top > 0u && digit[top] == 0u) top--;

    for (d = 0; d < 7; d++) {
        u8 col = (u8)(8u - d);   /* Spalten 8..2, units rechts */
        if (d <= top) {
            /* HW-Palette = scr2_pal_hw[2] (=6): dort liegt die logische Map-Palette
               lvl_pal_scr2[2] = {0x0DBD,0x0B99,0x0A77}, die EXAKT den Ziffernfarben
               (Sprite-Pal 0) entspricht. NICHT HW-Slot 2 nehmen ??? den ueberschreibt
               build_bar_assets mit dem dunklen "warm" der Bar (Bugfix 18.07.2026,
               Nutzerhinweis: Ziffern hatten faelschlich die dunkle Bar-/SCR1-Optik). */
            put_cell(SCR_2_PLANE, g_digit_pal_hw, col, g_bar_vrow,
                     spr_vram(lvl_sspr_a_idx[(u16)(SPR_S_DIGIT0 - 1u) + digit[d]]), 0u);
        } else {
            /* unterdrueckte Stelle: Bar-Tile dieser Spalte wiederherstellen */
            u8 bi = g_bar_col[col];
            put_cell(SCR_2_PLANE, barPal[bi], col, g_bar_vrow,
                     (u16)(TILE_BAR_BASE + bi), 0u);
        }
    }
}

/* Nutzerwunsch 14.07.2026: kurzer Paletten-Umkehr-Flash beim Schiffstreffer,
   siehe g_hit_flash-Kommentar bei ship_draw_meta. Einmal pro Frame aus der
   Hauptschleife aufgerufen. Der eigentliche Start (Paletten scannen+
   invertieren) passiert erst hier, nicht in player_hit() ??? dort sind
   tilt_by_level/lvl_meta_off noch nicht deklariert (C89, keine
   Vorwaertsdeklaration fuer file-scope Arrays). */
static void hit_flash_update(void) {
    /* 23.07.2026 NEUAUFBAU: Der alte Paletten-Umkehr-Blitz (inhaltliche Inversion
       der GETEILTEN Sprite-Paletten) wurde 18.07. entfernt, weil er auch Gegner
       traf. Der neue Treffer-Flash laeuft komplett ueber die OAM-Palettenzuordnung
       (dedizierter Slot FLASH_PAL, pro Sprite) im Zeichencode - Schiff/Waffen im
       draw_sprites-Player-Block, Gegner im enemy-/meta-Draw. Diese Funktion darf
       g_hit_flash NICHT mehr anfassen (das erledigt der Zeichencode: setzen bei
       Schaden, herunterzaehlen beim Zeichnen). Nur die toten Alt-Felder raeumen. */
    g_hit_flash_pending = 0u;
    g_hit_flash_pal_cnt = 0u;
}

/* OAM-Vollreset (22.07.2026 aus game_start() herausgeloest, damit respawn_do()
   dasselbe tun kann). Nutzerbericht: "nach dem Tod kann ich manchmal nicht
   schiessen" - genau das Bild, das ein verlorener OAM-Slot erzeugt: die Schuesse
   entstehen (nachgemessen: gleich viele wie ohne Tod), bekommen aber keinen Slot
   mehr und sind unsichtbar. respawn_do() hat sich bisher darauf verlassen, dass
   jeder Zeichenpfad seinen Slot beim Deaktivieren selbst zurueckgibt - eine
   Annahme ueber ein Dutzend Stellen hinweg, von denen eine (die Wandwurm-Baelle)
   sie schon verletzt hatte. Hier wird sie gar nicht mehr gebraucht.
   REIHENFOLGE IST PFLICHT: erst oam_pool_init(), dann JEDE Instanz auf OAM_NONE -
   sonst gibt draw_sprites() einen gerade neu freigegebenen Slot ein zweites Mal
   frei und der Freelist-Stack laeuft ueber sein Array-Ende hinaus. */
static void oam_reset_all(void) {
    u8 i;
    /* 25.07.2026 Frueh-Ausstieg: nach einem harten Reset (Respawn/Shop/Levelstart)
       JEDES System einmal zwangsweise durchlaufen lassen. Die Flags koennten sonst
       auf 0 stehen, waehrend Instanzen noch Slots halten - das Aufraeumen bliebe
       aus. Ein ueberfluessiger Durchlauf kostet einen Frame, ein ausgelassener
       hinterlaesst Geister. */
    g_busy_bullets = g_busy_mobullets = g_busy_ebullets = g_busy_enemies = 1u;
    g_busy_metas = g_busy_worms = g_busy_wworms = g_busy_pickups = 1u;
    g_busy_wpbul = g_busy_wpent = 1u;
    g_busy_wcrawl = 1u;
    for (i = 0; i < 64;          i++) UnsetSprite(i);
    /* Nutzerkorrektur 14.07.2026 (OAM-Pool-Umbau): Pool komplett neu
       auffuellen (alle 48 Slots wieder frei) UND jede Instanz, die noch
       einen Slot aus dem VORIGEN Spiel im Struct stehen hat, explizit auf
       OAM_NONE zuruecksetzen ??? sonst wuerde draw_sprites() beim naechsten
       Frame versuchen, einen laengst durch oam_pool_init() neu freigegebenen
       Slot ein ZWEITES Mal freizugeben (doppelter Eintrag im Freelist-Stack,
       g_oam_pool_n liefe unbemerkt ueber das Array-Ende hinaus). Alle
       Instanzen sind zu diesem Zeitpunkt ohnehin gleich darunter auf
       active=0 gesetzt (Positions-only-Fastpath wird durch last_snum=0/
       oam=OAM_NONE ebenfalls sauber erzwungen, kein separater Reset noetig). */
    oam_pool_init();
    g_ship_last_meta = 0xFFu;   /* dito fuers Schiff, siehe ship_hide()-Kommentar */
    g_hit_flash = 0u;           /* 23.07.2026: kein stehengebliebener Treffer-Flash nach Respawn/Neustart */
    g_thrust_last_s = 0u;
    g_thrust_last_back = 0xFFu;
    g_thrust_oam0 = g_thrust_oam1 = OAM_NONE;
    /* 27.07.2026: Module der Waffen 1..n. PFLICHT hier, nicht nur als statische
       Nullinitialisierung - OAM_NONE ist 0xFF, ein genulltes Array hiesse also
       "Slot 0 belegt" und der erste Durchlauf gaebe fremde Slots frei. */
    { u8 w, c;
      for (w = 0u; w < (u8)LVL_WEAPON_COUNT; w++) {
          g_wpx_n[w] = 0u;
          for (c = 0u; c < (u8)WPX_CELLS; c++) {
              g_wpx_oam[w][c][0] = OAM_NONE;
              g_wpx_oam[w][c][1] = OAM_NONE;
              g_wpx_last[w][c] = 0u;
          }
      } }
    for (i = 0; i < MAX_BULLETS; i++) { g_bullets[i].oam = OAM_NONE; g_bullets[i].last_snum = 0u; }
    /* 28.07.2026 Wandkriecher: dieselbe Pflicht wie bei den Waffenmodulen -
       OAM_NONE ist 0xFF, ein genulltes Array hiesse "Slot 0 belegt". Nur die
       Slots, NICHT alive/spent: der Levelzustand ueberlebt Respawn und Shop
       (ein abgeschossener Kriecher bleibt abgeschossen). */
    { u8 wi, wc;
      for (wi = 0u; wi < (u8)LVL_WCRAWL_COUNT; wi++) {
          g_wcrawlers[wi].cells = 0u;
          g_wcrawlers[wi].on    = 0u;
          g_wcrawlers[wi].ring  = WCRAWL_RING_NONE;
          for (wc = 0u; wc < (u8)WCRAWL_CELLS; wc++) {
              g_wcrawlers[wi].oam[wc][0] = OAM_NONE;
              g_wcrawlers[wi].oam[wc][1] = OAM_NONE;
              g_wcrawlers[wi].last_snum[wc] = 0u;
          }
      } }
    UnsetSprite(SPR_BULLET_0); UnsetSprite((u8)(SPR_BULLET_0 + 1u));   /* alte feste Bullet-Slots 14-15 jetzt ungenutzt (Vorwaerts-Schuesse -> Pool, 19.07.2026) */
    g_wpmod_last_s = 0u;
    g_wpmod_oam0 = g_wpmod_oam1 = OAM_NONE;
    for (i = 0; i < MAX_ENEMIES; i++) {
        g_enemies[i].oam0 = g_enemies[i].oam1 = OAM_NONE;
        g_enemies[i].last_snum = 0u;
    }
    for (i = 0; i < MAX_METAENEMIES; i++) {
        u8 c;
        for (c = 0u; c < (u8)META_CELLS; c++) {
            g_metaenemies[i].oam[c] = OAM_NONE;
            g_metaenemies[i].oam_b[c] = OAM_NONE;   /* b-Overlay-Slot, 21.07.2026 */
            g_metaenemies[i].last_snum[c] = 0u;
        }
    }
    for (i = 0; i < MAX_WORMS; i++) {
        u8 k;
        for (k = 0u; k < (u8)MAX_WORM_SEGS; k++) {
            g_worms[i].seg[k].oam = OAM_NONE;
            g_worms[i].seg[k].last_snum = 0u;
            g_worms[i].seg[k].last_flip = 0xFFu;
        }
    }
    /* Schritt 19: Wandwuermer + Baelle zuruecksetzen (oam explizit OAM_NONE, sonst
       Doppel-Free wie beim Schritt-12-Wurm, siehe OAM-Pool-Kommentar). */
    for (i = 0; i < (u8)WALLWORM_SLOTS; i++) {
        u8 k;
        g_wallworms[i].active = 0u; g_wallworms[i].part_count = 0u;
        g_wallworms[i].head_alive = 0u; g_wallworms[i].emerged = 0u;
        for (k = 0u; k < (u8)WALLWORM_SEGMENTS; k++) {
            g_wallworms[i].part[k].oam = OAM_NONE;
            g_wallworms[i].part[k].alive = 0u;
            g_wallworms[i].part[k].last_snum = 0u;
            g_wallworms[i].part[k].last_flip = 0xFFu;
        }
    }
    for (i = 0; i < (u8)WALLWORM_BALLS; i++) {
        g_wallworm_balls[i].active = 0u;
        g_wallworm_balls[i].oam = OAM_NONE;
    }
    g_wallworm_div = 0u;
    g_wallworm_spawn_cd = 0u;
#if WORM_TEST_MODE
    g_paused = 0u;
#endif
    for (i = 0; i < MAX_MAPOBJ_BULLETS; i++) {
        g_mo_bullets[i].oam = OAM_NONE;
        g_mo_bullets[i].last_snum = 0u;
    }
    /* Schritt 18: oam MUSS explizit auf OAM_NONE, nicht nur active=0 ??? sonst
       gaebe draw_sprites() im naechsten Frame einen vom vorigen Spiel
       uebriggebliebenen, von oam_pool_init() laengst neu freigegebenen Slot ein
       ZWEITES Mal frei (doppelter Eintrag im Freelist-Stack). Siehe
       OAM-Pool-Kommentar zum game_start()-Reset. */
    for (i = 0; i < (u8)MAX_ENEMY_BULLETS; i++) g_ebullets[i].oam = OAM_NONE;
    for (i = 0; i < MAX_WPBULLETS; i++) g_wp_bullets[i].oam = OAM_NONE;
    g_bomb.active = 0u; g_bomb.oam = OAM_NONE;   /* 23.07.2026: Sonderwaffen-Entities */
    g_laser_on = 0u; { u8 ls; for (ls = 0u; ls < (u8)LASER_SEGS; ls++) g_laser_oam[ls] = OAM_NONE; }
    g_electro.active = 0u; g_electro.oam = OAM_NONE;
    g_minepod.active = 0u; g_minepod.oam = OAM_NONE;
    { u8 mk; for (mk = 0u; mk < (u8)MINE_MAX; mk++) { g_mines[mk].active = 0u; g_mines[mk].oam = OAM_NONE; } }
    for (i = 0; i < MAX_PICKUPS; i++) g_pickups[i].oam0 = g_pickups[i].oam1 = OAM_NONE;
}

// --- Spielstart / Neustart --
static void game_start(void) {
    u8 i;
    oam_reset_all();
    vbc_stats_reset();   /* 29.07.2026: Messzaehler + Durchschnitts-Ring nullen (Pflicht, siehe dort) */
    g_score_last_shown = 0xFFFFu;   /* ??5.4: Ziffern-Tile-Cache invalidieren -> naechster score_draw setzt neu */
    /* Sicherheitsnetz: hit_flash_update() laeuft nur waehrend STATE_PLAY ???
       stirbt der Spieler waehrend die letzten Flash-Frames noch liefen,
       wuerde die invertierte Palette sonst dauerhaft haengen bleiben (auch
       ins naechste Spiel hinein). Bei jedem game_start() etwaige noch
       invertierte Paletten hart zuruecksetzen. */
    if (g_hit_flash_pal_cnt > 0u) {
        u16 *spal = (u16*)0x8200;
        u8 j, w;
        for (j = 0u; j < g_hit_flash_pal_cnt; j++)
            for (w = 1u; w < 4u; w++)
                spal[(u16)g_hit_flash_pal[j] * 4u + w] = g_hit_flash_orig[j][w];
    }
    g_hit_flash = 0u;
    g_hit_flash_pending = 0u;
    g_hit_flash_pal_cnt = 0u;
    for (i = 0; i < MAX_BULLETS; i++) g_bullets[i].active = 0;
    for (i = 0; i < MAX_ENEMIES; i++) g_enemies[i].active = 0;
    for (i = 0; i < MAX_METAENEMIES; i++) g_metaenemies[i].active = 0;
    for (i = 0; i < MAX_WORMS; i++) g_worms[i].active = 0;
    /* 25.07.2026 (Nutzer "am Levelanfang lauter Wurmloecher"): Wandwuermer UND
       ihre Loch-Anim-Skripte fehlten hier - sie ueberlebten den Level-Neustart
       (Endlosschleife) und stempelten danach Loch-Kacheln in fremdes Terrain. */
    for (i = 0; i < (u8)WALLWORM_SLOTS; i++)   g_wallworms[i].active = 0;
    for (i = 0; i < (u8)WALLWORM_BALLS; i++)   g_wallworm_balls[i].active = 0;
    wormhole_anims_reset();
    for (i = 0; i < MAX_MAPOBJ_BULLETS; i++) g_mo_bullets[i].active = 0;
    for (i = 0; i < (u8)MAX_ENEMY_BULLETS; i++) g_ebullets[i].active = 0;   /* Schritt 18 */
    for (i = 0; i < MAX_WPBULLETS; i++) g_wp_bullets[i].active = 0;
    for (i = 0; i < MAX_PICKUPS; i++) g_pickups[i].active = 0;
    /* Gruppen-Zaehler (Schritt 14) aus lvl_spawn_group/lvl_spawn_count
       aufbauen: Restanzahl lebender Gegner je Gruppe, bis die zugehoerige
       Belohnung spawnt (siehe enemy_killed/pickup_spawn_for_group). */
    {
        u8 g, sp;
        for (g = 0u; g < (u8)GROUP_COUNT_SLOTS; g++) g_group_count[g] = 0u;
        for (sp = 0u; sp < (u8)LVL_SPAWN_COUNT; sp++) {
            u8 grp = lvl_spawn_group[sp];
            if (grp) g_group_count[grp] = (u8)(g_group_count[grp] + lvl_spawn_count[sp]);
        }
    }
    g_thrust_tick = 0u; g_thrust_frame = 0u; g_thrust_dir = 0u;
    g_wpmod_tick = 0u; g_wpmod_frame = 0u;
    /* Performance-Fix 11.07.2026: lvl_weapon_spr[0] ist waehrend des
       gesamten Spiels konstant, der sspr_anim_index_for()-Treffer aendert
       sich also nie ??? einmalig hier statt bei jedem Feuer-Frame in
       weapon_update() UND nochmal in draw_sprites() neu suchen (siehe
       g_wpmod_aidx-Kommentar bei der Deklaration). */
    g_wpmod_aidx = sspr_anim_index_for((u16)(lvl_weapon_spr[0] & 0x01FFu));
    /* 27.07.2026: Modul-Bildzahl und Projektil-Anim je Waffe EINMAL aufloesen,
       siehe g_wp_alen/g_wp_bai. Danach liest der Zeichenpfad nur noch RAM. */
    { u8 aw;
      for (aw = 0u; aw < (u8)LVL_WEAPON_COUNT; aw++) {
          u16 bs = lvl_weapon_bullet_spr[aw];
          g_wp_alen[aw] = wp_anim_len(aw);
          if ((bs & 0x01FFu) == 0u) bs = lvl_weapon_bullet_spr[0];
          g_wp_bai[aw] = (bs & 0x4000u) ? sspr_anim_find((u16)(bs & 0x01FFu)) : 0xFFu;
      } }
    g_flicker = 0u;

    g_scr1_y     = 0;
    g_scroll_tick = 0u;
    {
        u8 tx;
        for (tx = 0; tx < 20; tx++) {
            g_bar_col[tx]  = barMapDef[tx];
        }
    }
    {
        u8 ay, ax;
        for (ay = 0u; ay < (u8)ANIM_GRID_ROWS; ay++)
            for (ax = 0u; ax < (u8)ANIM_GRID_COLS; ax++) {
                g_anim_grid[ay][ax]   = 0u;
                g_mapobj_grid[ay][ax] = 0u;
            }
        for (ay = 0u; ay < (u8)LVL_ANIM_COUNT; ay++) {
            g_anim_tick[ay]  = 0u;
            g_anim_frame[ay] = 0u;
            g_anim_cell_count[ay] = 0u;
        }
        for (ay = 0u; ay < 32u; ay++) g_row_map[ay] = (u16)LVL_MAP_H;  /* noch unbeschrieben */
    }

    /* 28.07.2026: Wandkriecher VOR lvl1_prefill() aufsetzen - prefill schreibt
       19 Zeilen und ruft dabei wcrawl_row_in(), das auf alive/ring zugreift. */
    wcrawls_init();
    /* 28.07.2026: ebenfalls VOR dem ersten anim_update() - sonst taktet der
       Auto-Loop eine getaktete Anemone einmal mit. */
    mapobj_gate_init();

    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);
    /* BUGFIX 28.07.2026 (Nutzerbericht "die Farbe der Tiles stimmt nicht",
       Gestein orange statt grau): DIESE ZEILE MUSS VOR lvl1_select_segment()
       STEHEN. lvl1_select_segment() ruft tile_words_build(), und das rechnet
       fuer JEDE Kachel das fertige Tilemap-Wort aus - inklusive
       g_scr2_pal_map[a_p]. Die Tabelle wurde aber erst in build_lvl1() gefuellt,
       eine Zeile spaeter. Beim ersten Aufbau stand sie also auf lauter Nullen:
       jede Kachel bekam HW-Palette 0, und das ist im Export vom 28.07. die
       orange. Ab dem ersten Abschnittswechsel (Zeile 125) baut
       lvl1_select_section() die Woerter neu und es stimmte wieder - deshalb war
       nur der ANFANG des Levels falsch, was die Suche lange in die Irre
       gefuehrt hat (spaete Screenshots sahen richtig aus).
       Der Fehler kam mit g_scr2_pal_map am 28.07.; davor stand an der Stelle
       das const-Array scr2_pal_hw[], das keine Initialisierung braucht. */
    scr2_pal_map_init();
    lvl1_select_segment(0u);   /* Segment A: Zeilen 0-195 */
    build_lvl1(0u);   /* leeres VRAM -> voller Aufbau, auch den gemeinsamen Block */

    /* Initialen Sichtbereich vorbelegen: Map-Zeilen 0..18 auf Tilemap-Zeilen 18..0.
       NGPC-Bildschirm = 152px = 19 Tile-Reihen. g_scr1_y=0 -> Tilemap unverdreht.
       Tilemap-Zeile 18 (unten) = Map-Zeile 0 (Spielstart, leer).
       Tilemap-Zeile 0 (oben)   = Map-Zeile 18 (Terrain sichtbar). */
    lvl1_prefill(0u);
    build_bar_assets();
    g_bar_vrow = 19u;      /* +1 (fester Split): Bar eine Ringzeile tiefer; Zeile 18 = Map-Zeile 0 bleibt sichtbares Terrain bei y=143 */
    g_bar_redraw = 0u; g_bar_restore_pending = 0xFFu;   /* kein Rest aus dem letzten Durchlauf */
    bar_draw_at(g_bar_vrow);
    build_bg();

    player_init();
    g_score       = 0;
    g_score_shown = 0;
#if SCORE_TEST_VALUE
    g_score = (u32)SCORE_TEST_VALUE; g_score_shown = (u32)SCORE_TEST_VALUE;
#endif
#if WEAPON_TEST_MASK
    g_player.weapons_active = (u16)WEAPON_TEST_MASK;   /* siehe WEAPON_TEST_MASK */
#endif
#if PAL_FAULT_TEST
    /* Absichtliche Verfaelschung, siehe PAL_FAULT_TEST */
    { u8 fq; u16 *sp = (u16*)0x8200;
      for (fq = 0u; fq < (u8)PAL_FAULT_TEST; fq++) sp[(u16)fq * 4u + 1u] ^= 0x0F0Fu; }
#endif
    g_cash        = 0u;   /* Shop-Waehrung, siehe g_cash */
    g_score_roll_p = 0u;
    g_scroll_y   = 0;     /* reiner Distanz-Zaehler, keine Spawn-Abhaengigkeit mehr */
    {
        u8 sp;
        for (sp = 0u; sp < (u8)LVL_SPAWN_COUNT; sp++) {
            /* 23.07.2026: ALLE Wellen ungefeuert starten. Den Start-Burst
               verhindert jetzt die Vorlauf-RAMPE im Trigger-Scan (die Spawn-Linie
               steigt am Levelstart vom Schiff auf +12), nicht mehr ein Vorfeuern -
               so bleiben die Startwellen erhalten und kommen gestaffelt. */
            g_spawn_fired[sp] = 0u;
            g_spawn_left[sp]  = 0u;
            g_spawn_timer[sp] = 0u;
        }
    }
    g_spawn_scroll_row    = 0u;   /* Fix 16.07.2026: Levelstart = Trigger-Zeile 0 */

    g_spawn_pending       = 0u;   /* siehe g_spawn_pending */
    g_spawn_row_changed   = 1u;   /* erster Frame prueft, siehe g_spawn_row_changed */
    g_spawn_row_frame_acc = 0u;
    {
        u8 mo;
        for (mo = 0u; mo < (u8)LVL_MAPOBJ_COUNT; mo++) g_mapobj_wilted[mo] = 0u;
    }
    g_tilt_level = 0;
    g_tilt_timer = g_tune_tilt;   /* geprimed: erster Tilt spricht sofort an */
    g_move_acc    = 0u;
    g_move_step   = 0u;
    g_coast_x = 0u; g_coast_y = 0u;
    g_last_dir_x = 0; g_last_dir_y = 0;
    g_back_acc    = 0u;
    g_back_speed  = 0u;
    g_back_accel_tick = 0u;
    g_back_budget = BACK_BUDGET_MAX;
    g_back_px     = 0u;   /* am Levelanfang gibt es nichts, wohin man zurueck koennte */
    g_shop_a_count = 0u;
    g_shop_entered = 0u;
    { u8 st; for (st = 0u; st < (u8)LVL_SHOP_TRIGGER_COUNT; st++) g_shop_fired[st] = 0u; }
    g_shop_delay   = 0u;
    g_checkpoint = 0u; g_checkpoint_seen = 0u; g_respawn_pending = 0u;
    /* 23.07.2026: Loadout-Schnappschuss auf den Startzustand setzen, damit ein
       Tod vor dem ersten Checkpoint sauber auf den Levelanfang zurueckfaellt. */
    g_cp_cash = 0u; g_cp_weapons = g_player.weapons_active; g_cp_power = 0u;
    g_boss_rain = 0u; g_nashwan_timer = 0u;
    boss_reset();
    g_state      = STATE_PLAY;
    music_start_theme();   /* Titelthema bei Spielstart von vorne (auch via level_loop_restart) */
#if WARP_CHECKPOINT >= 0
    if ((u8)WARP_CHECKPOINT < (u8)LVL1_CHECKPOINT_COUNT)   /* leere Tabelle -> kein Warp */
#endif
#if WARP_CHECKPOINT >= 0
    {
    /* Testphase 22.07.2026: direkt an einem Checkpoint starten, statt das halbe
       Level abzufliegen. Nutzt bewusst respawn_do() (laeuft am Frame-Ende, weil
       es VRAM schreibt) ??? damit prueft derselbe Schalter gleich den Wieder-
       einstiegspfad mit. Ohne das ist die Boss-Arena im Emulator praktisch
       unerreichbar: Dauer-UP scrollt wegen des Rueckwaerts-Budgets nur halb so
       schnell vorwaerts und das Schiff verkeilt sich irgendwann im Terrain. */
    /* 22.07.2026: nur beim ERSTEN game_start(). Sonst warpt die Level-Schleife
       (level_loop_restart -> game_start) sofort wieder in die Boss-Arena zurueck -
       genau daran ist der erste Test der Schleife gescheitert, nicht am Code. */
    { static u8 warp_done;
      if (!warp_done) { warp_done = 1u;
          g_checkpoint = (u8)WARP_CHECKPOINT; g_checkpoint_seen = 1u; g_respawn_pending = 1u; } }
    }
#endif
#if BENCH_DETERM_WORM
    /* Wurmband-Benchmark: respawn_do() erzwingt Zeile 113 (Warp scheitert, Map ohne
       Checkpoints) - hier nur EINMAL den Wiedereinstieg anstossen. */
    { static u8 ww_warp; if (!ww_warp) { ww_warp = 1u; g_respawn_pending = 1u; } }
#endif
#if BENCH_META
    /* Metasprite-Benchmark: respawn_do() erzwingt das Scroll-Ende (s.o.) - hier
       nur EINMAL den Wiedereinstieg anstossen (wie beim Wurmband-Benchmark). */
    { static u8 bm_warp; if (!bm_warp) { bm_warp = 1u; g_respawn_pending = 1u; } }
#endif
#if WCRAWL_TEST
    /* Der Warp springt ueber den Shop-Ausloeser bei Zeile 123 - ohne das landet
       der Test im Katalog statt im Level. */
    { static u8 wc_warp; if (!wc_warp) { wc_warp = 1u; g_respawn_pending = 1u;
        { u8 st; for (st = 0u; st < (u8)LVL_SHOP_TRIGGER_COUNT; st++) g_shop_fired[st] = 1u; } } }
#endif
#if TEST_WARP_ROW
    { static u8 tw_warp; if (!tw_warp) { tw_warp = 1u; g_respawn_pending = 1u;
        { u8 st; for (st = 0u; st < (u8)LVL_SHOP_TRIGGER_COUNT; st++) g_shop_fired[st] = 1u; } } }
#endif
}

/* Einmalig beim Uebergang in STATE_SHOP: Screen leeren, Platzhaltertext
   zeigen. Die eigentliche VRAM-Umschaltung passiert erst bei shop_resume()
   (2. A-Druck) ??? bis dahin bleibt Segment A's Terrain-VRAM unangetastet. */
/* ECHTER Shop-Bildschirm (20.07.2026) ??? ersetzt den alten Platzhaltertext
   ("SHOP / COMING SOON / PRESS A").
   Der Shop bringt einen KOMPLETT eigenen Kachelsatz mit (lvl_shop_tile_data,
   eigener Ladevorgang laut Export-Spec) und ueberschreibt dafuer das gesamte
   Character-RAM ab Slot 0. Das ist unkritisch, weil shop_resume() danach das
   Terrain ohnehin neu aufbaut ??? ABER Sprites liegen im selben Speicher, das
   Schiff wuerde also aus Shop-Kacheln zusammengesetzt. Deshalb erst alle OAM-
   Eintraege abschalten.
   Kachel i der Shop-Daten landet auf VRAM-Slot i, damit lvl_shop_tile_a/b_idx
   unveraendert als Slot-Nummer taugt. */
static void shop_screen_draw(void) {
    volatile u16 *p;
    volatile u16 *dst;
    const u16 *src;
    u16 i, w, cell;
    u8  r, c;

    /* ship_hide() statt roher UnsetSprite-Schleife: es setzt zusaetzlich
       g_ship_last_meta = 0xFF und erzwingt damit beim naechsten ship_draw_meta()
       einen VOLLEN Redraw. Ohne das denkt der Positions-only-Fastpath, das Schiff
       stehe noch, schreibt nur Koordinaten ??? und das Schiff bleibt unsichtbar. */
    ship_hide();
    for (i = (u16)(SHIP_CELLS * 2u); i < 64u; i++) UnsetSprite((u8)i);   /* Rest: Gegner, Schuesse, Ziffern */

    /* Shop-Paletten: a-Ebene -> SCR2, b-Ebene -> SCR1. Index 0 jeder Palette
       ist laut Export die Shop-Hintergrundfarbe; auf Scroll-Ebenen ist Farbe 0
       aber immer transparent, deshalb wie beim Terrain 0x0000 schreiben. */
    p = SCROLL_2_PALETTE;
    for (i = 0u; i < (u16)LVL_SHOP_PAL_SCR2_COUNT && i < 16u; i++) {
        p[i * 4u + 0u] = 0x0000u;
        p[i * 4u + 1u] = lvl_shop_pal_scr2[i][1];
        p[i * 4u + 2u] = lvl_shop_pal_scr2[i][2];
        p[i * 4u + 3u] = lvl_shop_pal_scr2[i][3];
    }
    p = SCROLL_1_PALETTE;
    for (i = 0u; i < (u16)LVL_SHOP_PAL_SCR1_COUNT && i < 16u; i++) {
        p[i * 4u + 0u] = 0x0000u;
        p[i * 4u + 1u] = lvl_shop_pal_scr1[i][1];
        p[i * 4u + 2u] = lvl_shop_pal_scr1[i][2];
        p[i * 4u + 3u] = lvl_shop_pal_scr1[i][3];
    }

    /* Kacheldaten 1:1 nach VRAM (Slot == Index) */
    for (i = 0u; i < (u16)LVL_SHOP_TILE_DATA_COUNT; i++) {
        dst = (volatile u16*)0xA000u + i * 8u;
        src = &lvl_shop_tile_data[i][0];
        for (w = 0u; w < 8u; w++) dst[w] = src[w];
    }

    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);

    /* Festes 20x19-Raster, kein Scrollen. Bit 15 = H-Flip, Bits 0-8 = Kachel-
       nummer (1-basiert) in lvl_shop_tile_a/b_idx. 0 = leere Zelle. */
    for (r = 0u; r < (u8)LVL_SHOP_MAP_H; r++) {
        for (c = 0u; c < (u8)LVL_SHOP_MAP_W; c++) {
            u16 v = lvl_shop_map[(u16)r * (u16)LVL_SHOP_MAP_W + (u16)c];
            u16 n = (u16)(v & 0x01FFu);
            u8  fl = (u8)((v & 0x8000u) ? 1u : 0u);
            if (n == 0u) continue;
            n--;                                   /* 1-basiert -> Array-Index */
            cell = lvl_shop_tile_a_idx[n];
            if (cell != 0xFFFFu && cell != 0u)
                put_cell(SCR_2_PLANE, lvl_shop_tile_a_pal[n], c, r, cell, fl);
            cell = lvl_shop_tile_b_idx[n];
            if (cell != 0xFFFFu && cell != 0u)
                put_cell(SCR_1_PLANE, lvl_shop_tile_b_pal[n], c, r, cell, fl);
        }
    }
}

/* ---- Shop-Schrift, Text und Cash (20.07.2026) ----
   Der Font ist ein EIGENER Block (lvl_shop_font_tile_data), er muss also zusaetzlich
   ins VRAM. Shop-Hintergrund belegt 0..309, der Auswahl-Indikator 310..314, die
   Schrift kommt dahinter ab 315.
   Wichtig: die Ziffern des HAUPTSPIELS sind waehrend des Shops NICHT resident ???
   der Shop ueberschreibt das ganze Character-RAM. Die Cash-Anzeige nutzt deshalb
   die Ziffern der SHOP-Schrift, die ohnehin 0-9 enthaelt. */
#define SHOP_FONT_VRAM 315u                 /* 315 .. 315+LVL_SHOP_FONT_TILE_DATA_COUNT-1 */
#define SHOP_FONT_PAL_SCR2 14u              /* SCR2 nutzt 0..13, Slot 14 ist frei */

/* Textfeld aus der Nutzer-Vorlage: x 8..96, y 4..51. Kacheln sitzen auf dem
   8px-Raster, deshalb Spalten 1..11 und Zeilen 1..6 (Nutzer: "Versatz egal"). */
#define SHOP_TEXT_COL0 1u
#define SHOP_TEXT_ROW0 1u
#define SHOP_TEXT_COLS 11u
#define SHOP_TEXT_ROWS 5u   /* 20.07.2026: 6 -> 5. Kachelzeile 6 traegt den unteren
                               Rahmen des Textfelds; shop_text_clear() hatte ihn
                               mitgeloescht (im Emulator sichtbar verschwunden). */

/* Cash-Feld: x 83..122, y 132..140 -> 5 Ziffern a 8px, pixelgenau als Sprites
   (auf dem Kachelraster laege es 3px daneben). */
#define SHOP_CASH_X 83u
#define SHOP_CASH_Y 132u
#define SHOP_CASH_DIGITS 5u
#define SHOP_CASH_OAM 8u                    /* 8..12, ausserhalb der Indikator-Slots 0..3 */

static void shop_font_upload(void) {
    volatile u16 *dst, *pal;
    u16 i, w;
    for (i = 0u; i < (u16)LVL_SHOP_FONT_TILE_DATA_COUNT; i++) {
        dst = (volatile u16*)0xA000u + (u16)(SHOP_FONT_VRAM + i) * 8u;
        for (w = 0u; w < 8u; w++) dst[w] = lvl_shop_font_tile_data[i][w];
    }
    /* Schrift-Palette in einen freien SCR2-Slot (Hintergrund-Ebene der Kacheln) */
    pal = SCROLL_2_PALETTE;
    pal[(u16)SHOP_FONT_PAL_SCR2 * 4u + 0u] = 0x0000u;
    pal[(u16)SHOP_FONT_PAL_SCR2 * 4u + 1u] = lvl_shop_font_pal[0][1];
    pal[(u16)SHOP_FONT_PAL_SCR2 * 4u + 2u] = lvl_shop_font_pal[0][2];
    pal[(u16)SHOP_FONT_PAL_SCR2 * 4u + 3u] = lvl_shop_font_pal[0][3];
    /* dieselbe Palette auch als SPRITE-Palette, fuer die Cash-Ziffern */
    pal = SPRITE_PALETTE;
    pal[14u * 4u + 0u] = 0x0000u;
    pal[14u * 4u + 1u] = lvl_shop_font_pal[0][1];
    pal[14u * 4u + 2u] = lvl_shop_font_pal[0][2];
    pal[14u * 4u + 3u] = lvl_shop_font_pal[0][3];
}

/* ASCII -> VRAM-Kachel der Shop-Schrift. 0 = Zeichen nicht vorhanden. */
static u16 shop_glyph(char ch) {
    u8 c = (u8)ch;
    u16 g;
    if (c >= 'a' && c <= 'z') c = (u8)(c - 32u);        /* Font ist Grossbuchstaben */
    if (c < (u8)LVL_SHOP_FONT_ASCII_MIN || c > (u8)LVL_SHOP_FONT_ASCII_MAX) return 0u;
    g = lvl_shop_font_tile[c - (u8)LVL_SHOP_FONT_ASCII_MIN];
    if (g == 0u || g > (u16)LVL_SHOP_FONT_TILE_DATA_COUNT) return 0u;
    return (u16)(SHOP_FONT_VRAM + lvl_shop_font_glyph_idx[g - 1u]);
}

/* Schreibposition des zuletzt gezeichneten Textes ??? damit shop_text_num() eine
   Zahl direkt dahinter setzen kann, wie im Original ("..THAT WILL COST 1000"). */
static u8 g_text_col, g_text_row;

static void shop_text_clear(void) {
    u8 r, c;
    for (r = 0u; r < (u8)SHOP_TEXT_ROWS; r++)
        for (c = 0u; c < (u8)SHOP_TEXT_COLS; c++)
            put_cell(SCR_2_PLANE, 0u, (u8)(SHOP_TEXT_COL0 + c), (u8)(SHOP_TEXT_ROW0 + r), 0u, 0u);
}

/* Text ins Feld schreiben, Wortumbruch bei SHOP_TEXT_COLS.
   '
' (im Export aus den Original-Pausenmarken) = Zeilenumbruch. */
static void shop_text_draw(const char *t) {
    u16 i, wlen;
    g_text_col = 0u; g_text_row = 0u;
    shop_text_clear();
    if (!t) return;
    i = 0u;
    while (t[i] != '\0' && g_text_row < (u8)SHOP_TEXT_ROWS) {
        char ch = t[i];
        if (ch == '\r' || ch == '\n') { g_text_col = 0u; g_text_row++; i++; continue; }
        if (ch == ' ') {
            /* Wortlaenge vorausschauen, damit ganze Woerter umbrechen statt mitten
               im Wort zu reissen. */
            wlen = 0u;
            while (t[i + 1u + wlen] != '\0' && t[i + 1u + wlen] != ' ' &&
                   t[i + 1u + wlen] != '\r' && t[i + 1u + wlen] != '\n') wlen++;
            if (g_text_col != 0u && (u16)g_text_col + 1u + wlen > (u16)SHOP_TEXT_COLS) { g_text_col = 0u; g_text_row++; i++; continue; }
            if (g_text_col != 0u) g_text_col++;
            i++;
            continue;
        }
        if (g_text_col >= (u8)SHOP_TEXT_COLS) { g_text_col = 0u; g_text_row++; continue; }
        {
            u16 tile = shop_glyph(ch);
            if (tile != 0u)
                put_cell(SCR_2_PLANE, (u8)SHOP_FONT_PAL_SCR2,
                         (u8)(SHOP_TEXT_COL0 + g_text_col), (u8)(SHOP_TEXT_ROW0 + g_text_row), tile, 0u);
        }
        g_text_col++; i++;
    }
}

/* Cash als Sprites, rechtsbuendig im roten Feld, fuehrende Nullen unterdrueckt. */

/* ---- Shop-Auswahl (20.07.2026) ----
   Rasterpositionen des Auswahl-Indikators aus der Nutzer-Vorlage (magenta Marker
   in shop_mit_daten.png, exakt ausgemessen): 6 Spalten x 3 Zeilen fuer die Items,
   darunter die drei Knoepfe. Der sichtbare Streifen je Item ist nur 8x1 px hoch ???
   dort ist der Fensterrahmen transparent, das Sprite liegt HINTER den Kacheln
   (SPR_FURTHEST) und leuchtet genau durch diesen Spalt. */
#define SHOP_SEL_COLS 6u
#define SHOP_SEL_ROWS 3u
#define SHOP_ITEM_COUNT (SHOP_SEL_COLS * SHOP_SEL_ROWS)   /* 18 */
#define SHOP_SEL_BUY   (SHOP_ITEM_COUNT + 0u)
#define SHOP_SEL_SELL  (SHOP_ITEM_COUNT + 1u)
#define SHOP_SEL_EXIT  (SHOP_ITEM_COUNT + 2u)
static const u8 shop_sel_col_x[SHOP_SEL_COLS] = { 8u, 35u, 62u, 89u, 116u, 143u };
static const u8 shop_sel_row_y[SHOP_SEL_ROWS] = { 77u, 101u, 125u };
/* Knoepfe: 11x11 px -> 2x2 Sprites */
static const u8 shop_btn_x[3] = { 10u, 37u, 145u };   /* BUY, SELL, EXIT */
#define SHOP_BTN_Y 137u

/* Vier feste OAM-Slots fuer den Indikator (Items brauchen 1, Knoepfe 4). Liegen
   ausserhalb des dynamischen Pools (OAM_POOL_BASE=16), im Shop ist ohnehin alles
   andere abgeschaltet. */
#define SHOP_SEL_OAM 0u
/* Fuenf Muster ab VRAM 310 (Shop belegt 0..309, VRAM hat 512). Der Indikator soll
   exakt die Flaechen der Nutzer-Vorlage treffen: Items 8x1 px, Knoepfe 11x11 px.
   Sprites sind immer 8x8, die Form kommt also aus der Kachel selbst. */
#define SHOP_SEL_T_ROW1  310u   /* 8x1  ??? Item-Streifen           */
#define SHOP_SEL_T_FULL  311u   /* 8x8  ??? Knopf, oben links       */
#define SHOP_SEL_T_C3    312u   /* 3x8  ??? Knopf, rechte Spalte    */
#define SHOP_SEL_T_R3    313u   /* 8x3  ??? Knopf, untere Zeile     */
#define SHOP_SEL_T_3X3   314u   /* 3x3  ??? Knopf, Ecke unten rechts*/
#define SHOP_SEL_PAL 15u          /* Sprite-Palette, vom Shop-Hintergrund unbenutzt */

static u8 g_shop_sel;       /* 0..17 Item, 18 BUY, 19 SELL, 20 EXIT */
static u8 g_shop_in_btns;   /* 0 = Raster, 1 = Knopfreihe */

/* Vollflaechige 8x8-Kachel in Farbindex 1 + Sprite-Palette auf E2A100 setzen.
   NGPC-Farbwort ist (B<<8)|(G<<4)|R (siehe toRGB444 im Tool): E2A100 -> 0x00AE. */
static void shop_sel_build_tile(void) {
    volatile u16 *pal = SPRITE_PALETTE;
    volatile u16 *d;
    u8 i;
    /* 2bpp gepackt, MSB zuerst: 0x5555 = alle 8 Pixel Farbindex 1,
       0x5400 = nur die linken 3 Pixel (01 01 01 00 00 00 00 00). */
    d = (volatile u16*)0xA000u + (u16)SHOP_SEL_T_ROW1 * 8u;
    d[0] = 0x5555u; for (i = 1u; i < 8u; i++) d[i] = 0x0000u;
    d = (volatile u16*)0xA000u + (u16)SHOP_SEL_T_FULL * 8u;
    for (i = 0u; i < 8u; i++) d[i] = 0x5555u;
    d = (volatile u16*)0xA000u + (u16)SHOP_SEL_T_C3 * 8u;
    for (i = 0u; i < 8u; i++) d[i] = 0x5400u;
    d = (volatile u16*)0xA000u + (u16)SHOP_SEL_T_R3 * 8u;
    for (i = 0u; i < 3u; i++) d[i] = 0x5555u; for (; i < 8u; i++) d[i] = 0x0000u;
    d = (volatile u16*)0xA000u + (u16)SHOP_SEL_T_3X3 * 8u;
    for (i = 0u; i < 3u; i++) d[i] = 0x5400u; for (; i < 8u; i++) d[i] = 0x0000u;
    pal[(u16)SHOP_SEL_PAL * 4u + 0u] = 0x0000u;
    pal[(u16)SHOP_SEL_PAL * 4u + 1u] = 0x00AEu;   /* E2A100 */
    pal[(u16)SHOP_SEL_PAL * 4u + 2u] = 0x0000u;
    pal[(u16)SHOP_SEL_PAL * 4u + 3u] = 0x0000u;
}

/* Indikator auf die aktuelle Auswahl setzen. Items = 1 Sprite, Knoepfe = 2x2. */
static void shop_sel_draw(void) {
    u8 k;
    for (k = 0u; k < 4u; k++) UnsetSprite((u8)(SHOP_SEL_OAM + k));
    if (g_shop_sel < (u8)SHOP_ITEM_COUNT) {
        /* Item: exakt 8x1 px, wie in der Vorlage */
        u8 x = shop_sel_col_x[g_shop_sel % SHOP_SEL_COLS];
        u8 y = shop_sel_row_y[g_shop_sel / SHOP_SEL_COLS];
        SetSprite(SHOP_SEL_OAM, SHOP_SEL_T_ROW1, 0, x, y, SHOP_SEL_PAL);
        SpriteControl(SHOP_SEL_OAM, SPR_FURTHEST, 0);
    } else {
        /* Knopf: 11x11 px aus vier Sprites (8+3 waagerecht, 8+3 senkrecht) */
        u8 bi = (u8)(g_shop_sel - SHOP_ITEM_COUNT);
        u8 x = shop_btn_x[bi], y = (u8)SHOP_BTN_Y;
        static const u16 tl[4] = { SHOP_SEL_T_FULL, SHOP_SEL_T_C3, SHOP_SEL_T_R3, SHOP_SEL_T_3X3 };
        for (k = 0u; k < 4u; k++) {
            SetSprite((u8)(SHOP_SEL_OAM + k), tl[k], 0,
                      (u8)(x + ((k & 1u) ? 8u : 0u)), (u8)(y + ((k & 2u) ? 8u : 0u)), SHOP_SEL_PAL);
            SpriteControl((u8)(SHOP_SEL_OAM + k), SPR_FURTHEST, 0);
        }
    }
}

/* Ist an diesem Katalogplatz etwas montiert? (nur Waffen koennen montiert sein) */
static u8 shop_item_owned(u8 idx) {
    if (idx >= (u8)LVL_SHOP_COUNT) return 0u;
    if (lvl_shop_type[idx] != 0u) return 0u;              /* 0 = Waffe */
    return (u8)((g_player.weapons_active & ((u16)1u << lvl_shop_ref[idx])) ? 1u : 0u);
}

static void shop_show_item(u8 idx);
static u8   shop_do_buy(u8 idx);
static u8   shop_do_sell(u8 idx);
static void shop_cash_draw(void);

static u8 shop_input(void) {   /* Rueckgabe: 1 = Shop verlassen */
    u8 col, row;
    if (!g_shop_in_btns) {
        col = (u8)(g_shop_sel % SHOP_SEL_COLS);
        row = (u8)(g_shop_sel / SHOP_SEL_COLS);
        if (g_pad_pressed & J_LEFT)  col = (u8)((col + SHOP_SEL_COLS - 1u) % SHOP_SEL_COLS);
        if (g_pad_pressed & J_RIGHT) col = (u8)((col + 1u) % SHOP_SEL_COLS);
        if (g_pad_pressed & J_UP)    row = (u8)((row + SHOP_SEL_ROWS - 1u) % SHOP_SEL_ROWS);
        if (g_pad_pressed & J_DOWN) {
            if (row == (u8)(SHOP_SEL_ROWS - 1u)) {
                /* 23.07.2026 (Nutzerwunsch): von der UNTERSTEN Item-Reihe direkt in
                   die Knopfreihe - so ist EXIT erreichbar, OHNE erst ein Item mit A
                   auszuwaehlen. Landet auf dem Knopf, der raeumlich unter der
                   aktuellen Spalte liegt (rechte Spalten -> EXIT). */
                u8 cx = shop_sel_col_x[col], bi = 0u, bd = 255u, b, d;
                for (b = 0u; b < 3u; b++) {
                    d = (u8)((shop_btn_x[b] > cx) ? (shop_btn_x[b] - cx) : (cx - shop_btn_x[b]));
                    if (d < bd) { bd = d; bi = b; }
                }
                g_shop_last_item = (u8)(row * SHOP_SEL_COLS + col);
                g_shop_sel = (u8)((u8)SHOP_SEL_BUY + bi);
                g_shop_in_btns = 1u;
                shop_sel_draw();
                return 0u;
            }
            row = (u8)((row + 1u) % SHOP_SEL_ROWS);
        }
        if (g_shop_sel != (u8)(row * SHOP_SEL_COLS + col)) {
            g_shop_sel = (u8)(row * SHOP_SEL_COLS + col);
            shop_show_item(g_shop_sel);      /* Colins Zeile + Betrag */
        }
        if (g_pad_pressed & J_A) {
            /* Runter auf BUY, bei bereits montiertem Item stattdessen auf SELL */
            g_shop_last_item = g_shop_sel;
            g_shop_sel = shop_item_owned(g_shop_last_item) ? (u8)SHOP_SEL_SELL : (u8)SHOP_SEL_BUY;
            g_shop_in_btns = 1u;
        }
    } else {
        if (g_pad_pressed & J_UP) {          /* 23.07.2026: HOCH -> zurueck ins Item-Raster */
            g_shop_sel = g_shop_last_item;
            g_shop_in_btns = 0u;
            shop_sel_draw();
            return 0u;
        }
        if (g_pad_pressed & J_LEFT) {
            if (g_shop_sel > (u8)SHOP_SEL_BUY) g_shop_sel--;
        }
        if (g_pad_pressed & J_RIGHT) {
            if (g_shop_sel < (u8)SHOP_SEL_EXIT) g_shop_sel++;
        }
        if (g_pad_pressed & J_B) {          /* Abbrechen -> zurueck aufs Item */
            g_shop_sel = g_shop_last_item;
            g_shop_in_btns = 0u;
            shop_sel_draw();
            return 0u;                      /* WICHTIG: diese B-Taste ist verbraucht */
        }
        if (g_pad_pressed & J_A) {
            if (g_shop_sel == (u8)SHOP_SEL_EXIT) return 1u;      /* Shop verlassen */
            if (g_shop_sel == (u8)SHOP_SEL_BUY)  shop_do_buy(g_shop_last_item);
            else                                 shop_do_sell(g_shop_last_item);
            /* Nutzerwunsch 20.07.: nach dem Kauf zurueck auf DASSELBE Item. */
            g_shop_sel = g_shop_last_item;
            g_shop_in_btns = 0u;
        }
    }
    shop_sel_draw();
    return 0u;
}

/* ---- Katalog-Icons (20.07.2026) ----
   lvl_shop_icon_spr nutzt dasselbe Encoding wie lvl_spawn_spr: Bits 0-8 = S-Nummer
   (1-basiert in lvl_sspr_*), Bit 14 = animiert. Beispiel 0xC082 = AS130.
   Die Sprite-Kacheln des Spiels sind im Shop NICHT resident (der Shop ueberschreibt
   das Character-RAM), deshalb werden die benoetigten Kacheln hier zusaetzlich
   hochgeladen ??? direkt aus lvl_tile_data, wie spr_tiles_upload() es auch macht.
   VRAM-Belegung im Shop: 0..309 Hintergrund, 310..314 Indikator, 315..364 Schrift,
   ab 365 die Icons (max. 13 x 2 = 26 Kacheln). */
#define SHOP_ICON_VRAM 365u
#define SHOP_ICON_OAM  16u    /* 16..41, ausserhalb Indikator (0-3) und Cash (8-12) */

/* --- Animierte Shop-Icons + sauberes Aufraeumen (21.07.2026) ---
   Je Katalogeintrag merken wir uns belegte VRAM-/OAM-Slots und die zuletzt
   hochgeladene S-Nummer. shop_icons_anim() schreibt pro Frame NUR die Kacheln
   neu, deren Animationsbild sich geaendert hat (statt 26 Uploads je Frame). */
#define SHOP_ICON_MAX 18u        /* == LVL_SHOP_SLOT_COUNT */
static u8  g_shop_anim_tick;
static u16 g_shop_icon_head[SHOP_ICON_MAX];   /* Kopf-S-Nummer, 0 = Eintrag ungenutzt */
static u16 g_shop_icon_shown[SHOP_ICON_MAX];  /* zuletzt hochgeladene S-Nummer */
static u16 g_shop_icon_avram[SHOP_ICON_MAX], g_shop_icon_bvram[SHOP_ICON_MAX];
static u8  g_shop_icon_aoam[SHOP_ICON_MAX],  g_shop_icon_boam[SHOP_ICON_MAX];

/* Aktuelles Animationsbild einer Kopf-S-Nummer ??? exakt wie bei den Pickups. */
/* Metaanim-Icons (27.07.2026, Nutzerwunsch): im Katalogfenster soll NUR die erste
   Zelle jedes Animationsbildes laufen - bei der Kanone also S250/251/252/253 statt
   des kompletten 8x16-Metasprites. Das passt besser ins 8x8-Fenster und nutzt die
   bestehende Ein-Kachel-Nachfuehrung in shop_icons_anim() unveraendert weiter.
   0xFFu = dieser Eintrag ist kein Metaanim. */
static u8 g_shop_icon_manim[SHOP_ICON_MAX];

/* S-Nummer der ersten Zelle des aktuellen Metaanim-Bildes. */
static u16 shop_meta_frame_s(u8 h) {
    u8  fpos;
    u16 mi, off;
    if (lvl_metaanim_speed[h] == 0u) return 0u;
    fpos = (u8)((g_shop_anim_tick / lvl_metaanim_speed[h]) % lvl_metaanim_len[h]);
    mi   = lvl_metaanim_frames[h][fpos];
    if (mi >= (u16)LVL_META_COUNT || lvl_meta_count[mi] == 0u) return 0u;
    off = lvl_meta_off[mi];
    return lvl_meta_num[off];          /* erste Zelle */
}

static u16 shop_icon_frame(u16 head) {
    u8 ai = sspr_anim_find(head);
    if (ai != 0xFFu) {
        u8  fpos = (u8)((g_shop_anim_tick / lvl_sspr_anim_speed[ai]) % lvl_sspr_anim_len[ai]);
        u16 fs   = sspr_anim_frame_s(ai, fpos);   /* leere Bilder ueberspringen */
        if (fs != 0u) return fs;
    }
    return head;
}

static void shop_tile_upload(u16 vram, u16 raw) {
    volatile u16 *d = (volatile u16*)0xA000u + vram * 8u;
    const u16 *s = &lvl_tile_data[raw][0];
    u8 w;
    for (w = 0u; w < 8u; w++) d[w] = s[w];
}

/* Pro Frame: nur Icons neu hochladen, deren Anim-Bild gewechselt hat. */
static void shop_icons_anim(void) {
    u8 i;
    g_shop_anim_tick++;
    for (i = 0u; i < (u8)SHOP_ICON_MAX; i++) {
        u16 head = g_shop_icon_head[i], now, n;
        if (head == 0u) continue;
        /* 27.07.2026: Metaanim-Icons laufen ueber die Metasprite-Bilder, nicht
           ueber eine Streifen-Animation - je Bild die ERSTE Zelle. */
        now = (g_shop_icon_manim[i] != 0xFFu)
                ? shop_meta_frame_s(g_shop_icon_manim[i])
                : shop_icon_frame(head);
        if (now == 0u) continue;
        if (now == g_shop_icon_shown[i]) continue;
        n = (u16)(now - 1u);
        if (n >= (u16)LVL_SSPR_COUNT) continue;
        if (g_shop_icon_aoam[i] != 0xFFu) {
            u16 raw = lvl_sspr_a_idx[n];
            if (raw != 0xFFFFu) shop_tile_upload(g_shop_icon_avram[i], raw);
        }
        if (g_shop_icon_boam[i] != 0xFFu) {
            u16 raw = lvl_sspr_b_idx[n];
            if (raw != 0u && raw != 0xFFFFu) {
                shop_tile_upload(g_shop_icon_bvram[i], raw);
            } else {
                /* Bild ohne b-Ebene: Kachel leeren, sonst bleibt das alte Overlay stehen */
                volatile u16 *d = (volatile u16*)0xA000u + g_shop_icon_bvram[i] * 8u;
                u8 w;
                for (w = 0u; w < 8u; w++) d[w] = 0u;
            }
        }
        g_shop_icon_shown[i] = now;
    }
}

/* Alle vom Shop belegten OAM-Slots abschalten. MUSS beim Verlassen laufen: die
   Icons liegen auf 16..41 und damit MITTEN im dynamischen Pool (OAM_POOL_BASE
   16). Slots, die das Spiel nicht sofort neu vergibt, zeigten sonst weiter ihr
   Shop-Icon (Nutzerbericht 21.07.2026 "Items nach dem Shop noch sichtbar").
   0..15 sind die festen Schiffs-Slots, die ohnehin jeden Frame neu gesetzt
   werden ??? mit abzuschalten raeumt Indikator (0-3) und Cash-Ziffern (8-12)
   gleich mit weg. */
static void shop_oam_clear(void) {
    u8 o;
    for (o = 0u; o < (u8)(SHOP_ICON_OAM + 26u); o++) UnsetSprite(o);
    for (o = 0u; o < (u8)SHOP_ICON_MAX; o++) {
        g_shop_icon_head[o]  = 0u;
        g_shop_icon_shown[o] = 0u;
        g_shop_icon_aoam[o]  = 0xFFu;
        g_shop_icon_boam[o]  = 0xFFu;
    }
}

static void shop_icons_draw(void) {
    volatile u16 *dst;
    const u16 *src;
    u16 vram = (u16)SHOP_ICON_VRAM;
    u8  oam  = (u8)SHOP_ICON_OAM;
    u8  i, w;

    for (i = 0u; i < (u8)SHOP_ICON_MAX; i++) {
        g_shop_icon_head[i] = 0u;
        g_shop_icon_aoam[i] = 0xFFu;
        g_shop_icon_boam[i] = 0xFFu;
        g_shop_icon_manim[i] = 0xFFu;   /* 27.07.2026: kein Metaanim */
    }
    for (i = 0u; i < (u8)LVL_SHOP_COUNT && i < (u8)LVL_SHOP_SLOT_COUNT; i++) {
        u16 spr = lvl_shop_icon_spr[i];
        u16 n, a_raw, b_raw, head, shown;
        u8  x, y;
        if (spr == 0u) continue;                  /* Eintrag ohne Grafik */
        head  = (u16)(spr & 0x01FFu);             /* Kopf-S-Nummer (1-basiert) */
        /* BUGFIX 27.07.2026 (Nutzer: "im Shop wird die Kanone nicht richtig
           angezeigt"): bis hier wurde NUR Bit 0-8 + Anim-Bit ausgewertet. Traegt
           der Eintrag zusaetzlich das META-Bit (0x1000) - wie die Kanone mit
           0x500E -, dann ist die Zahl KEINE S-Nummer, sondern ein Metasprite-
           bzw. Metaanim-Index. Ungeprueft landete man damit auf einem voellig
           fremden Sprite-Streifen (S14 statt Kanone).
           Das Katalogfenster ist 8x8 gross und je Eintrag nur eine a+b-Kachel
           breit ausgelegt (SHOP_ICON_VRAM, 13x2 Kacheln) - ein mehrzelliges
           Metasprite passt da nicht hinein. Als Icon wird deshalb die ERSTE
           ZELLE des ersten Bildes gezeigt. Das ist die Grafik, die der Spieler
           erwartet, und kostet keinen zusaetzlichen VRAM. */
        if (spr & 0x1000u) {
            u16 mi = head;                        /* Metasprite- bzw. Metaanim-Index */
            if (spr & 0x4000u) {                  /* animiert -> erstes Bild nehmen */
                u8 h;
                for (h = 0u; h < (u8)LVL_METAANIM_COUNT; h++)
                    if (lvl_metaanim_head[h] == head) break;
                if (h >= (u8)LVL_METAANIM_COUNT) continue;   /* unbekannt -> kein Icon */
                mi = lvl_metaanim_frames[h][0];
            }
            if (mi >= (u16)LVL_META_COUNT || lvl_meta_count[mi] == 0u) continue;
            /* Nutzerwunsch 27.07.2026: im Katalogfenster laeuft NUR die erste
               Zelle jedes Animationsbildes - bei der Kanone S250/251/252/253.
               Das komplette 8x16-Metasprite sprengte das 8x8-Fenster optisch;
               so bleibt es eine saubere Vorschau und die bestehende
               Ein-Kachel-Nachfuehrung in shop_icons_anim() greift unveraendert. */
            head = lvl_meta_num[lvl_meta_off[mi]];   /* erste Zelle des 1. Bildes */
            if (spr & 0x4000u) {                     /* animiert -> je Bild nachfuehren */
                u8 h;
                for (h = 0u; h < (u8)LVL_METAANIM_COUNT; h++)
                    if (lvl_metaanim_head[h] == (u16)(spr & 0x01FFu)) break;
                if (h < (u8)LVL_METAANIM_COUNT) g_shop_icon_manim[i] = h;
            }
        }
        shown = (g_shop_icon_manim[i] != 0xFFu)
                  ? shop_meta_frame_s(g_shop_icon_manim[i])   /* erste Zelle des Metaanim-Bildes */
                  : shop_icon_frame(head);                    /* normale Streifen-Animation */
        if (shown == 0u) shown = head;
        n = (u16)(shown - 1u);
        if (n >= (u16)LVL_SSPR_COUNT) continue;
        g_shop_icon_head[i]  = head;
        g_shop_icon_shown[i] = shown;
        /* lvl_shop_slot_x/y ist die FENSTERMITTE, SetSprite erwartet die linke
           obere Ecke ??? ein 8x8-Sprite also um 4 Pixel nach links oben ruecken. */
        x = (u8)(lvl_shop_slot_x[i] - 4);
        y = (u8)(lvl_shop_slot_y[i] - 4);

        a_raw = lvl_sspr_a_idx[n];
        if (a_raw != 0xFFFFu) {
            dst = (volatile u16*)0xA000u + vram * 8u;
            src = &lvl_tile_data[a_raw][0];
            for (w = 0u; w < 8u; w++) dst[w] = src[w];
            SetSprite(oam, vram, 0, x, y, lvl_sspr_a_pal[n]);
            SpriteControl(oam, SPR_FRONT, 0);
            g_shop_icon_avram[i] = vram;          /* fuer shop_icons_anim() merken */
            g_shop_icon_aoam[i]  = oam;
            vram++; oam++;
        }
        /* b-Slot IMMER belegen, wenn irgendein Bild der Animation eine b-Ebene hat ???
           sonst haette ein spaeteres Frame keinen Platz fuers Overlay. */
        b_raw = lvl_sspr_b_idx[n];
        if (b_raw != 0u && b_raw != 0xFFFFu) {    /* optionales b-Overlay */
            dst = (volatile u16*)0xA000u + vram * 8u;
            src = &lvl_tile_data[b_raw][0];
            for (w = 0u; w < 8u; w++) dst[w] = src[w];
            SetSprite(oam, vram, 0, x, y, lvl_sspr_b_pal[n]);
            SpriteControl(oam, SPR_FRONT, 0);
            g_shop_icon_bvram[i] = vram;
            g_shop_icon_boam[i]  = oam;
            vram++; oam++;
        }
    }
    /* restliche Icon-Slots abschalten */
    for (; oam < (u8)(SHOP_ICON_OAM + 26u); oam++) UnsetSprite(oam);
}

/* ---- Shop-Mechanik (20.07.2026) ----
   Cash-Ziffern: NICHT die Shop-Schrift, sondern dieselben Ziffern wie im Spiel
   (Sprite-Streifen S37..S46, SPR_S_DIGIT0 ??? Nutzerwunsch). Die liegen im Shop
   nicht im VRAM und werden hier wie die Icons aus lvl_tile_data nachgeladen. */
#define SHOP_DIGIT_VRAM 391u   /* 391..400, hinter den Icons (365..390) */

static void shop_digits_upload(void) {
    volatile u16 *dst;
    const u16 *src;
    u8 d, w;
    for (d = 0u; d < 10u; d++) {
        u16 raw = lvl_sspr_a_idx[(u16)(SPR_S_DIGIT0 - 1u) + d];
        if (raw == 0xFFFFu) continue;
        dst = (volatile u16*)0xA000u + (u16)(SHOP_DIGIT_VRAM + d) * 8u;
        src = &lvl_tile_data[raw][0];
        for (w = 0u; w < 8u; w++) dst[w] = src[w];
    }
}

static void shop_cash_draw(void) {
    /* u16 wie die Score-Anzeige: die Toolchain hat keine 32-Bit-Division
       (C9H_divlu/C9H_remlu fehlen beim Linken). 5 Stellen zeigen bis 65535. */
    u16 v = (g_cash > 65535u) ? 65535u : (u16)g_cash;
    u8 d;
    for (d = 0u; d < (u8)SHOP_CASH_DIGITS; d++) {
        u8 slot = (u8)(SHOP_CASH_OAM + d);
        u8 pos  = (u8)(SHOP_CASH_DIGITS - 1u - d);
        if (v == 0u && d > 0u) { UnsetSprite(slot); continue; }
        SetSprite(slot, (u16)(SHOP_DIGIT_VRAM + (u8)(v % 10u)), 0,
                  (u8)(SHOP_CASH_X + pos * 8u), (u8)SHOP_CASH_Y,
                  lvl_sspr_a_pal[(u16)(SPR_S_DIGIT0 - 1u)]);
        SpriteControl(slot, SPR_FRONT, 0);
        v /= 10u;
    }
}

/* Zahl an den zuletzt gezeichneten Text anhaengen ??? wie im Original, das
   "..THAT WILL COST " schreibt und die Zahl direkt dahinter setzt. Nutzt die
   Shop-Schrift, damit es zum Text passt. */
static void shop_text_num(u16 v) {
    u8 buf[6];
    u8 n = 0u, k;
    if (v == 0u) { buf[n++] = 0u; }
    while (v != 0u && n < 5u) { buf[n++] = (u8)(v % 10u); v /= 10u; }
    for (k = 0u; k < n; k++) {
        u16 tile = shop_glyph((char)('0' + buf[n - 1u - k]));
        if (g_text_col >= (u8)SHOP_TEXT_COLS) { g_text_col = 0u; g_text_row++; }
        if (g_text_row >= (u8)SHOP_TEXT_ROWS) return;
        if (tile != 0u)
            put_cell(SCR_2_PLANE, (u8)SHOP_FONT_PAL_SCR2,
                     (u8)(SHOP_TEXT_COL0 + g_text_col), (u8)(SHOP_TEXT_ROW0 + g_text_row), tile, 0u);
        g_text_col++;
    }
}

/* Aktueller Preis eines Katalogplatzes. Bei Waffen fest, bei Feuerrate/Power
   haengt er von der schon erreichten Stufe ab (Export-Spec: lvl_shop_price ist
   dort 0, der echte Preis kommt aus der Stufentabelle). 0 = nicht kaufbar. */
static u16 shop_price_of(u8 idx) {
    u8 t = lvl_shop_type[idx];
    if (t == 1u) {
        u8 nxt = (u8)(g_player.firerate_stage + 1u);
        if (nxt >= (u8)LVL_FIRERATE_STAGE_COUNT) return 0u;
        return lvl_firerate_stage_price[nxt];
    }
    if (t == 2u) {
        u8 nxt = (u8)(g_player.power_stage + 1u);
        if (nxt >= (u8)LVL_POWER_STAGE_COUNT) return 0u;
        return lvl_power_stage_price[nxt];
    }
    return lvl_shop_price[idx];
}

/* Verkaufswert: nur montierte Waffen, Stufen sind unverkaeuflich (Nutzer 20.07.). */
static u16 shop_sell_value(u8 idx) {
    if (lvl_shop_type[idx] != 0u) return 0u;
    if (!shop_item_owned(idx)) return 0u;
    return lvl_weapon_sell_price[lvl_shop_ref[idx]];
}

/* Montage-Slot frei? Waffen mit gleicher lvl_weapon_mount_slot schliessen sich
   gegenseitig aus (Schritt 17). */
static u8 shop_slot_free(u8 idx) {
    u8 want = lvl_weapon_mount_slot[lvl_shop_ref[idx]];
    u8 w;
    if (want == 0u) return 1u;
    for (w = 0u; w < (u8)LVL_WEAPON_COUNT; w++) {
        if (!(g_player.weapons_active & ((u16)1u << w))) continue;
        if (w == (u8)lvl_shop_ref[idx]) continue;
        if (lvl_weapon_mount_slot[w] == want) return 0u;
    }
    return 1u;
}

/* Item angewaehlt: Colins Zeile + Betrag. */
static void shop_show_item(u8 idx) {
    if (idx >= (u8)LVL_SHOP_COUNT) { shop_text_draw(lvl_shop_txt_what_buy); return; }
    if (shop_item_owned(idx)) {
        shop_text_draw(lvl_shop_txt_will_pay);
        shop_text_num(shop_sell_value(idx));
    } else {
        shop_text_draw(lvl_shop_txt_will_cost);
        shop_text_num(shop_price_of(idx));
    }
}

static u8 shop_do_buy(u8 idx) {
    u16 pr;
    u8  t;
    if (idx >= (u8)LVL_SHOP_COUNT) return 0u;
    t  = lvl_shop_type[idx];
    pr = shop_price_of(idx);
    if (pr == 0u)          { shop_text_draw(lvl_shop_txt_out_of_stock); return 0u; }
    if (g_cash < (u32)pr)  { shop_text_draw(lvl_shop_txt_out_of_stock); return 0u; }
    if (t == 0u) {
        if (shop_item_owned(idx)) { shop_text_draw(lvl_shop_txt_no_room); return 0u; }
        if (!shop_slot_free(idx)) { shop_text_draw(lvl_shop_txt_no_room); return 0u; }
        g_player.weapons_active |= (u16)((u16)1u << lvl_shop_ref[idx]);
    } else if (t == 1u) {
        /* BUGFIX 24.07.2026 (Nutzer-HW: "Feuerrate im Shop gekauft -> danach kein
           Schuss"): firerate_stage lief hier UNGEDECKELT hoch. Ueber die Stufe 3
           hinaus (LVL_FIRERATE_STAGE_COUNT-1) liest fire_cd = lvl_firerate_stage
           [stage]>>1 ausserhalb des 4-Element-Arrays -> Muell-Cooldown -> kein
           Schuss. apply_pickup() deckelte laengst, der Shop nicht. Bei Max: kein
           Kauf, kein Cash-Abzug (wie bei Waffen). */
        if (g_player.firerate_stage >= (u8)(LVL_FIRERATE_STAGE_COUNT - 1u)) {
            shop_text_draw(lvl_shop_txt_no_room); return 0u;
        }
        g_player.firerate_stage++;
    } else {
        /* dito Power: lvl_power_stage_damage[] hat nur LVL_POWER_STAGE_COUNT Eintraege */
        if (g_player.power_stage >= (u8)(LVL_POWER_STAGE_COUNT - 1u)) {
            shop_text_draw(lvl_shop_txt_no_room); return 0u;
        }
        g_player.power_stage++;
    }
    g_cash -= (u32)pr;
    shop_text_draw(lvl_shop_txt_give_cash);
    shop_cash_draw();
    shop_icons_draw();      /* montierte Waffe kann das Icon-Bild aendern */
    return 1u;
}

static u8 shop_do_sell(u8 idx) {
    u16 val;
    if (idx >= (u8)LVL_SHOP_COUNT) return 0u;
    val = shop_sell_value(idx);
    if (val == 0u) { shop_text_draw(lvl_shop_txt_out_of_stock); return 0u; }
    g_player.weapons_active &= (u16)(~((u16)1u << lvl_shop_ref[idx]));
    g_cash += (u32)val;
    shop_text_draw(lvl_shop_txt_heres_cash);
    shop_cash_draw();
    shop_icons_draw();
    return 1u;
}

/* Cash-Mindestbetrag beim Betreten (Punkt 18, aus XENON2.EXE 1000:3fd4/400d).
   Original: Sockel = (Scroll > 0x1E0 ? [0x8eae] : [0x8eac]) + 400 + rnd(0..3)*50,
   und NUR aufstocken, wenn man darunter liegt - eine Untergrenze, kein Ersatz
   ("you never enter the shop broke"). Die Scroll-Schwelle 0x1E0 entspricht bei
   uns Zeile 263; davor gilt der kleine Sockel (L1: 600), ab da der grosse (3000).
   Keine 32-Bit-Division noetig, nur Vergleich und Addition - wichtig, weil
   C9H_divlu beim Linken fehlt (siehe 0.1). */
#define SHOP_FLOOR_MID   600u    /* [0x8eae] */
#define SHOP_FLOOR_END  3000u    /* [0x8eac] */
#define SHOP_FLOOR_ROW   263u    /* Scroll 0x1E0 */

static void shop_cash_floor(void) {
    /* 22.07.2026: ebenfalls gegen die SPIELERzeile pruefen, nicht gegen g_lvl_row
       (19 Zeilen voraus) - sonst schaltet der Sockel 19 Zeilen zu frueh auf den
       hohen Endboss-Betrag um. Schwelle 263 = DOS-Scroll 0x1E0. */
    u32 floor = (u32)(((u16)(g_scroll_y >> 3) >= (u16)SHOP_FLOOR_ROW) ? SHOP_FLOOR_END : SHOP_FLOOR_MID);
    floor += 400u;
    floor += (u32)(GetRandom(3u) * 50u);   /* rnd(0..3)*50 wie im Original */
    if (g_cash < floor) g_cash = floor;
}

static void shop_enter(void) {
    /* Der Raster-Split wird im STATE_SHOP-Zweig abgeschaltet (g_split_active /
       HW_DMA0V sind erst weiter unten deklariert). Hier nur das Bild. */
    shop_cash_floor();   /* Punkt 18: nie mittellos in den Shop */
    SCR1_X = 0u; SCR1_Y = 0u;
    SCR2_X = 0u; SCR2_Y = 0u;
    shop_screen_draw();
    g_shop_sel = 0u; g_shop_in_btns = 0u; g_shop_last_item = 0u;
    shop_sel_build_tile();
    shop_sel_draw();
    shop_font_upload();
    shop_digits_upload();
    shop_text_draw(lvl_shop_txt_welcome);
    shop_cash_draw();
    shop_icons_draw();
}

/* Level-Schleife (Nutzerwunsch 22.07.2026): solange es kein Level 2 gibt, faengt
   nach dem Verlassen des ENDBOSS-Shops Level 1 wieder von vorn an - aber mit
   allem Erspielten: Waffen, Aufwertungsstufen, Geld, Punkte, Leben, Energie.
   game_start() setzt das Level komplett neu auf (Terrain, Wellen, Boss, Shop-
   Latches, Checkpoints, Map-Objekte) und ruft dabei player_init(), das die
   Ausruestung zuruecksetzt - deshalb Schnappschuss davor, Wiederherstellung
   danach. Bewusst NICHT zurueckgesetzt wird der Punktestand: der laeuft ueber
   die Runden weiter. */
static void level_loop_restart(void) {
    u16 keep_weapons = g_player.weapons_active;
    u8  keep_power   = g_player.power_stage;
    u8  keep_rate    = g_player.firerate_stage;
    u8  keep_speed   = g_player.speed_stage;
    u8  keep_lives   = g_player.lives;
    u8  keep_energy  = g_player.energy;
    u16 keep_score   = g_score;
    u32 keep_cash    = g_cash;
    u8  c;

    game_start();

    g_player.weapons_active = keep_weapons;
    g_player.power_stage    = keep_power;
    g_player.firerate_stage = keep_rate;
    g_player.speed_stage    = keep_speed;
    g_player.lives          = keep_lives;
    g_player.energy         = keep_energy;
    g_tune_speed = (u8)(PLAYER_SPEED_STEP + (u8)(g_player.speed_stage * PLAYER_SPEED_STAGE_STEP));
    g_score       = keep_score;
    g_score_shown = keep_score;
    g_cash        = keep_cash;
    /* Leben-Zellen der Bar an den uebernommenen Stand anpassen - game_start()
       hat sie auf "voll" gesetzt (Zelle 10+i lebt, solange i < lives). */
    for (c = 0u; c < 5u; c++)
        g_bar_col[(u8)(10u + c)] = (u8)((g_player.energy > (u8)(c * 8u)) ? 9u : 2u);
    bar_draw_at(g_bar_vrow);
}

/* 2. A-Druck in STATE_SHOP: Terrain-VRAM auf Segment B umschalten, Ring
   komplett neu vorbelegen (wie game_start(), aber ab LVL1_SEGB_ROW_BASE
   statt Zeile 0) und zurueck ins Spiel. Spieler-/Score-/Mapobj-/Gruppen-
   Zustand bleibt unangetastet ??? nur Terrain-Streaming wird neu aufgesetzt. */
static void shop_resume(void) {
    /* Der Shop hat das KOMPLETTE Character-RAM mit seinen eigenen Kacheln
       ueberschrieben ??? Sprites und Bar liegen dort ebenfalls. Vor dem Zurueck
       ins Spiel also nicht nur das Terrain (build_lvl1 unten), sondern auch
       Sprite-Kacheln und -Paletten neu hochladen, sonst besteht das Schiff aus
       Shop-Bruchstuecken. Ab hier wieder VOLLSTAENDIG laden (Index 0), der
       gemeinsame Praefix steht nach dem Shop nicht mehr korrekt im VRAM. */
    spr_sec_select(spr_sec_for_row((u16)(g_scroll_y >> 3)));   /* 30.07.2026: nach dem Shop den Abschnitt der aktuellen Zeile */
    spr_tiles_upload();
    spr_pal_load();
    build_bar_assets();   /* 20.07.2026: die Bar-Kacheln lagen ebenfalls im ueberschriebenen
                             Character-RAM ??? ohne das zeigt die HUD-Leiste nach dem Shop
                             weisse Kaestchen (Emulator-Test). */
    /* Alle Sprite-Caches verwerfen: die Pools merken sich pro Slot die zuletzt
       geschriebene Kachelnummer und ueberspringen sonst das Neusetzen. Nach dem
       Shop stimmt keiner dieser Staende mehr. */
    {
        u8 q, cc;
        for (q = 0u; q < (u8)MAX_BULLETS; q++)        g_bullets[q].last_snum = 0u;
        for (q = 0u; q < (u8)MAX_WPBULLETS; q++)      g_wp_bullets[q].last_snum = 0u;   /* 27.07.2026: animierte Projektile */
        for (q = 0u; q < (u8)MAX_MAPOBJ_BULLETS; q++) g_mo_bullets[q].last_snum = 0u;
        for (q = 0u; q < (u8)MAX_ENEMIES; q++)        g_enemies[q].last_snum = 0u;
        for (q = 0u; q < (u8)MAX_METAENEMIES; q++)
            for (cc = 0u; cc < (u8)META_CELLS; cc++)  g_metaenemies[q].last_snum[cc] = 0u;
        g_ship_last_meta = 0xFFu;
        g_score_last_shown = 0xFFFFu;   /* Ziffern neu setzen */
    }
    /* Level-Schleife: war das der Endboss-Shop, faengt das Level von vorn an
       (siehe level_loop_restart).
       REIHENFOLGE IST WESENTLICH und war beim ersten Anlauf falsch: der Zweig
       stand VOR dem Cache-Block darueber und sprang mit return heraus. Die
       last_snum-Staende der Sprite-Pools hielten dadurch noch die Kachelnummern
       des SHOPS, die Pools ueberspringen ein Neusetzen bei gleicher Nummer - und
       nach dem Neustart standen zerstueckelte Fremdsprites im Bild. Erst
       Uploads, dann Caches verwerfen, DANN neu starten. */
    if (g_shop_was_boss) {
        g_shop_was_boss = 0u;
        level_loop_restart();
        return;
    }
    /* BUGFIX 22.07.2026 (Nutzerbericht "nach dem Shop Versatz der Spawnpunkte,
       beim Endboss kommen noch Gegnerwellen"): hier stand fest
       lvl1_select_segment(1u) + lvl1_prefill(LVL1_SEGB_ROW_BASE) = Zeile 193 -
       ein Rest der alten Zweiteilung, als der Shop zwangsweise genau dort lag.
       Seit der Entkopplung liegt er bei Zeile 125, die Welt sprang also 68
       Zeilen VOR, waehrend g_scroll_y stehenblieb. Die Spawn-Trigger haengen an
       g_scroll_y>>3 und hinkten damit dauerhaft hinterher - Wellen aus der
       Levelmitte feuerten noch in der Boss-Arena.
       Jetzt genau wie respawn_do(): dort weitermachen, wo der Shop betreten
       wurde, mit dem zur Zeile passenden Terrain-Abschnitt. */
    {
        u8 sec = 0u, si;
        for (si = 0u; si < (u8)LVL1_TERR_SEC_COUNT; si++)
            if (g_shop_return_row >= lvl1_terr_sec_row[si]) sec = si;
        lvl1_select_section(sec);
        g_terr_target = sec;       /* keinen halben Wechsel stehen lassen */
    }
    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);
    /* Dynamischer Tile-Austausch (Nutzerwunsch 11.07.2026): der gemeinsame
       Block (Index 0..SHARED_PREFIX_COUNT-1) steht schon von Segment A her
       korrekt im VRAM, siehe build_lvl1()-Kommentar ??? nur den Rest neu
       hochladen (119 statt 162 Tiles, ~27% weniger Bus-Verkehr fuer den
       spuerbarsten Teil des Segment-Wechsels). */
    build_lvl1(0u);   /* 20.07.2026: nach dem echten Shop ist auch der gemeinsame Praefix ueberschrieben -> alles neu */
    {
        u8 ay, ax;
        for (ay = 0u; ay < (u8)ANIM_GRID_ROWS; ay++)
            for (ax = 0u; ax < (u8)ANIM_GRID_COLS; ax++) {
                g_anim_grid[ay][ax]   = 0u;
                g_mapobj_grid[ay][ax] = 0u;
            }
        for (ay = 0u; ay < (u8)LVL_ANIM_COUNT; ay++) {
            g_anim_tick[ay]  = 0u;
            g_anim_frame[ay] = 0u;
            g_anim_cell_count[ay] = 0u;
        }
        for (ay = 0u; ay < 32u; ay++) g_row_map[ay] = (u16)LVL_MAP_H;
    }
    g_scr1_y      = 0u;
    g_scroll_tick = 0u;
    lvl1_prefill(g_shop_return_row);
    g_scroll_y = (u32)g_shop_return_row * 8u;   /* Distanzzaehler auf dieselbe Zeile, sonst
                                                   laufen Terrain und Spawn-Trigger auseinander */
    g_bar_vrow = 19u;   /* +1 (fester Split), siehe game_start */
    g_bar_redraw = 0u; g_bar_restore_pending = 0xFFu;
    bar_draw_at(g_bar_vrow);
    /* BUGFIX 24.07.2026 (Nutzer-HW: grauer Wurm-/Segment-Kaefer bleibt nach dem
       Shop eingefroren an derselben Stelle stehen - das Wurm-Band liegt bei
       Zeile ~92-130, genau um den Shop bei 125). oam_reset_all() setzt Gegner-,
       Meta- UND Wurm-/Wandwurm-OAM sauber zurueck (wie respawn_do). Die
       UnsetSprite-Schreibzugriffe starten wir im VBlank (wait_vblank), damit sie
       auf HW nicht gegen den Bildaufbau racen (vgl. "kein Schuss nach Shop"). */
    wait_vblank();
    oam_reset_all();
    wormhole_anims_reset();   /* 25.07.2026: Loch-Skripte verwerfen - der Ring ist gerade neu aufgebaut worden */
    g_state = STATE_PLAY;
    music_start_theme();   /* Nutzerwunsch: nach dem Shop das Thema von vorne */
}

/* Wiedereinstieg am zuletzt passierten Checkpoint (Punkt 4, siehe CLAUDE.md 0.4e).
   Bewusst dem Original nachgebaut: Scroll springt zurueck, Welt wird gewischt,
   Wellen werden neu scharf, Energie voll, Autofire auf 1 - Punkte und Tempo
   bleiben. KEINE Unverwundbarkeit ueber die normalen 90 Frames hinaus.
   Laeuft am Frame-Ende, weil build_lvl1() Kacheldaten nach VRAM schreibt. */
static void respawn_do(void) {
    u16 row = g_checkpoint_seen ? lvl1_checkpoint_row[g_checkpoint] : 0u;
    u8  cpx = g_checkpoint_seen ? lvl1_checkpoint_x[g_checkpoint] : 80u;
    u8  sec, i;
#if BENCH_DETERM_WORM
    row = 113u;   /* Wurmband-Benchmark: fest ins Band (Map hat keine Checkpoints, Warp scheitert sonst) */
#endif
#if BENCH_META
    /* 25.07.2026 Metasprite-Benchmark: fest ans SCROLL-ENDE (Boss-Arena). Der
       Umweg ueber WARP_CHECKPOINT taugt dafuer NICHT - die Map exportiert
       LVL_CHECKPOINT_COUNT 0, der Warp faellt still aus und der Benchmark liefe
       am Levelanfang mit wechselndem Terrain. Auf dem Deckel steht der Scroll
       sofort still (End-Stop in scroll_update) -> Map steht, Szene konstant. */
    row = (u16)LVL_SCROLL_END_TOP_ROW;
#endif
#if WCRAWL_TEST
    row = 246u;                       /* kurz vor die erste Kriecher-Gruppe */
#endif
#if TEST_WARP_ROW
    row = (u16)TEST_WARP_ROW;         /* Zonen-Messung, siehe TEST_WARP_ROW */
#endif

    g_respawn_pending = 0u;

    /* 22.07.2026: Die Kamera kann nicht weiter als LVL_SCROLL_END_TOP_ROW - ein
       Checkpoint dahinter (Nr. 7 liegt auf 285, der Deckel bei 274) wuerde das
       Bild hinter das Levelende setzen: Boss unter dem Rand, Rest leer. Deshalb
       hier auf denselben Deckel begrenzen; der Wiedereinstieg landet damit
       exakt im stehenden Arena-Bild. */
    if (row > (u16)LVL_SCROLL_END_TOP_ROW) row = (u16)LVL_SCROLL_END_TOP_ROW;

    /* passenden Terrain-Abschnitt waehlen und dessen Kacheln laden */
    sec = 0u;
    for (i = 0u; i < (u8)LVL1_TERR_SEC_COUNT; i++)
        if (row >= lvl1_terr_sec_row[i]) sec = i;
    lvl1_select_section(sec);
    g_terr_target = sec;            /* keinen halben Wechsel stehen lassen */
    build_lvl1(0u);

    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);
    { u8 ay; for (ay = 0u; ay < 32u; ay++) g_row_map[ay] = (u16)LVL_MAP_H; }
    lvl1_prefill(row);
    g_scr1_y   = 0u;
    g_scroll_y = (u32)row * 8u;     /* Distanzzaehler mitziehen (Spawn-Trigger haengt daran) */

    /* Welt wischen: alles, was an einer Position haengt */
    for (i = 0u; i < (u8)MAX_ENEMIES; i++)      g_enemies[i].active = 0u;
    for (i = 0u; i < (u8)MAX_METAENEMIES; i++)  g_metaenemies[i].active = 0u;
    for (i = 0u; i < (u8)MAX_BULLETS; i++)      g_bullets[i].active = 0u;
    for (i = 0u; i < (u8)MAX_ENEMY_BULLETS; i++) g_ebullets[i].active = 0u;
    for (i = 0u; i < (u8)MAX_PICKUPS; i++)      g_pickups[i].active = 0u;
    /* BUGFIX 22.07.2026: diese vier fehlten. Besonders die Wandwuermer sind
       heikel - sie merken sich beim Spawn ihre Loch-RINGZEILE, und respawn_do()
       baut den Ring gleich komplett neu auf. Ein ueberlebender Wurm haengt danach
       an einer voellig falschen Bildschirmzeile und blockiert dazu OAM-Slots. */
    for (i = 0u; i < (u8)MAX_WORMS; i++)        g_worms[i].active = 0u;
    for (i = 0u; i < (u8)WALLWORM_SLOTS; i++)   g_wallworms[i].active = 0u;
    for (i = 0u; i < (u8)WALLWORM_BALLS; i++)   g_wallworm_balls[i].active = 0u;
    /* 28.07.2026 (Nutzerbericht "nach dem Sterben bleiben die Plobs in der
       Explosions-Ansicht"): die Wandkriecher wurden hier bisher GAR NICHT
       angefasst. Sie behielten damit zweierlei ueber den Tod hinweg - ihr
       spent-Flag (also die geplatzte Huelle) UND ihre Ringzeile, die
       respawn_do() gleich darauf neu belegt. Dasselbe Muster wie bei den
       Wandwuermern eine Zeile darueber, nur schlimmer sichtbar, weil die Huelle
       stehenbleibt statt zu verschwinden.
       wcrawls_init() macht sie komplett neu scharf - genau wie die Spawn-Wellen
       weiter unten neu scharf gemacht werden. lvl1_prefill() ordnet ihnen danach
       ueber wcrawl_row_in() wieder ihre Ringzeilen zu. */
    wcrawls_init();
    wormhole_anims_reset();   /* 25.07.2026: Loch-Skripte mitwischen (Ring wird gleich neu aufgebaut) */
    /* Boss bleibt am Leben (mit seinem Schaden, wie im Original), aber seine
       Tentakel-Sprites muessen weg - der Wiedereinstieg setzt das Bild woanders
       hin, und boss_place() laeuft erst wieder, wenn der Anker sichtbar ist. */
    boss_hide();
    for (i = 0u; i < (u8)MAX_MAPOBJ_BULLETS; i++) g_mo_bullets[i].active = 0u;
    for (i = 0u; i < (u8)MAX_WPBULLETS; i++)    g_wp_bullets[i].active = 0u;
    g_bomb.active = 0u; g_laser_on = 0u; g_electro.active = 0u; g_minepod.active = 0u;   /* 23.07.2026: Sonderwaffen-Entities wischen (oam via oam_reset_all) */
    { u8 mk; for (mk = 0u; mk < (u8)MINE_MAX; mk++) g_mines[mk].active = 0u; }

    /* 22.07.2026: OAM hart zuruecksetzen statt darauf zu hoffen, dass jeder
       Zeichenpfad seinen Slot beim Deaktivieren zurueckgegeben hat. Muss NACH
       boss_hide() und nach dem Deaktivieren laufen (siehe oam_reset_all). */
    oam_reset_all();

    /* Wellen ab dem Checkpoint neu scharf machen. Schwelle mit dem Spawn-Vorlauf:
       eine Welle bei Zeile M feuert, sobald Schiff + SPAWN_LEAD_ROWS >= M - also
       gelten Wellen mit M <= row + SPAWN_LEAD_ROWS beim Wiedereinstieg als schon
       passiert (sonst spawnt der Warp/Respawn die naechsten ~12 Zeilen sofort). */
    { u8 sp; u16 lead_row = (u16)(row + SPAWN_LEAD_ROWS);
      for (sp = 0u; sp < (u8)LVL_SPAWN_COUNT; sp++) {
          if (lvl_spawn_row[sp] > lead_row) { g_spawn_fired[sp] = 0u; g_spawn_left[sp] = 0u; g_spawn_timer[sp] = 0u; }
          else                              { g_spawn_fired[sp] = 1u; g_spawn_left[sp] = 0u; g_spawn_timer[sp] = 0u; }
      } }
    { u8 sc; g_spawn_pending = 0u;

      for (sc = 0u; sc < (u8)LVL_SPAWN_COUNT; sc++) if (g_spawn_left[sc]) g_spawn_pending++; }

    g_spawn_scroll_row  = row;
    g_spawn_row_changed = 1u;

    /* Schiff an die Checkpoint-Position; cpx/CHECKPOINT_RESPAWN_Y sind MITTEN,
       g_player.x/y ist die linke obere Ecke der 24x24-Box. */
    g_player.x = (u8)(cpx - 12u);
    g_player.y = (u8)(CHECKPOINT_RESPAWN_Y - 12u);
    g_player.firerate_stage = 0u;    /* Autofire zurueck auf 1 (Original) */
    g_player.energy = (u8)PLAYER_MAX_ENERGY;   /* 22.07.2026: Energie voll, wie im Original */
    bar_set_energy();
    /* 23.07.2026 (s0.4e): Geld und Waffen-Loadout fallen auf den beim letzten
       Checkpoint gesicherten Stand zurueck. Tempo (speed_stage) und Punkte
       bleiben bewusst erhalten. Vor dem ersten Checkpoint gibt es keinen
       Schnappschuss -> dann bleibt der Startzustand (g_cp_* = 0 aus game_start). */
    g_cash                  = g_cp_cash;
    g_player.weapons_active = g_cp_weapons;
    g_player.power_stage    = g_cp_power;
    g_boss_rain             = 0u;   /* kein Regen-Fenster nach einem Respawn */
    g_nashwan_timer         = 0u;   /* Tod beendet Nashwan (Loadout = Checkpoint-Stand) */
    g_tilt_level = 0; g_coast_x = 0u; g_coast_y = 0u;
    g_back_acc = 0u; g_back_px = 0u; /* direkt nach dem Respawn kein Rueckwaertsscrollen */

    /* Sprite-Caches verwerfen, sonst bleiben alte Kachelnummern stehen */
    { u8 q, cc;
      for (q = 0u; q < (u8)MAX_BULLETS; q++)   g_bullets[q].last_snum = 0u;
      for (q = 0u; q < (u8)MAX_ENEMIES; q++)   g_enemies[q].last_snum = 0u;
      for (q = 0u; q < (u8)MAX_METAENEMIES; q++)
          for (cc = 0u; cc < (u8)META_CELLS; cc++) g_metaenemies[q].last_snum[cc] = 0u;
      g_ship_last_meta = 0xFFu; g_score_last_shown = 0xFFFFu; }
    bar_draw_at(g_bar_vrow);
}

/* --- Titelscreen (Nutzerwunsch 14.07.2026, "wie bei Xenon 2" ??? Sterne, die
   einem beim Warp-Flug entgegenkommen, XENON-Logo in der Mitte) ---
   Laeuft VOR jedem Level-/Sprite-VRAM-Laden in main() als eigene, in sich
   abgeschlossene Schleife (kein g_state-Zustand im normalen Sinn) ??? genau
   deshalb ist zu diesem Zeitpunkt praktisch der GESAMTE Tile-Adressraum ab
   Slot 69 frei (nur der kompaktierte Systemfont 32-68 ist schon belegt,
   siehe font_compact_init()-Kommentar in main()) und ALLE 64 OAM-Slots frei
   (Schiff/Schuesse existieren noch nicht) ??? deshalb HIER keine Nutzung des
   dynamischen OAM-Pools (oam_pool_*(), der erst in game_start() initialisiert
   wird), sondern direkte feste Slot-Zuordnung 1:1 pro Stern. */
#define STAR_COUNT     48u
#define TITLE_CX       80
#define TITLE_CY       76
#define STAR_TILE_VRAM 69u   /* einzelner 1px-Punkt, siehe star_tile[] unten */
#define LOGO_TILE_VRAM_BASE 70u   /* 70..(70+LOGO_TILE_COUNT-1), siehe logo_tiles.h */
/* Nutzerkorrektur 14.07.2026 (Farben 221/187/221, 153/153/187, 119/119/170):
   quantisiert auf NGPC-4-Bit (Runden auf /17) -> 0x0DBD/0x0B99/0x0A77 ???
   praktisch dieselben Toene wie die Blau-Familie im Logo selbst, siehe
   logo_tiles.h (logo_pal_blue_a/b), passt farblich bewusst zusammen. */
#define STAR_PAL_0 1u    /* Sprite-Palette 0x8200: 221,187,221 -> 0x0DBD */
#define STAR_PAL_1 2u    /* 153,153,187 -> 0x0B99 */
#define STAR_PAL_2 3u    /* 119,119,170 -> 0x0A77 */
#define LOGO_PAL_BLUE  1u    /* SCR2/SCR1-Palette (getrennter Adressraum je Ebene) */
#define LOGO_PAL_RED   2u
#define LOGO_TX0       2u    /* (20 Bildschirm-Tiles - 16 Logo-Tiles) / 2, horizontal zentriert */
#define LOGO_TY0       4u

typedef struct { s8 dx, dy; u8 t; u8 oam; u8 pal; } TStar;
static TStar g_stars[STAR_COUNT];

/* Nutzerkorrektur 14.07.2026 ("Sterne nur auf 2/5/7/11 Uhr"): dx/dy vorher
   UNABHAENGIG mit Betrag 2-6 gewuerfelt ??? das schliesst rein waagerechte/
   senkrechte Richtungen (dx=0 ODER dy=0, also 12/3/6/9 Uhr) rechnerisch aus,
   uebrig blieben nur diagonale Winkel. Jetzt feste Tabelle mit 24 Uhrzeiger-
   Richtungen (15 statt 30 Grad Schritte, feinere Streuung, Nutzerwunsch
   14.07.2026 "Verteilung im Winkel etwas feiner"), Betrag ~6,
   integer-approximiert (keine Trig-Funktionen auf dieser Plattform). */
static const s8 star_dir_dx[24] = {
     0,  2,  3,  4,  5,  6,  6,  6,  5,  4,  3,  2,
     0, -2, -3, -4, -5, -6, -6, -6, -5, -4, -3, -2
};
static const s8 star_dir_dy[24] = {
    -6, -6, -5, -4, -3, -2,  0,  2,  3,  4,  5,  6,
     6,  6,  5,  4,  3,  2,  0, -2, -3, -4, -5, -6
};

/* Neue zufaellige Flugrichtung/-phase fuer einen Stern. Nutzerkorrektur
   14.07.2026 ("nicht alle aus einem Zentrum, sondern aus einem Kreis"): t
   startete vorher bei 1-4 (Stern taucht quasi exakt im Mittelpunkt auf, alle
   Neustarts haeuften sich dort zu einem dichten Klumpen) ??? jetzt 10-17, das
   ergibt einen kleinen Startradius (Betrag*t/8, siehe Skalierung im
   Zeichen-Loop) statt eines einzelnen Punktes. */
static void star_reset(TStar *s) {
    u8 d = (u8)(QRandom() % 24u);
    s->dx = star_dir_dx[d];
    s->dy = star_dir_dy[d];
    s->t  = (u8)(10u + (QRandom() % 8u));
}

/* Intro/Highscore liegen im File hinter der Titelschleife, werden von ihr aber
   gebraucht - deshalb hier vorwaerts deklariert. cc900 wuerde einen Aufruf ohne
   Deklaration stillschweigend als int-Funktion annehmen; das ist bei void-
   Funktionen zwar folgenlos, aber nichts, worauf man sich verlassen sollte. */
static void intro_start(void);
static void intro_tick(void);
static void intro_stop(void);
static void hs_draw(void);
static u8  g_intro_done;   /* 1 = Intro-Liste einmal komplett gezeigt */
static u8  g_hs_shown;     /* 1 = Highscore-Seite steht und wartet auf A */

static void title_screen_run(void) {
    u8  i;
    u8  blink = 0u, blink_tick = 0u;
    static const u16 star_tile[8] = { 0, 0, 0, 0x0100u, 0, 0, 0, 0 };   /* 1px-Punkt, Zeile 3, Spalte 3 */
    u16 *spal = (u16*)0x8200;

    /* Nutzerkorrektur 14.07.2026 ("Hintergrund 0/0/34"): main() setzt die
       BG-Farbe erst NACH title_screen_run() (siehe dortigen Kommentar), bis
       dahin stand hier noch der InitNGPC()-Standardwert. 0/0/34 quantisiert
       auf NGPC 4-Bit -> 0x0200 (dunkles Blau, kein reines Schwarz). */
    SetBackgroundColour(0x0200u);
    if (OS_VERSION >= 0x10u) {
        BG_PAL = 0x0200u;
        BG_COL = 0xC0u;   /* Bit7-6 = 10: BG-Farbe aktiv, Index 0 */
    }
    /* 27.07.2026: Intro-Texte laufen IM Titelbild, unter dem Logo (Zeilen 9..16).
       intro_start() legt die Alphabet-Paletten an, armiert den Rastersplit und
       baut den ersten Text auf; intro_tick() in der Schleife schaltet weiter. */
    intro_start();

    /* Stern-Tile (1 Pixel) + drei Paletten hochladen ??? EIN Tile reicht, die
       drei Farbtoene kommen rein ueber die Palette, kein zweites/drittes
       Tile noetig. */
    {
        volatile u16 *dst = (volatile u16*)0xA000u + (u16)STAR_TILE_VRAM * 8u;
        for (i = 0u; i < 8u; i++) dst[i] = star_tile[i];
    }
    spal[(u16)STAR_PAL_0 * 4u + 0u] = 0x0000u;
    spal[(u16)STAR_PAL_0 * 4u + 1u] = 0x0DBDu;
    spal[(u16)STAR_PAL_1 * 4u + 0u] = 0x0000u;
    spal[(u16)STAR_PAL_1 * 4u + 1u] = 0x0B99u;
    spal[(u16)STAR_PAL_2 * 4u + 0u] = 0x0000u;
    spal[(u16)STAR_PAL_2 * 4u + 1u] = 0x0A77u;

    /* Logo-Tiles hochladen (94 eindeutige a/b-Tiles, siehe logo_tiles.h)
       und als SCR2(a)+SCR1(b)-Kacheln platzieren ??? exakt dieselbe
       a+b-6-Farben-Technik wie beim Terrain, nur diesmal fuer ein
       statisches Bild statt gescrolltes Level. */
    {
        volatile u16 *dst;
        u16 t;
        for (t = 0u; t < LOGO_TILE_COUNT; t++) {
            dst = (volatile u16*)0xA000u + (u16)(LOGO_TILE_VRAM_BASE + t) * 8u;
            for (i = 0u; i < 8u; i++) dst[i] = logo_tile_data[t][i];
        }
    }
    SetPalette(SCR_2_PLANE, LOGO_PAL_BLUE, logo_pal_blue_a[0], logo_pal_blue_a[1], logo_pal_blue_a[2], logo_pal_blue_a[3]);
    SetPalette(SCR_2_PLANE, LOGO_PAL_RED,  logo_pal_red_a[0],  logo_pal_red_a[1],  logo_pal_red_a[2],  logo_pal_red_a[3]);
    SetPalette(SCR_1_PLANE, LOGO_PAL_BLUE, logo_pal_blue_b[0], logo_pal_blue_b[1], logo_pal_blue_b[2], logo_pal_blue_b[3]);
    SetPalette(SCR_1_PLANE, LOGO_PAL_RED,  logo_pal_red_b[0],  logo_pal_red_b[1],  logo_pal_red_b[2],  logo_pal_red_b[3]);

    for (i = 0u; i < (u8)(LOGO_TW * LOGO_TH); i++) {
        u8 tx = (u8)(i % LOGO_TW), ty = (u8)(i / LOGO_TW);
        u8 pal2 = (logo_lookup[i].pal == 1u) ? LOGO_PAL_BLUE : (logo_lookup[i].pal == 2u) ? LOGO_PAL_RED : 0u;
        PutTile(SCR_2_PLANE, pal2, (u8)(LOGO_TX0 + tx), (u8)(LOGO_TY0 + ty), (u16)(LOGO_TILE_VRAM_BASE + logo_lookup[i].a_vram));
        PutTile(SCR_1_PLANE, pal2, (u8)(LOGO_TX0 + tx), (u8)(LOGO_TY0 + ty), (u16)(LOGO_TILE_VRAM_BASE + logo_lookup[i].b_vram));
    }

    /* Sterne initialisieren ??? gestaffelter Start (t zufaellig 0..119, an die
       Skalierung unten angepasst), sonst wuerden alle sichtbar im selben
       Frame "geboren". Gemischt auf die 3 Farbtoene verteilt. */
    for (i = 0u; i < STAR_COUNT; i++) {
        u8 pick = (u8)(QRandom() % 3u);
        g_stars[i].oam = i;
        g_stars[i].pal = (pick == 0u) ? STAR_PAL_0 : (pick == 1u) ? STAR_PAL_1 : STAR_PAL_2;
        star_reset(&g_stars[i]);
        g_stars[i].t = (u8)(QRandom() % 120u);
    }

    {
    u8 frame_tick = 0u;
    while (1) {
        WaitVsync();
        input_update();
        Sounds_Update();   /* Menue-Musik weiterdrehen */
        /* Nutzerkorrektur 14.07.2026 ("ein Tick schneller"): t waechst
           i.d.R. +1/Frame, aber jeden 3. Frame +2 -> im Schnitt +1.33/Frame
           (~33% schneller als die vorige, als "besser" bestaetigte
           Fassung), ohne wieder auf die zu schnelle erste Fassung
           zurueckzufallen. */
        frame_tick++;
        if (frame_tick >= 3u) frame_tick = 0u;

        for (i = 0u; i < STAR_COUNT; i++) {
            TStar *s = &g_stars[i];
            s16 ox, oy;
            s->t = (u8)(s->t + (frame_tick == 0u ? 2u : 1u));
            ox = (s16)(((s16)s->dx * (s16)s->t) >> 3);
            oy = (s16)(((s16)s->dy * (s16)s->t) >> 3);
            /* Aus dem sichtbaren Bereich raus (links/rechts/oben/unten,
               BAR_Y als untere Grenze wie im Spiel) -> neu starten, damit
               der Strom nie abreisst. */
            if (ox <= (s16)(-TITLE_CX) || ox >= (s16)(SCR_W - TITLE_CX) ||
                oy <= (s16)(-TITLE_CY) || oy >= (s16)(BAR_Y - TITLE_CY)) {
                star_reset(s);
                ox = (s16)(((s16)s->dx * (s16)s->t) >> 3);
                oy = (s16)(((s16)s->dy * (s16)s->t) >> 3);
            }
            SetSprite(s->oam, STAR_TILE_VRAM, 0, (u8)(TITLE_CX + ox), (u8)(TITLE_CY + oy), s->pal);
            /* Nutzerkorrektur 14.07.2026 ("Sterne duerfen nicht durchs/uebers
               Logo fliegen"): SPR_FURTHEST statt SPR_FRONT ??? Sterne liegen
               jetzt HINTER beiden Scroll-Ebenen (SCR2=a/SCR1=b) und werden
               dadurch von jedem opaken Logo-Pixel (egal ob a- oder b-Layer)
               automatisch verdeckt, ohne eigene Kollisionsbox-Pruefung ???
               ueberall sonst (transparente Flaechen) scheinen sie normal
               durch. Priority-Konstanten siehe library.h: SPR_FURTHEST=1<<3,
               SPR_MIDDLE=2<<3, SPR_FRONT=3<<3 (kein SPR_BEHIND definiert). */
            SpriteControl(s->oam, SPR_FURTHEST, 0);
        }

        /* "PRESS A" entfaellt (Nutzerwunsch 27.07.2026) - die Flaeche unter dem
           Logo gehoert jetzt den Intro-Texten (Zeilen 9..16). */
        (void)blink_tick; (void)blink;
        if (!g_hs_shown) {
            intro_tick();   /* Intro-Texte laufen IM Titelbild, unter dem Logo */
            if (g_intro_done) {
                /* Logo UND Text verschwinden, dafuer die Bestenliste (Nutzerwunsch).
                   Der Rastersplit muss aus, sonst wuerde er die Seite verzerren. */
                intro_stop();
                hs_draw();
                g_hs_shown = 1u;
            }
        }

        if (g_pad_pressed & J_A) break;
    }
    }

    intro_stop();   /* Rastersplit aus, Scroll zurueck (siehe intro_start) */
    for (i = 0u; i < STAR_COUNT; i++) UnsetSprite(g_stars[i].oam);
    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);
}

/* ===== Raster-Split-TEST (17.07.2026) =====
   Minimaler, sichtbarer Test des Timer0-HBlank-Interrupts mit Tixus jetzt
   HARDWARE-BESTAETIGTER BIOS_INTLVSET-Sequenz (NgpCraft_MCP commit 798625f,
   ??6.5b). Die ISR schiebt ab einer Scanline den unteren Bildbereich horizontal
   um 40px. Sieht man die Verschiebung -> Interrupt feuert. Aus der HOEHE, in der
   sie beginnt, kalibriere ich dann den echten TREG0-Wert fuer den Bar-Split
   (der offene Punkt: zaehlt der Timer im VBlank mit oder nicht).
   Non-invasiv: laesst bar_shift & den Rest unangetastet, nutzt nur SCR_X
   (die Bar friert ueber SCR_Y, kein Konflikt). */
#define BAR_SPLIT_Y     136u   /* Split-Zeile in der DMA-Tabelle (BAR_Y-8), evtl. kalibrieren */
static u8 g_split_active;       /* 1 = Split aktiv (nur Gameplay) */

/* === MicroDMA-Raster Bar-Split (17.07.2026) ===
   Ersetzt den CPU-Timer0-Split (flackerte beim Scrollen: Interrupt-Latenz-Jitter).
   Zwei MicroDMA-Kanaele schreiben SCR1/SCR2 X+Y pro Scanline aus einer RAM-Tabelle
   -> KEIN CPU-Interrupt pro Zeile, kein Jitter. CH0->SCR1 (0x8032, Timer0),
   CH1->SCR2 (0x8034, Timer1=Timer0-Overflow). Wort-Modus (Y<<8)|X. Tabelle:
   Zeilen 0..135 = g_scr1_y (Terrain scrollt), 136..151 = Freeze (Bar fest bei 144).
   dma_prog_chN_u16 (dmaprog.asm) setzt DMASn/DMADn/DMACn/DMAMn. */
#define HW_DMA0V   (*(volatile u8*)0x007Cu)
#define HW_DMA1V   (*(volatile u8*)0x007Du)
#define TREG1      (*(volatile u8*)0x0023u)
extern void dma_prog_ch0_u16(u32 src, u32 dst, u32 count);
extern void dma_prog_ch1_u16(u32 src, u32 dst, u32 count);
static u16 g_dma_table[152];
static u8  g_dma_dirty;   /* 1 = Tabelle muss neu gebaut werden (im VBlank, siehe ISR) */

/* Eigener VBlank-Handler (ersetzt library VBInterrupt): Watchdog/VBCounter +
   Tabelle bauen + beide DMA-Kanaele neu scharfstellen (MicroDMA ist One-Shot,
   muss jeden VBlank neu gearmt werden -> auch bei 30fps jedes Displaybild). */
/* DMA-Split-Tabelle EINMAL PRO FRAME bauen (aus scroll_update heraus, wenn
   g_scr1_y/g_bar_vrow feststehen). Der ISR baute das frueher JEDEN VBlank neu ->
   bei 30fps (2 VBlanks/Frame) und scroll_update DAZWISCHEN zeigte das 1. Halbbild
   alten, das 2. neuen Scroll -> an der 8px-Zeilengrenze blitzte die Bar-Ringzeile
   kurz durch (Nutzer-HW 18.07.: "Bar blitzt auf"). Jetzt beide Halbbilder = selbe
   Tabelle -> stabil. Bonus: ISR viel leichter (kein 152-Schleifen-Rebuild). */
/* ===================== INTRO (27.07.2026) =====================
   Erster Text mit dem 16x16-Menue-Alphabet, der von klein auf gross aufzieht.
   Der K2GE kann NICHT skalieren (geprueft: der Videoregistersatz hat weder Zoom
   noch Affin-Transformation). Was er kann, ist der Scroll-Offset JE RASTERZEILE -
   genau die Maschinerie, die den HUD-Bar-Split treibt (g_dma_table[152], zwei
   MicroDMA-Kanaele auf 0x8032/0x8034). Damit wird das Textband senkrecht
   gestaucht bzw. gedehnt: fuer jede Scanline im Band zeigt die Tabelle auf eine
   andere Quellzeile. Klein -> gross heisst also erst viele Quellzeilen auf
   wenigen Scanlines, dann 1:1.
   WAAGERECHT geht das nicht - X je Zeile verschiebt nur, es verbreitert nicht.
   Der Text steht deshalb von Anfang an in seiner endgueltigen Breite.
   VRAM: waehrend des Intros ist weder Terrain noch Sprite-Satz geladen, der
   Bereich ab INTRO_VRAM ist frei. Je Zeichen bis zu 8 Kacheln (vier Felder mal
   a/b); gleiche Glyphen werden nur EINMAL hochgeladen. */
#define INTRO_VRAM      200u
#define INTRO_VRAM_MAX  200u
#define INTRO_COLS       10u
#define INTRO_ROWS        4u
/* 27.07.2026: eine Kachelzeile tiefer (Nutzerwunsch). Das Logo endet auf Zeile 8,
   der Textblock belegt jetzt 11..18 und steht damit am unteren Rand. Tiefer geht
   nicht - bei 4 Textzeilen a 2 Kachelzeilen waere sonst der untere Rand ueberschritten. */
#define INTRO_TROW       11u
/* Palettenaufteilung auf dem Titelbildschirm (je Ebene 16 Slots).
   BUGFIX 27.07.2026: das Alphabet hat 10 Paletten und lag vorher auf 0..9 -
   damit ueberschrieb es LOGO_PAL_BLUE (1) und LOGO_PAL_RED (2), das Logo bekam
   fremde Farben. Jetzt: 0..3 bleiben dem Logo, 4..13 das Alphabet, 14..15 der
   Shop-Font der Highscore-Seite. */
#define INTRO_PAL_BASE    4u
#define HS_FONT_PAL_BASE 14u
#define INTRO_SRC_H  (INTRO_ROWS * 16u)

static u8  g_intro_on;
static u8  g_intro_h;    /* Bandhoehe der gerade aufziehenden Zeile */
static u8  g_intro_cur;  /* Index dieser Zeile */
static u8  g_intro_rows; /* wie viele Zeilen der Text belegt */
static u8  g_intro_idx;  /* welcher Text aus lvl_intro_text laeuft gerade */
static u8  g_intro_ph;   /* 0 = Zeile zieht auf, 1 = Zeile steht, 2 = Text steht */
static u16 g_intro_f;    /* Frames in der aktuellen Phase */
static u16 g_intro_glyph[64];

static u16 intro_upload_glyph(u16 g, u16 *next)
{
    volatile u16 *dst;
    const u16 *src;
    u16 base;
    u8 k, w;
    if (g == 0u || g > 64u) return 0u;
    if (g_intro_glyph[g - 1u] != 0u) return g_intro_glyph[g - 1u];
    if ((*next + 8u) > (u16)(INTRO_VRAM + INTRO_VRAM_MAX)) return 0u;
    base = *next;
    for (k = 0u; k < 8u; k++) {
        u16 ti = lvl_menu_font_glyph_idx[(u16)(g - 1u) * 8u + k];
        dst = (volatile u16*)0xA000u + (u16)(base + k) * 8u;
        if (ti == 0u || ti == 0xFFFFu) { for (w = 0u; w < 8u; w++) dst[w] = 0u; continue; }
        src = &lvl_menu_font_tile_data[ti][0];
        for (w = 0u; w < 8u; w++) dst[w] = src[w];
    }
    *next = (u16)(base + 8u);
    g_intro_glyph[g - 1u] = base;
    return base;
}

static u16 intro_glyph_of(u8 code)
{
    if (code >= 97u && code <= 122u) code = (u8)(code - 32u);
    if (code < (u8)LVL_MENU_FONT_ASCII_MIN || code > (u8)LVL_MENU_FONT_ASCII_MAX) return 0u;
    return lvl_menu_font_tile[code - (u8)LVL_MENU_FONT_ASCII_MIN];
}

/* Wie intro_draw_text, aber mit freier Startzeile - fuer die Highscore-
   Ueberschrift. */
static u8 intro_draw_rows(const char *txt, u8 trow);
static void intro_draw_at(const char *txt, u8 trow) { (void)intro_draw_rows(txt, trow); }
static u8 intro_draw_text(const char *txt) { return intro_draw_rows(txt, (u8)INTRO_TROW); }

static u8 intro_draw_rows(const char *txt, u8 trow)
{
    u16 next = (u16)INTRO_VRAM;
    u8  row = 0u;
    u8  i, n, start, cut, k;

    for (i = 0u; i < 64u; i++) g_intro_glyph[i] = 0u;

    start = 0u;
    while (txt[start] != 0 && row < (u8)INTRO_ROWS) {
        n = 0u; cut = 0u;
        while (txt[start + n] != 0 && n < (u8)INTRO_COLS) {
            if (txt[start + n] == 32) cut = n;
            n++;
        }
        if (txt[start + n] != 0 && cut != 0u) n = cut;
        {
            u8 col0 = (u8)((20u - (u16)n * 2u) / 2u);
            volatile u16 *m2 = SCROLL_PLANE_2;
            volatile u16 *m1 = SCROLL_PLANE_1;
            u8 ty = (u8)(trow + row * 2u);
            for (k = 0u; k < n; k++) {
                u16 g = intro_glyph_of((u8)txt[start + k]);
                u16 base = intro_upload_glyph(g, &next);
                u8 f;
                if (base == 0u) continue;
                for (f = 0u; f < 4u; f++) {
                    u8  cx = (u8)(col0 + k * 2u + (f & 1u));
                    u8  cy = (u8)(ty + (f >> 1));
                    u16 pa = lvl_menu_font_glyph_pal[(u16)(g - 1u) * 8u + f * 2u];
                    u16 pb = lvl_menu_font_glyph_pal[(u16)(g - 1u) * 8u + f * 2u + 1u];
                    u16 ia = lvl_menu_font_glyph_idx[(u16)(g - 1u) * 8u + f * 2u];
                    u16 ib = lvl_menu_font_glyph_idx[(u16)(g - 1u) * 8u + f * 2u + 1u];
                    u16 idx = (u16)cy * 32u + (u16)cx;
                    m2[idx] = (ia == 0u || ia == 0xFFFFu) ? 0u
                              : (u16)(((u16)(INTRO_PAL_BASE + pa) << 9) | (u16)(base + f * 2u));
                    m1[idx] = (ib == 0u || ib == 0xFFFFu) ? 0u
                              : (u16)(((u16)(INTRO_PAL_BASE + pb) << 9) | (u16)(base + f * 2u + 1u));
                }
            }
        }
        row++;
        start = (u8)(start + n);
        while (txt[start] == 32) start++;
    }
    return row;
}

/* ===================== HIGHSCORE-SEITE (27.07.2026) =====================
   Zeigt nach einem vollen Intro-Durchlauf die Bestenliste und bleibt stehen,
   bis A gedrueckt wird - dann startet das Spiel.

   LAYOUT: die DOS-Vorlage ist 320px breit, wir haben 160. Rang + siebenstelliger
   Punktestand + Name sind zusammen ~18 Zeichen - mit dem 16x16-Alphabet passen
   aber nur 10 in eine Zeile. Deshalb zweigeteilt:
     Ueberschrift "HIGH SCORE" -> 16x16-Alphabet, exakt 10 Zeichen = volle Breite
     die zehn Eintraege        -> 8x8-Shop-Font, 20 Zeichen je Zeile
   Der Shop-Font liegt im Spiel nicht im VRAM; auf dem Titelbildschirm ist aber
   weder Terrain noch Sprite-Satz geladen, er wird hier also voruebergehend in
   den freien Bereich ab HS_FONT_VRAM geladen.

   SPEICHERN: es gibt im Projekt KEIN Speichersystem (kein Flash-Schreiben). Die
   Tabelle lebt im RAM und ist nach dem Ausschalten weg. Persistenz waere ein
   eigener Schritt - Flash-Sektor loeschen/beschreiben, auf echter Hardware
   heikel, deshalb bewusst nicht nebenbei. */
#define HS_COUNT        10u
#define HS_FONT_VRAM   360u   /* 50 Kacheln Shop-Font, waehrend des Titels frei */
#define HS_HEAD_TROW     1u   /* Ueberschrift (2 Kachelzeilen) */
#define HS_LIST_TROW     5u   /* erste Eintragszeile */

static u32 g_hs_score[HS_COUNT];
static u8  g_hs_name[HS_COUNT][4];   /* 3 Zeichen + Abschluss */
static u8  g_hs_ready;

/* Standardtabelle. Wird spaeter vom echten Spielstand ueberschrieben. */
static void hs_init(void)
{
    u8 i, k;
    if (g_hs_ready) return;
    for (i = 0u; i < (u8)HS_COUNT; i++) {
        g_hs_score[i] = 0uL;
        for (k = 0u; k < 3u; k++) g_hs_name[i][k] = (u8)'.';
        g_hs_name[i][3] = 0u;
    }
    g_hs_ready = 1u;
}

/* Neuen Punktestand einsortieren (vom Spielende aufzurufen). */
static void hs_submit(u32 sc)
{
    u8 i, k;
    hs_init();
    for (i = 0u; i < (u8)HS_COUNT; i++) {
        if (sc > g_hs_score[i]) {
            for (k = (u8)(HS_COUNT - 1u); k > i; k--) {
                g_hs_score[k] = g_hs_score[k - 1u];
                g_hs_name[k][0] = g_hs_name[k - 1u][0];
                g_hs_name[k][1] = g_hs_name[k - 1u][1];
                g_hs_name[k][2] = g_hs_name[k - 1u][2];
            }
            g_hs_score[i] = sc;
            g_hs_name[i][0] = (u8)'.'; g_hs_name[i][1] = (u8)'.'; g_hs_name[i][2] = (u8)'.';
            return;
        }
    }
}

/* Shop-Font in den freien VRAM-Bereich legen. */
static void hs_font_upload(void)
{
    volatile u16 *dst;
    const u16 *src;
    u16 t;
    u8 w;
    for (t = 0u; t < (u16)LVL_SHOP_FONT_TILE_DATA_COUNT && t < 96u; t++) {
        dst = (volatile u16*)0xA000u + (u16)(HS_FONT_VRAM + t) * 8u;
        src = &lvl_shop_font_tile_data[t][0];
        for (w = 0u; w < 8u; w++) dst[w] = src[w];
    }
    for (t = 0u; t < (u16)LVL_SHOP_FONT_PAL_COUNT && t < 16u; t++)
        SetPalette(SCR_2_PLANE, (u8)(HS_FONT_PAL_BASE + t), lvl_shop_font_pal[t][0], lvl_shop_font_pal[t][1],
                                              lvl_shop_font_pal[t][2], lvl_shop_font_pal[t][3]);
}

/* Ein Zeichen mit dem Shop-Font setzen (SCR2). */
static void hs_putc(u8 col, u8 row, u8 ch)
{
    volatile u16 *m2 = SCROLL_PLANE_2;
    u16 g, ti;
    u8  pal;
    if (col >= 20u) return;
    if (ch >= 97u && ch <= 122u) ch = (u8)(ch - 32u);
    if (ch < (u8)LVL_SHOP_FONT_ASCII_MIN || ch > (u8)LVL_SHOP_FONT_ASCII_MAX) {
        m2[(u16)row * 32u + col] = 0u; return; }
    g = lvl_shop_font_tile[ch - (u8)LVL_SHOP_FONT_ASCII_MIN];
    if (g == 0u) { m2[(u16)row * 32u + col] = 0u; return; }
    ti  = lvl_shop_font_glyph_idx[g - 1u];
    pal = lvl_shop_font_glyph_pal[g - 1u];
    if (ti == 0xFFFFu) { m2[(u16)row * 32u + col] = 0u; return; }
    m2[(u16)row * 32u + col] = (u16)((((u16)(HS_FONT_PAL_BASE + pal)) << 9) | (u16)(HS_FONT_VRAM + ti));
}

static void hs_puts(u8 col, u8 row, const char *t)
{
    u8 i = 0u;
    while (t[i] != 0 && (u8)(col + i) < 20u) { hs_putc((u8)(col + i), row, (u8)t[i]); i++; }
}

/* Punktestand als 7 Ziffern. u32-Division fehlt in cc900 -> fortlaufend abziehen. */
static void hs_put_score(u8 col, u8 row, u32 v)
{
    static const u32 pw[7] = { 1000000uL, 100000uL, 10000uL, 1000uL, 100uL, 10uL, 1uL };
    u8 d, k;
    for (k = 0u; k < 7u; k++) {
        d = 0u;
        while (v >= pw[k]) { v -= pw[k]; d++; if (d > 9u) { d = 9u; break; } }
        hs_putc((u8)(col + k), row, (u8)(48u + d));
    }
}

static void hs_draw(void)
{
    u8 i;
    volatile u16 *m2 = SCROLL_PLANE_2;
    volatile u16 *m1 = SCROLL_PLANE_1;
    u16 c;
    hs_init();
    for (c = 0u; c < 32u * 20u; c++) { }          /* (Platzhalter, kein Vollclear noetig) */
    for (i = 0u; i < 19u; i++)
        for (c = 0u; c < 20u; c++) { m2[(u16)i * 32u + c] = 0u; m1[(u16)i * 32u + c] = 0u; }

    hs_font_upload();
    intro_load_pals();
    {   /* Ueberschrift mit dem 16x16-Alphabet, exakt 10 Zeichen = volle Breite */
        u8 saved = 0u;
        (void)saved;
        intro_draw_at("HIGH SCORE", (u8)HS_HEAD_TROW);
    }
    for (i = 0u; i < (u8)HS_COUNT; i++) {
        u8 row = (u8)(HS_LIST_TROW + i);
        u8 r10 = (u8)(i + 1u);
        if (r10 >= 10u) { hs_putc(1u, row, (u8)'1'); hs_putc(2u, row, (u8)'0'); }
        else            { hs_putc(2u, row, (u8)(48u + r10)); }
        hs_put_score(5u, row, g_hs_score[i]);
        hs_puts(14u, row, (const char*)g_hs_name[i]);
    }
}

static void intro_load_pals(void)
{
    u8 i;
    for (i = 0u; i < (u8)LVL_MENU_FONT_PAL_COUNT; i++) {
        u8 sl = (u8)(INTRO_PAL_BASE + i);
        if (sl > 15u) break;
        SetPalette(SCR_2_PLANE, sl, lvl_menu_font_pal[i][0], lvl_menu_font_pal[i][1],
                                    lvl_menu_font_pal[i][2], lvl_menu_font_pal[i][3]);
        SetPalette(SCR_1_PLANE, sl, lvl_menu_font_pal[i][0], lvl_menu_font_pal[i][1],
                                    lvl_menu_font_pal[i][2], lvl_menu_font_pal[i][3]);
    }
}

/* Textblock-Zeilen leeren (Tilemap 9..16), bevor der naechste Text kommt. */
static void intro_clear_block(void)
{
    volatile u16 *m2 = SCROLL_PLANE_2;
    volatile u16 *m1 = SCROLL_PLANE_1;
    u8 r, c;
    for (r = (u8)INTRO_TROW; r < (u8)(INTRO_TROW + INTRO_ROWS * 2u); r++)
        for (c = 0u; c < 20u; c++) { m2[(u16)r * 32u + c] = 0u; m1[(u16)r * 32u + c] = 0u; }
}

/* Text idx aufbauen. 0 = kein Text mehr (leer) -> Aufrufer faengt von vorn an. */
static u8 intro_begin(u8 idx)
{
    if (idx >= (u8)LVL_INTRO_COUNT) return 0u;
    if (lvl_intro_text[idx][0] == 0) return 0u;
    intro_clear_block();
    g_intro_rows = intro_draw_text(&lvl_intro_text[idx][0]);
    if (g_intro_rows == 0u) return 0u;
    g_intro_idx = idx;
    g_intro_cur = 0u;
    g_intro_h   = 2u;
    g_intro_ph  = 0u;
    g_intro_f   = 0u;
    g_intro_on  = 1u;
    g_dma_dirty = 1u;
    return 1u;
}

/* Ein Frame weiterschalten. Wird aus der Titelschleife gerufen, laeuft also
   NICHT in einer eigenen Schleife - der Titel bleibt bedienbar. */
static void intro_tick(void)
{
    if (!g_intro_on) {
        /* naechsten Text suchen, am Ende wieder von vorn */
        u8 nxt = (g_intro_idx == 0xFFu) ? 0u : (u8)(g_intro_idx + 1u);
        u8 tries;
        /* Liste einmal durch -> Highscore-Seite (Nutzerwunsch 27.07.2026). */
        if (nxt >= (u8)LVL_INTRO_COUNT) { g_intro_done = 1u; return; }
        for (tries = 0u; tries < (u8)LVL_INTRO_COUNT; tries++) {
            if (nxt >= (u8)LVL_INTRO_COUNT) { g_intro_done = 1u; return; }
            if (intro_begin(nxt)) return;
            nxt++;
        }
        g_intro_done = 1u;
        return;                      /* gar kein Text vorhanden */
    }
    g_intro_f++;
    if (g_intro_ph == 0u) {          /* Zeile zieht auf */
        /* 27.07.2026 (Nutzerbefund "Artefakte ueber dem Logo, wie eine
           Spiegelung"): die Tabelle NUR neu bauen, wenn sich die Bandhoehe
           wirklich geaendert hat. Der Neubau laeuft im VBlank-ISR und schreibt
           152 Eintraege - jeden Frank blind anzustossen, obwohl sich nichts
           aendert, kostet Zeit im ISR und kann den DMA-Start verzoegern. */
        u8 nh = (u8)(2u + ((14u * g_intro_f) / 24u));
        if (nh != g_intro_h) { g_intro_h = nh; g_dma_dirty = 1u; }
        if (g_intro_f >= 24u) { g_intro_h = 16u; g_intro_ph = 1u; g_intro_f = 0u; }
    } else if (g_intro_ph == 1u) {   /* Zeile steht kurz, dann die naechste */
        if (g_intro_f >= 10u) {
            g_intro_f = 0u;
            g_intro_cur++;
            if (g_intro_cur >= g_intro_rows) { g_intro_ph = 2u; }
            else { g_intro_h = 2u; g_intro_ph = 0u; }
            g_dma_dirty = 1u;
        }
    } else {                         /* ganzer Text steht - Standzeit aus dem Tool */
        u16 hold = lvl_intro_frames[g_intro_idx];
        if (hold < 30u) hold = 30u;
        if (g_intro_f >= (hold >> 1)) {   /* Frames sind 1/60, Spiel laeuft 30 fps */
            g_intro_on = 0u;              /* naechster Durchlauf holt den naechsten Text */
        }
    }
}

/* Einmalige Vorbereitung, bevor die Titelschleife laeuft. */
static void intro_start(void)
{
    u8 i;
    if ((u8)LVL_INTRO_COUNT == 0u) return;
    if ((u8)LVL_MENU_FONT_COUNT == 0u) return;
    intro_load_pals();
    /* 0xFF = "noch keiner gezeigt". Frueher stand hier COUNT-1 als "vor dem
       ersten" - dann hielt der erste Tick die Liste sofort fuer beendet und die
       Highscore-Seite kam noch vor dem ersten Text. */
    (void)i;
    g_intro_idx  = 0xFFu;
    g_intro_on   = 0u;
    g_intro_done = 0u;
    g_split_active = 1u;
    intro_tick();          /* ersten Text sofort aufbauen */
}

static void intro_stop(void)
{
    g_intro_on = 0u;
    g_split_active = 0u;
    SCR1_X = 0; SCR1_Y = 0; SCR2_X = 0; SCR2_Y = 0;
}

static void dma_table_build(void) {
    u8 i;
    /* FESTER Split bei y=144 (18.07.2026): Terrain scrollt Zeile-fuer-Zeile fluessig
       bis 143, ab 144 die (frozene) Bar. KEIN Sub-Pixel mehr (der fror 0-7px ueber
       der Bar ein -> Terrain verschwand in 8px-Stufen, Nutzer-HW). Damit die Bar-
       Ringzeile nicht als Ghost ins Terrain lugt, wird sie eine Ringzeile TIEFER
       gezeichnet (new_bar_vrow +1) -> im Terrain-Scroll off-screen (y>=145).
       Split-Zeile = BAR_Y+1: der MicroDMA greift eine Scanline zu frueh (Nutzer-HW:
       y=143 zeigte sonst die Terrain-Zeile EINGEFROREN). Mit +1 bekommt y=143 noch
       topv (scrollt), erst y=144 den Bar-Freeze. */
    u8 split_row = (u8)(BAR_Y + 1u);
    u16 topv = (u16)((u16)g_scr1_y << 8);
    u16 botv = (u16)((u16)((u8)((u8)(g_bar_vrow << 3) - (u8)BAR_Y)) << 8);
    /* Intro (27.07.2026): jede Textzeile zieht NACHEINANDER von klein auf gross
       auf. Fuer jede Scanline entscheidet die Tabelle, was dort zu sehen ist:
         - Zeilen, die schon fertig sind -> 1:1 (Offset 0),
         - die gerade aufziehende Zeile  -> ihre 16 Quellzeilen auf g_intro_h
           Scanlines verteilt, mittig um ihre Endposition,
         - noch nicht gezeigte Zeilen    -> leerer Tilemap-Bereich.
       Waagerecht laesst sich nichts strecken (X je Zeile verschiebt nur), die
       Breite steht also von Anfang an fest. */
    if (g_intro_on) {
        u16 r;
        for (i = 0u; i < 152u; i++) g_dma_table[i] = 0u;   /* Standard: 1:1 */
        for (r = 0u; r < (u16)g_intro_rows; r++) {
            u16 ry0 = (u16)(((u16)INTRO_TROW + r * 2u) * 8u);   /* Endposition, 16px hoch */
            u16 y;
            if (r < (u16)g_intro_cur) continue;                /* fertig -> 1:1 bleibt */
            if (r > (u16)g_intro_cur) {                        /* noch nicht dran -> leer */
                for (y = ry0; y < (u16)(ry0 + 16u) && y < 152u; y++)
                    g_dma_table[y] = (u16)(((u16)(250u - y) & 0xFFu) << 8);
                continue;
            }
            {   /* die aktive Zeile */
                u16 h    = g_intro_h ? (u16)g_intro_h : 1u;
                u16 top  = (u16)((ry0 + 8u) - (h / 2u));
                u16 step = (u16)((16u << 6) / h);
                u16 acc  = 0u;
                for (y = ry0; y < (u16)(ry0 + 16u) && y < 152u; y++)
                    g_dma_table[y] = (u16)(((u16)(250u - y) & 0xFFu) << 8);
                for (y = top; y < (u16)(top + h) && y < 152u; y++) {
                    u16 sy = (u16)((ry0 + (acc >> 6)) - y);
                    acc = (u16)(acc + step);
                    g_dma_table[y] = (u16)((sy & 0xFFu) << 8);
                }
            }
        }
        return;
    }
    for (i = 0u; i < split_row; i++) g_dma_table[i] = topv;
    for (; i < 152u; i++) g_dma_table[i] = botv;
}
static void __interrupt my_vblank_isr(void) {
    WATCHDOG = WATCHDOG_CLEAR;
    if (USR_SHUTDOWN) { SysShutdown(); while (1); }
    VBCounter++;
    /* Treffer-Flash: NEG-Bit hier setzen, Beam ist aus (siehe neg_flush()). */
    if (g_neg_want) K2GE_2D_CONTROL |= (u8)K2GE_NEG_BIT;
    else            K2GE_2D_CONTROL &= (u8)~K2GE_NEG_BIT;
    /* Tabelle NUR im VBlank neu bauen (Beam aus -> der DMA liest sie nicht gerade)
       und nur EINMAL pro Frame (Dirty-Flag vom Main-Loop). Frueher baute der
       Main-Loop sie MITTEN im Frame, waehrend der DMA sie Scanline fuer Scanline
       las -> Split-Linie sprang fuer ein Bild = Bar flackerte (Nutzer-HW 18.07.).
       Danach nur noch ARMEN (MicroDMA One-Shot, jeden VBlank neu). */
    if (g_split_active) {
        if (g_dma_dirty) { dma_table_build(); g_dma_dirty = 0u; }
        HW_DMA0V = 0u; HW_DMA1V = 0u;                       /* Trigger aus beim Programmieren */
        dma_prog_ch0_u16((u32)g_dma_table, 0x8032u, 152u);  /* CH0 -> SCR1_X/Y */
        dma_prog_ch1_u16((u32)g_dma_table, 0x8034u, 152u);  /* CH1 -> SCR2_X/Y */
        HW_DMA0V = 0x10u;                                    /* CH0 <- Timer0 (HBlank) */
        HW_DMA1V = 0x11u;                                    /* CH1 <- Timer1 (Timer0-Overflow) */
    } else {
        HW_DMA0V = 0u; HW_DMA1V = 0u;                       /* DMA aus ausserhalb des Spiels */
    }
}

static void split_test_init(void) {
    /* Timer0=TI0(HBlank), Timer1=TO0TRG (Timer0-Overflow); beide feuern jede
       Scanline und triggern die MicroDMA-Kanaele. KEIN CPU-ISR/INTLVSET fuer
       Timer0 noetig -> der DMA konsumiert die rohe Timer-Anforderung. */
    T01MOD &= (u8)~0xCFu;   /* Timer0-Takt=TI0, Timer1-Takt=TO0TRG, PWM-Bits erhalten */
    TREG0 = 1u;             /* jede Scanline ein Trigger */
    TREG1 = 1u;
    VBL_INT = my_vblank_isr;
    TRUN |= 0x03u;          /* Timer0 + Timer1 starten */
}

// --- main --
void main(void) {
    InitNGPC();
    /* Volle CPU-Taktrate 6,144 MHz erzwingen (17.07.2026, Test): das Spiel hat
       den Takt bisher nie explizit gesetzt, lief also auf dem BIOS-Default.
       Falls der nicht voll war, ist das ein kostenloser Geschwindigkeitsgewinn;
       war er schon voll, schadet es nicht. CpuSpeed(0)=6MHz, (4)=384KHz. */
    CpuSpeed(0);
    split_test_init();   /* Raster-Split-Test (17.07.2026), siehe split_test_isr */
    /* 28.07.2026: SysSetSystemFont() und font_compact_init() sind RAUS. Der
       kompakte 8x8-Font war zuletzt nur noch fuer "GAME OVER"/"PRESS A" da und
       belegte dafuer dauerhaft die Kacheln 32-41. Der Game-Over-Bildschirm
       schreibt den Text jetzt im 16x16-Intro-Alphabet auf schwarzen Grund
       (STATE_OVER), dort ist der VRAM ohnehin frei. Damit gehoert 0..41
       komplett dem Sprite-Pool (spr_tile_vram_init).
       ACHTUNG: der BIOS-Font steht damit NICHT mehr im VRAM - PrintStringCustom()
       hat keine Datenquelle mehr und darf nicht mehr aufgerufen werden. */

    /* Nutzerwunsch 14.07.2026 (Titelscreen): Pad-Status + QRandom MUESSEN
       schon vor title_screen_run() stehen (Sternenrichtungen/Eingabe-Check
       dort). Level-/Sprite-VRAM UND -Paletten werden bewusst ERST NACH dem
       Titelscreen geladen ??? zu diesem Zeitpunkt ist der komplette
       Tile-Adressraum ab Slot 69 sowie alle 64 OAM-Slots frei, der
       Titelscreen bekommt also ein sauberes Blatt statt sich in die knappe
       Gameplay-Belegung (siehe CLAUDE.md OAM-/VRAM-Budget) einquetschen zu
       muessen. */
    g_pad         = JOYPAD;
    g_pad_prev    = g_pad;
    g_pad_pressed = 0;
    InitialiseQRandom();

    /* Sound-Treiber hochfahren (Z80-Programm nach 0x7000) und das Titelthema im
       Menue starten. Sounds_Update() tickt es in der Titel- und der Spielschleife. */
    Sounds_Init();
    music_start_theme();

    title_screen_run();

    lvl1_vram_init();

    /* bar_shift ENTFERNT 2026-07-18: der MicroDMA-Raster-Split haelt die Bar fest
       (bar_draw_at nutzt die 14 Basis-Tiles bei TILE_BAR_BASE=148, via
       build_bar_assets). Die 126 Sub-Pixel-Varianten (Slots 135-136, 162-176,
       243-252, 355-453) sind damit frei -> jetzt im Sprite-Pool (spr_tile_vram_init),
       macht Platz fuer die neue Map (175 Sprite-Tiles). */
    /* bar_shift_vram_init(); bar_shift_build();  -- nicht mehr noetig */

    SetBackgroundColour(0x0200);
    /* BG-Farbe auch im K2GE-HW-Register aktivieren (nur Color-Hardware) */
    if (OS_VERSION >= 0x10u) {
        BG_PAL = 0x0200u;   /* dunkles Blau hinter allen Ebenen */
        BG_COL = 0xC0u;     /* Bit7-6 = 10: BG-Farbe aktiv, Index 0 */
    }
    ClearScreen(SCR_1_PLANE);
    ClearScreen(SCR_2_PLANE);

    /* Einheitlicher Sprite-Tile-Pool + Paletten fuer Schiff/Schuss/Ziffern/
       Gegner (lvl_pal_sprites, lvl_tile_data 204-281) ??? einmalig, ROM-Daten.
       Ueberschreibt die Titelscreen-Logo-/Stern-Tiles+Paletten wieder, das
       ist gewollt (Titelscreen wird davor komplett geleert). */
    spr_tile_vram_init();
    spr_sec_select(0u);      /* 30.07.2026: Levelstart = Abschnitt 0 */
    spr_tiles_upload();
    spr_pal_load();
    /* Performance-Fix 11.07.2026: einmalig statt in game_start() (das bei
       jedem Retry erneut liefe) ??? lvl_hitzone_* sind feste ROM-Daten, das
       Ergebnis aendert sich nie waehrend der Laufzeit. Siehe
       ship_hitzone_cache_init()-Kommentar. */
    ship_hitzone_cache_init();
    tune_init();   /* Live-Tuning-Startwerte aus den #defines, siehe g_tune_* */

    /* Nutzerwunsch/Library-Hinweis (siehe SeedRandom()-Kommentar in
       library.c: "Call SeedRandom just after the user has pressed
       fire/start from the title screen"): bisher lief das direkt beim
       kalten Boot, VBCounter hatte also praktisch IMMER denselben kleinen
       Wert -> vermutlich seit jeher ein zu wenig zufaelliger Seed. Jetzt
       erst NACH dem Titelscreen, VBCounter hat bis zum A-Druck einen vom
       Spieler abhaengigen, echt variierenden Wert erreicht. */
    SeedRandom();
    game_start();

    {
    u8 frame_ref = VBCounter;   /* 30-fps-Cap, siehe unten */
    while (1) {
        /* FESTER 30-fps-Cap (17.07.2026, Nutzerwunsch): jeder Frame dauert
           mindestens 2 VBlanks. Deckelt den leichten Fall (der bis ~50 kann) auf
           konstante 30 -> gleichmaessiges Spieltempo, kein Schwanken mehr. Unter
           Vollast (Frame >2 VBlanks) ist die Bedingung sofort erfuellt -> kein
           Extra-Warten, laeuft so schnell wie moeglich (selten unter 30). u8-
           Subtraktion wrappt korrekt (siehe VBCounter-Wrap-Bugfix). */
#if BENCH_NOWAIT
        /* gar nicht warten - reine Zyklusmessung, siehe BENCH_NOWAIT */
#elif BENCH_NOKILL || BENCH_META || UNCAP_FPS
        while ((u8)(VBCounter - frame_ref) < 1u) ;   /* Benchmark: 60-fps-Deckel -> die echte Pro-Frame-Last wird sichtbar (der feste 30er-Cap maskiert Verbesserungen) */
#else
        while ((u8)(VBCounter - frame_ref) < 2u) ;
#endif
        /* Raster-Split-Test: Scharfstellen macht jetzt der VBlank-ISR (jeden
           VBlank, siehe my_vblank_isr). Hier nur noch aktivieren, wenn im Spiel. */
        /* Freeze-Wert wird jetzt live im Split-ISR aus g_scr1_y berechnet
           (kein g_bar_vrow-Lag mehr). Hier nur noch aktivieren. */
#if SPLIT_OFF_TEST
        g_split_active = 0u;   /* Messung: Rastersplit aus, siehe SPLIT_OFF_TEST */
        HW_DMA0V = 0u; HW_DMA1V = 0u;
#else
        g_split_active = (u8)(g_state == STATE_PLAY);
        if (PROF_OFF(1)) { g_split_active = 0u; HW_DMA0V = 0u; HW_DMA1V = 0u; }
#endif
        fps_tick(frame_ref);   /* Validierung: misst die VBlanks des eben fertigen Frames (bis final) */
        frame_ref = VBCounter;
        input_update();
        Sounds_Update();   /* Musik weiterdrehen (schreibt nur die Z80-Mailbox, kein VRAM) */
#if BENCH_FIRE
        g_pad |= J_A;      /* Dauerfeuer fuer die Benchmark-Szene (siehe BENCH_FIRE) */
#endif
#if TEST_COLL_HALF || TEST_BULLET_UPDATE_HALF
        g_testpar ^= 1u;   /* Frame-Paritaet der 2-Frame-Takte */
#endif
#if PROFILE_MODE
        /* Blockauswahl mit OPTION + HOCH/RUNTER (Benchmark: Killer-Suche). */
        /* 25.07.2026: Obergrenze 13 -> 17, sonst waren die neuen Zeichen-
           Teilbloecke 13-16 gar nicht anwaehlbar (Nutzer: "ich kann nur bis 12
           auswaehlen"). PROF_SEL_COUNT = hoechster Block + 1. */
#define PROF_SEL_COUNT 24u
        if ((g_pad & J_OPTION) && (g_pad_pressed & J_UP))
            g_prof_sel = (u8)((g_prof_sel + 1u) % PROF_SEL_COUNT);
        if ((g_pad & J_OPTION) && (g_pad_pressed & J_DOWN))
            g_prof_sel = (u8)((g_prof_sel + (PROF_SEL_COUNT - 1u)) % PROF_SEL_COUNT);
#endif

        if (g_state == STATE_PLAY) {
            /* 24.07.2026 Benchmark: OPTION+HOCH/RUNTER cyclet den Profiling-Block
               (siehe oben). OPTION allein macht nichts (God ist via GOD_MODE=1
               dauerhaft an). */
#if WORM_TEST_MODE
            if (g_pad_pressed & J_OPTION) g_paused ^= 1u;   /* Testphase: Pause umschalten (OPTION) */
            if (!g_paused)
#endif
            {
#if WORM_TEST_MODE
            if (g_pad_pressed & J_B) wallworm_spawn_test();   /* Testphase: Wurm an naechster Loch-Spalte ausloesen */
#endif
#if BUSY_TEST
            /* Kalibrierlast, siehe BUSY_TEST. volatile, damit -O3 sie nicht wegwirft. */
            { static volatile u16 bt; u16 bi;
              for (bi = 0u; bi < (u16)BUSY_TEST; bi++) bt = (u16)(bt + bi); }
#endif
            player_update();
#if BENCH_DETERM
            bench_determ_tick();   /* 24.07.2026: feste Gegner-Szene fuer wiederholbare Messungen (siehe BENCH_DETERM) */
#endif
#if BENCH_META
            bench_meta_tick();     /* 25.07.2026: 5 kreisende Alien-Kaefer am Scroll-Ende (siehe BENCH_META) */
#endif
#if ROLLBACK_TEST
            /* Nachweis-Skript, siehe ROLLBACK_TEST. Static-Latches: genau einmal. */
            { u16 prow = (u16)(g_scroll_y >> 3);
              static u8 rt_granted, rt_killed;
              if (!rt_granted && prow >= 8u) {
                  rt_granted = 1u;
                  g_cash += 500u;
                  g_player.weapons_active |= 0x0002u;
                  g_player.power_stage = 2u;
              } else if (rt_granted && !rt_killed && prow >= 12u) {
                  rt_killed = 1u;
                  player_damage(255u);   /* Energie < 255 -> Leben -1 -> Respawn */
              }
            }
#endif
#if RAIN_TEST
            /* Nachweis-Skript, siehe RAIN_TEST. */
            { static u8 rn_phase; u16 prow = (u16)(g_scroll_y >> 3);
              if (rn_phase == 0u && prow >= 8u) { rn_phase = 1u; boss_cash_rain(); }
              else if (rn_phase == 1u) { player_damage(255u); }   /* muss wirkungslos sein, solange Regen laeuft */
            }
#endif
#if PICKUP_TEST
            /* Nachweis-Skript, siehe PICKUP_TEST. */
            { static u8 pk_phase; u16 prow = (u16)(g_scroll_y >> 3);
              if (pk_phase == 0u && prow >= 6u) { pk_phase = 1u; g_player.energy = 10u; bar_set_energy(); }
              else if (pk_phase == 1u) { pk_phase = 2u; apply_pickup(5u, 40u); }   /* Energie -> voll (39) */
              else if (pk_phase == 2u && prow >= 12u) { pk_phase = 3u; apply_pickup(6u, 0u); }   /* Smart Bomb */
            }
#endif
#if NASHWAN_TEST
            /* Nachweis-Skript, siehe NASHWAN_TEST. */
            { static u8 nw_done; if (!nw_done && (u16)(g_scroll_y >> 3) >= 6u) { nw_done = 1u; apply_pickup(7u, 0u); } }
#endif
            if (!PROF_OFF(5)) {          /* Block 5: alle Schuss-Systeme */
#if TEST_BULLET_UPDATE_HALF
            if (g_testpar) {             /* 2-Frame-Takt, Doppelschritt in den Funktionen */
            bullets_update();
            mapobj_bullets_update();
            }
#else
            bullets_update();
            mapobj_bullets_update();
#endif
            weapon_update();
            bomb_update();       /* 23.07.2026: BOMB (Waffe 10) */
            laser_update();      /* 23.07.2026: LASER (Waffe 3) */
            electro_update();    /* 23.07.2026: ELECTRO BALL (Waffe 6) */
            mine_update();       /* 23.07.2026: MINE (Waffe 5) */
            nashwan_update();    /* 23.07.2026: Super Nashwan Power (Ablauf-Timer) */
            }
            if (!PROF_OFF(3)) {          /* Block 3: Gegner- und Wurm-Update */
            if (!PROF_OFF(9))  enemies_update();
            boss_update();   /* Endboss, ab Zeile BOSS_ROW */
            if (!PROF_OFF(10)) metaenemies_update();
            if (!PROF_OFF(11)) worms_update();
            if (!PROF_OFF(12)) wallworms_update();  /* Schritt 19: Wandwuermer */
            wcrawls_update();   /* 28.07.2026: Wandkriecher ("Schleim") */
            }
            if (!PROF_OFF(5))
#if TEST_BULLET_UPDATE_HALF
            { if (g_testpar) ebullets_update(); }   /* 2-Frame-Takt, siehe oben */
#else
            ebullets_update();   /* Schritt 18: NACH enemies_update (dort entstehen die Schuesse) */
#endif
            if (!PROF_OFF(4)) {          /* Block 4: Kollisionen */
#if TEST_COLL_HALF
            if (g_testpar) {             /* nur jeden zweiten Frame (Messbuild, siehe TEST_COLL_HALF) */
            check_collisions();
            wallworms_collide();
            }
#else
            check_collisions();
            wallworms_collide(); /* Schritt 19: nach check_collisions (nutzt g_wp_bullets) */
#endif
            }
            pickups_update();
            if (!PROF_OFF(6))            /* Block 6: Scroll + Terrain-Streaming */
            scroll_update();
            g_dma_dirty = 1u;    /* Split-Tabelle im naechsten VBlank neu bauen (Beam aus) ??? nicht hier mitten im Frame (sonst flackert die Bar, Nutzer-HW 18.07.) */
            thrust_update();
            /* SCR_Y wird im VBlank-ISR gesetzt (my_vblank_isr) ??? jeden VBlank,
               damit der Split stabil ist. */
            if (!PROF_OFF(7))            /* Block 7: Terrain-Animationen */
#if TEST_ANIM_OFF
            { }                          /* Messbuild: Animationen komplett aus */
#else
            anim_update();
#endif
            mapobj_rates_update();   /* 28.07.2026: Akkumulator-Feuern + getaktete Anemone */
            /* 25.07.2026 HAUSPUTZ gegen Geister-Sprites (Nutzerbericht: "einmal
               in mehreren Durchlaeufen ein Geister-Sprite"). Alle 128 Frames
               JEDES Zeichen-System einmal zwangsweise durchlaufen lassen, auch
               bei busy==0. Das begrenzt die Lebensdauer eines haengengebliebenen
               Sprites auf gut vier Sekunden - unabhaengig von der Ursache:
               verlorener UnsetSprite-Schreibzugriff auf Hardware (dafuer gibt es
               oam_scrub_step), ein Flag, das faelschlich auf 0 stand, oder eine
               Erzeugungsstelle, die ich uebersehen habe. Kostet einen Frame
               "alter" Rechenzeit alle 128 Frames, also unter 1 %. Bewusst ein
               Sicherheitsnetz und keine Ursachenbehebung - die Ursache ist bei
               einem Einzelfall in mehreren Durchlaeufen nicht greifbar. */
            { static u8 hk_tick;
              hk_tick++;
              if ((u8)(hk_tick & 127u) == 0u) {
                  g_busy_bullets = g_busy_mobullets = g_busy_ebullets = 1u;
                  g_busy_enemies = g_busy_metas = g_busy_worms = 1u;
                  g_busy_wworms  = g_busy_pickups = g_busy_wpbul = g_busy_wpent = 1u;
              } }
            if (!PROF_OFF(2))            /* Block 2: Sprites zeichnen (OAM) */
            draw_sprites();
            /* Schritt 18: Gegner-Schuesse nach draw_sprites, aber VOR score_draw()
               ??? hinter allen Spielobjekten, vor den Ziffern (OAM-Pool-Prioritaet,
               siehe oam_pool_*()-Kommentar zur Zeichenreihenfolge). */
            boss_draw();
            ebullets_draw();
            hit_flash_update();
            oam_scrub_step();   /* 25.07.2026: HW-Geister-Heilung (verlorene Unset-Schreibzugriffe), siehe oam_scrub_step() */
            score_roll_update();
#if PAL_SELFCHECK
            /* Eine Palette je Frame, siehe spr_pal_check_step(). */
            spr_pal_check_step();
#if PAL_SELFHEAL
            if (g_pal_bad) { g_pal_heals++; spr_pal_load(); g_pal_bad = 0u; }
#endif
#endif
            if (!PROF_OFF(8))            /* Block 8: HUD-Bar + Ziffern */
            bar_redraw_flush();   /* Bar-Rahmen am Frame-Ende zeichnen (Timing wie Ziffern -> kein Aufblitzen) */
            boss_body_flash();    /* 23.07.2026: Boss-Body-Treffer-Flash (Tilemap, Frame-Ende-Timing wie die Bar) */
            /* 28.07.2026: EINE Stelle schreibt das NEG-Bit, nachdem alle
               Trefferstellen ihr g_neg_on gesetzt haben. Muss hinter
               boss_body_flash() stehen - der setzt es als letzter. */
            neg_flush();
            terr_sec_step();      /* Terrain-Abschnitt schrittweise nachladen, gleiches Timing */
            spr_sec_step();       /* 30.07.2026: Sprite-Abschnitt, gleiches Timing */
            if (g_respawn_pending) respawn_do();   /* Punkt 4: Wiedereinstieg, gleiches Timing */
            shop_delay_tick();    /* Shop-Ausloeser: Countdown, Zustandswechsel am Frame-Ende */
#if PAL_RELOAD_TEST
            /* HW-Diagnose, siehe PAL_RELOAD_TEST. Einmalig, am Frame-Ende. */
            { static u16 pal_rt; static u8 pal_done;
              if (!pal_done) { pal_rt++;
                  if (pal_rt >= (u16)PAL_RELOAD_FRAME) { spr_pal_load(); pal_done = 1u; } } }
#endif
            /* Block 8 laesst die Bar weg, die Ziffern muessen bleiben - sonst
               kann man den Messwert nicht mehr ablesen. */
            score_draw();

            if (g_player.lives == 0 && g_player.inv_cd == 0) {
                /* GAME OVER (28.07.2026, Nutzerwunsch): schwarzes Bild, dann den
                   INTRO-Font laden und den Text im 16x16-Alphabet zeigen.
                   Der Gewinn ist VRAM: der kompakte 8x8-Font belegte dafuer
                   dauerhaft die Kacheln 32-41 - fuer einen einzigen Bildschirm.
                   Auf schwarzem Bild ist der ganze Adressraum frei, die Glyphen
                   koennen also dorthin, wo sonst Sprites liegen (INTRO_VRAM).
                   Reihenfolge ist wesentlich: erst Bild und Sprites weg, dann
                   Paletten, dann Text - sonst blitzt das Alphabet kurz mit den
                   Terrain-Farben auf (intro_load_pals belegt SCR2 4..13, also
                   auch Terrain- und Bar-Slots). */
                g_state = STATE_OVER;
                g_split_active = 0u;          /* Rastersplit aus, wie im Shop */
                HW_DMA0V = 0u; HW_DMA1V = 0u;
                wait_vblank();
                oam_reset_all();              /* Schiff, Gegner, Schuesse weg */
                ClearScreen(SCR_1_PLANE);
                ClearScreen(SCR_2_PLANE);
                intro_load_pals();
                intro_draw_at("GAME OVER", 8u);
            }
            }   /* Ende Pause-Gate (WORM_TEST_MODE) */
        } else if (g_state == STATE_OVER) {
            if (g_pad_pressed & J_A) {
                SeedRandom();
                game_start();
            }
        } else {   /* STATE_SHOP */
            if (!g_shop_entered) {
                /* Raster-Split aus: der MicroDMA schreibt sonst weiter Scroll-
                   Werte fuer die Bar und zerreisst das stehende Shop-Bild. */
                g_split_active = 0u;
                HW_DMA0V = 0u; HW_DMA1V = 0u;
                shop_enter();
                g_shop_entered = 1u;
            }
            /* Verlassen ausschliesslich ueber A auf EXIT. Ein zusaetzlicher
               B-Notausgang ging nicht: shop_input() verbraucht B bereits fuer den
               Ruecksprung aus der Knopfreihe, und dieselbe Taste haette hier
               nochmal gezuendet ??? der Shop schloss sich beim Abbrechen. */
            else {
                /* Icons animieren (Nutzerwunsch 21.07.2026). Laeuft NACH shop_enter(),
                   damit die Slot-Zuordnung steht, und schreibt nur bei Bildwechsel. */
                shop_icons_anim();
            }
            if (shop_input()) {
                g_shop_entered = 0u;
                g_shop_a_count = 0u;
                shop_oam_clear();   /* Shop-Sprites abschalten, BEVOR das Spiel weiterlaeuft */
                shop_resume();
            }
        }
#if TELEMETRY
        /* Messwerte fuer den Emulator-Treiber, siehe TELEMETRY. Steht bewusst
           HINTER der Zustands-Verzweigung: im STATE_PLAY-Zweig platziert fror er
           im Shop ein, der Treiber sah dort nur Standwerte und konnte nicht
           erkennen, wo der Cursor steht. */
        {
            volatile u8 *t = (volatile u8 *)TELEMETRY_ADDR;
            u16 sy = (u16)g_scroll_y;
            t[0]  = (u8)(sy & 0xFFu);              t[1]  = (u8)(sy >> 8);
            t[2]  = g_player.x;                    t[3]  = g_player.y;
            t[4]  = g_player.lives;                t[5]  = g_player.energy;
            t[6]  = (u8)(g_score & 0xFFuL);        t[7]  = (u8)((g_score >> 8) & 0xFFuL);
            t[8]  = g_state;                       t[9]  = (u8)(g_player.weapons_active & 0xFFu);
            t[10] = (u8)((g_score >> 16) & 0xFFuL); t[11] = (u8)((g_score >> 24) & 0xFFuL);
            t[12] = g_shop_sel;                    t[13] = g_shop_in_btns;
            t[14] = (u8)(g_cash & 0xFFuL);         t[15] = (u8)((g_cash >> 8) & 0xFFuL);
            /* Erster aktiver Modulschuss: Y, Herkunftswaffe, Anzahl aktiver.
               Damit laesst sich die Flugbahn je Waffe MESSEN statt im Bild zu
               raten - die Hauptkanone feuert immer nach oben und uebertoent den
               Unterschied optisch. */
            { u8 q, cnt = 0u, fy = 0u, fw = 0xFFu, fx = 0u; s16 fvx = 0;
              for (q = 0u; q < (u8)MAX_WPBULLETS; q++)
                  if (g_wp_bullets[q].active) {
                      if (cnt == 0u) { fy = (u8)(g_wp_bullets[q].y_fix >> 4); fw = g_wp_bullets[q].w;
                                       fx = (u8)(g_wp_bullets[q].x_fix >> 4); fvx = g_wp_bullets[q].vx; }
                      cnt++;
                  }
              t[16] = fy; t[17] = fw; t[18] = cnt; t[19] = g_player.y;
              t[35] = fx; t[36] = (u8)(fvx & 0xFF);   /* Modulschuss[0]: x, vx (signed low) */
              { u8 en; s16 ex = 0, ey = 0; en = wp_nearest_enemy((s16)fx, (s16)fy, &ex, &ey);
                t[37] = (u8)(en == 0xFFu ? 0xFFu : (u8)ex); } }   /* naechster Gegner-x (Homing-Ziel) */
            t[38] = (u8)(g_dbg_area & 0xFFu); t[39] = (u8)(g_dbg_area >> 8);   /* Flaechentreffer */
            t[40] = (u8)(g_bomb.active | (u8)(g_bomb.phase << 1));             /* Bomb: aktiv|phase */
            t[41] = (u8)(g_bomb.y_fix >> 4);                                   /* Bomb-y */
            { u8 mc = 0u, mk; for (mk = 0u; mk < (u8)MINE_MAX; mk++) if (g_mines[mk].active) mc++;
              t[42] = mc; }                                                    /* aktive Minen */
            t[43] = (u8)(g_electro.active | (u8)(g_minepod.active << 1));      /* Electro|Pod aktiv */
            { u8 ec = 0u, ej; for (ej = 0u; ej < (u8)MAX_ENEMIES; ej++) if (g_enemies[ej].active) ec++;
              for (ej = 0u; ej < (u8)MAX_METAENEMIES; ej++) if (g_metaenemies[ej].active) ec++;
              t[44] = ec; }                                                    /* aktive Gegner (Smart-Bomb-Test) */
            t[45] = (u8)(g_nashwan_timer & 0xFFu); t[46] = (u8)(g_nashwan_timer >> 8);  /* Nashwan-Timer */
            t[20] = g_dmg_src;

            { u8 wq, wc = 0u, wp = 0u;

              for (wq = 0u; wq < (u8)WALLWORM_SLOTS; wq++) if (g_wallworms[wq].active) wc++;

              for (wq = 0u; wq < (u8)WALLWORM_BALLS; wq++) if (g_wallworm_balls[wq].active) wp++;

              t[21] = wc; t[22] = wp; }


              t[23] = g_oam_pool_n;



              { u8 bq, bc = 0u; for (bq = 0u; bq < (u8)MAX_BULLETS; bq++) if (g_bullets[bq].active) bc++;



                t[24] = bc; t[25] = g_player.fire_cd; t[26] = g_player.inv_cd; }
            t[27] = (u8)(g_dbg_shots & 0xFFu); t[28] = (u8)(g_dbg_shots >> 8);
            /* 23.07.2026: Nachweis Loadout-Rueckfall + Cash-Regen-Fenster. */
            t[29] = (u8)(g_cp_cash & 0xFFuL); t[30] = (u8)((g_cp_cash >> 8) & 0xFFuL);
            t[31] = g_cp_power;               t[32] = g_player.power_stage;
            t[33] = g_boss_rain;              t[34] = (u8)(g_cp_weapons & 0xFFu);
            { u8 mq, mc = 0u, mrun = 0u; u16 mtot = 0u;
              for (mq = 0u; mq < (u8)MAX_MAPOBJ_BULLETS; mq++) if (g_mo_bullets[mq].active) mc++;
              for (mq = 0u; mq < (u8)LVL_MAPOBJ_COUNT; mq++) { if (g_mapobj_run[mq]) mrun++; mtot += g_mapobj_frame[mq]; }
              t[62] = mc; t[63] = mrun; t[64] = (u8)(mtot & 0xFFu);
              t[65] = g_dbg_mocall; t[66] = g_dbg_mofire; t[67] = g_mapobj_acc[0]; t[68] = g_mapobj_wilted[0]; }
            /* 28.07.2026 Wandkriecher (tools/probe_wallcrawler.py):
               t[47] = wie viele gerade im Bild sind
               t[48] = wie viele schon geplatzt sind
               t[49] = wie viele abgeschossen sind
               t[50] = y des ERSTEN im Bild (Bewegungsnachweis), 0xFF = keiner
               t[51] = x dazu
               t[52] = Zustand dazu: Bit0 geplatzt, Bit1 Seite (1=rechts)
               t[53] = aktive Gegnerschuesse (die Salve muss auf 3 springen) */
            { u8 wk, won = 0u, wsp = 0u, wde = 0u, wfy = 0xFFu, wfx = 0u, wst = 0u, wbl = 0u;
              for (wk = 0u; wk < (u8)LVL_WCRAWL_COUNT; wk++) {
                  if (!g_wcrawlers[wk].alive) { wde++; continue; }
                  if (g_wcrawlers[wk].spent) wsp++;
                  if (g_wcrawlers[wk].on) {
                      won++;
                      if (wfy == 0xFFu) {
                          wfy = g_wcrawlers[wk].y; wfx = g_wcrawlers[wk].x;
                          wst = (u8)((g_wcrawlers[wk].spent ? 1u : 0u) | (u8)(lvl_wcrawl_side[wk] ? 2u : 0u));
                          /* Laufweg + Richtung roh (probe_wallcrawler.py) */
                          t[54] = (u8)(g_wcrawlers[wk].off_fix & 0xFFu);
                          t[55] = (u8)((u16)g_wcrawlers[wk].off_fix >> 8);
                          t[56] = (u8)(g_wcrawlers[wk].dir & 0xFF);
                          t[57] = g_wcrawlers[wk].ring;
                          t[58] = lvl_wcrawl_amp[wk];
                      }
                  }
              }
              for (wk = 0u; wk < (u8)MAX_ENEMY_BULLETS; wk++) if (g_ebullets[wk].active) wbl++;
              t[47] = won; t[48] = wsp; t[49] = wde;
              t[50] = wfy; t[51] = wfx; t[52] = wst; t[53] = wbl;
              /* Erster aktiver Gegnerschuss: x/y/vx - damit laesst sich die
                 RICHTUNG der Salve messen (linke Wand nach rechts, rechte nach
                 links) statt sie im Bild zu suchen. */
              { u8 wb; t[59] = 0xFFu; t[60] = 0u; t[61] = 0u;
                for (wb = 0u; wb < (u8)MAX_ENEMY_BULLETS; wb++)
                    if (g_ebullets[wb].active) {
                        t[59] = (u8)(g_ebullets[wb].x_fix >> 4);
                        t[60] = (u8)(g_ebullets[wb].vx & 0xFF);
                        t[61] = (u8)(g_ebullets[wb].y_fix >> 4);
                        break;
                    } } }
        }
#endif
    }
    }   /* Ende frame_ref-Block (30-fps-Cap) */
}





































































