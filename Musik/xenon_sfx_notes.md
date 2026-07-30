# Xenon 2 DOS Sound-Effekte — weggesichert für spätere NGPC-Integration

Best-Effort-Sicherung (24.07.2026) neben der Musik-Konvertierung. Die
**Roh-Records** und die **ID-Landkarte** sind hier festgehalten; die genaue
15-Byte-Record-Struktur ist noch **nicht dekodiert** (separater Schritt).

## Woher

Die DOS-SFX liegen — wie die Musik — im INT-80h-Treiber in `XENON2.EXE`
(DRV = Datei-Offset `0x15390`). Jede SFX-ID lädt einen **15-Byte-Record** ab
`DRV:0x420 + id*15` (INT 80h `AH=3`, Loader `1000:14d33`, Ziel `[0x410]`).
Getriggert werden SFX über `[0x8e88]`/`[0x8e8a]` (pending-Byte, vom Timer-ISR
`1000:602c..6048` konsumiert) bzw. die AH=05..0x10-Einsprünge (`19ca5…1e4a0`).

Hardware im Original = **PC-Speaker** (Rechteck mit Ton-Slide + Lautstärke-
Hüllkurve). NGPC-Ziel-API (schon im Projekt, `sounds.h`):
`Sfx_PlayToneEx(ch, divider, attn, frames, sw_end, sw_step, sw_speed, sw_ping,
sw_on, env_on, env_step, env_spd)` und `Sfx_PlayNoiseEx(rate, type, attn,
frames, burst, burst_dur, env_on, env_step, env_spd)`. Sfx_Update() läuft schon
in `Sounds_Update()`.

## ID-Landkarte (aus dem Remaster-RE bestätigt)

| ID | Bedeutung | Quelle |
|----|-----------|--------|
| **0x00** | **Spieler-Schuss** (shared shoot sfx) | `docs/formats/weapons.md:220` — **das gesuchte Schussgeräusch** |
| 0x01 | Treffer/Boss-Klaue voll ausgefahren (`[0x8e88]=1`) | `bosses.md:347` |
| 0x07 | Shop: Türen fahren / Tür-Clang | `shop.md:131,242` |
| 0x08 | Shop: Cursor-Bewegung | `shop.md:242` |
| 0x0b | Smart Bomb | `upgrades.md:113` |
| 0x0f | Token/Pickup einsammeln | `upgrades.md:90` |
| 0x12 | Energie-Pickup | `upgrades.md:97` |
| 0x18 | Dive-Move | `upgrades.md:249,261` |

**Shop-Alien:** noch nicht sicher gepinnt. Kandidaten sind die Shop-SFX
(`[0x8e88]`=7/8) bzw. die „play-sample"-Aufrufe über `[0x8e8a]` (`shop.md:242`).
Zum genauen Pinnen: die Stelle finden, an der der Shop-Alien/Händler `[0x8e88]`
bzw. `[0x8e8a]` schreibt (Shop-Update-Code). → offener Schritt.

## Roh-Records (15 Byte je ID, hex, direkt aus XENON2.EXE)

```
id 0x00 @DRV:0x420: c0 ff c0 44 80 3e 10 00 f0 ff 55 00 00 06 01   <- SCHUSS
id 0x01 @DRV:0x42f: a0 00 20 03 80 0c 40 01 c0 fe 55 00 0a 00 01
id 0x02 @DRV:0x43e: 80 02 60 36 20 17 60 05 c0 f9 55 88 04 00 01
id 0x03 @DRV:0x44d: 10 00 e0 01 e0 01 08 00 f8 ff 00 00 00 10 00
id 0x04 @DRV:0x45c: 40 ff c0 08 c0 08 f0 ff 10 00 37 00 0a 00 01
id 0x05 @DRV:0x46b: 80 02 60 36 60 36 60 05 c0 f9 55 2c 0c 02 01
id 0x06 @DRV:0x47a: 80 02 60 1d 40 1a 10 00 f0 ff 55 00 0c 63 01
id 0x07 @DRV:0x489: c0 fe e0 2e e0 2e 10 00 f0 ff 55 00 00 03 01   <- Shop-Tür
id 0x08 @DRV:0x498: 10 00 80 3e c0 5d 20 00 e0 ff 55 ec 02 01 01   <- Shop-Cursor
id 0x09 @DRV:0x4a7: c0 ff 60 04 60 04 10 00 f0 ff 55 00 0a 00 01
id 0x0a @DRV:0x4b6: 80 02 80 3e 80 3e 10 00 f0 ff 55 00 07 63 01
id 0x0b @DRV:0x4c5: a0 01 80 20 f0 2c 60 05 a0 fa d4 44 46 09 01   <- Smart Bomb
id 0x0c @DRV:0x4d4: 50 00 40 1f c0 2b 40 06 60 fa 55 88 50 00 01
id 0x0d @DRV:0x4e3: 90 00 a0 00 b0 00 70 0a 90 f5 55 00 0f 05 01
id 0x0e @DRV:0x4f2: 70 02 50 00 60 00 70 0a 90 f5 55 00 07 00 01
id 0x0f @DRV:0x501: a0 ff c0 08 c0 08 40 ff 30 01 37 00 0f 00 01   <- Pickup
id 0x10 @DRV:0x510: 80 f3 00 64 60 36 40 01 c0 fe 55 00 0b 00 01
id 0x11 @DRV:0x51f: a0 01 80 20 70 20 60 05 a0 fa d4 44 19 09 01
id 0x12 @DRV:0x52e: 00 01 00 2d 00 2d 60 05 a0 fa 55 00 12 0c 01   <- Energie
id 0x13 @DRV:0x53d: 80 06 10 00 10 00 10 01 20 ff 55 ec 05 00 01
id 0x14 @DRV:0x54c: 50 00 80 3e 00 4b a0 05 60 fa 55 88 5a 03 01
id 0x15 @DRV:0x55b: 20 03 80 0c 80 0c 40 01 c0 fe 55 00 0a 00 01
id 0x16 @DRV:0x56a: 90 ff 00 0a 60 09 10 00 f0 ff 55 00 00 04 01
id 0x17 @DRV:0x579: 80 02 c0 26 08 00 10 00 f0 ff 55 00 0a 63 01
id 0x18 @DRV:0x588: 80 02 00 2d 08 00 10 00 f0 ff 55 00 0a 63 01   <- Dive
id 0x19 @DRV:0x597: 80 02 40 33 08 00 10 00 f0 ff 55 00 0a 63 01
```

## Offene Schritte (wenn wir SFX einbauen)
1. **15-Byte-Layout dekodieren** — aus dem SFX-Player `1000:19ca5+` bzw. dem
   AH=3-Loader. Wahrscheinlich: Start-Ton (word), Ton-Step/Slide (word),
   Ton-Grenzen (word), Vol-Hüllkurve, Speed, Repeat — analog zum PC-Speaker-
   Pitch/Vol-Modell (mods-vga.md). Muster: viele Records enden `55 .. .. 01`.
2. **Ton→NGPC** übersetzen: DOS-PIT-Divisor (F=1193182/div) → NGPC-Periode
   (n=96000/F, ≤1023), dann `Sfx_PlayToneEx`/`Sfx_PlayNoiseEx`.
3. **Shop-Alien-ID pinnen** (s.o.).
