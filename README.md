# BoomTown Beats — Clip 96

Clipper en limiter in één, als **Audio Unit** (Logic Pro) en **VST3**.
Gebouwd met [JUCE](https://juce.com) 9.0.4. De klank is één op één overgezet uit het browserprototype in `prototype/`.

## Downloaden en installeren (Mac)

1. Ga naar het tabblad **Actions** van deze repository en open de bovenste groene run *Build macOS (AU + VST3)*.
2. Download onderaan bij **Artifacts** het bestand `Clip96-…`. Je krijgt een zip; dubbelklik die om de `.dmg` te krijgen.
3. Open de `.dmg`.
4. Sluit Logic Pro, open **Terminal** en plak:

   ```
   bash "/Volumes/Clip 96/install.sh"
   ```

5. Start Logic Pro. De plugin staat bij **Audio FX → Audio Units → BoomTown Beats → Clip 96**.

## Wat de plugin doet

- **Push** (0–24 dB) duwt het signaal tegen het plafond. **Ceiling** en **Output**.
- **Mode**: Clip (oversampled clipper), Limit (lookahead-limiter van 1,5 ms) of Clip + Limit (de clipper neemt de
  transients, de limiter de rest). Niets komt boven de Ceiling uit.
- **Clip**: Hardness (zacht afronden tot keihard) en Asym (even harmonischen).
- **Split**: het laag krijgt een eigen clipper (Freq, Low Drive), zodat de kick de rest niet vervormt. Linksom = Off.
- **Limit**: Release en Link (stereokoppeling).
- **Monitor**: Delta laat horen wat er wordt weggehaald, Match zet het niveau terug met de Push-waarde.
- **Meters**: In (na Push), GR (gain reduction) en Out.
- **Quality**: Eco, Normal 2× of High 4×. Latency 2,9 ms (Normal/High) of 1,5 ms (Eco); Logic compenseert dit automatisch.
- 8 presets, waaronder *Hardcore Master*, *Kick Smash* en *Hakkuh Loudness War*.

## Zelf bouwen

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
```

## Licenties

- JUCE: dual-licensed (AGPLv3 of de JUCE 9-licentie); gratis Starter-licentie zolang de omzet onder $20.000 per jaar blijft.
- Fonts: Anton (OFL), Inter (OFL), JetBrains Mono (OFL); licenties staan in `Resources/fonts/`.
