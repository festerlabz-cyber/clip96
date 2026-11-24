# Clip 96 — project notes

BoomTown Beats Clip 96: JUCE 9.0.4 audio effect (AU + VST3), oversampled clipper plus lookahead limiter. Part of the
BoomTown Beats 96 line (festerlabz-cyber/console96, reverb96, delay96, kick96, pump96), sharing their look and build setup.
Owner speaks Dutch; write user-facing docs in Dutch, GUI text in English.

## Layout
- `Source/Clip96DSP.h` — whole DSP engine, no JUCE dependency. Port of `prototype/dsp.js`; keep them in sync
  (`tests/dsp_pairs.cpp` output matches the JS engine to float precision for all modes and qualities).
  Push, optional split (lows pre-clipped on their own, complementary x − LP so Off is exact), oversampled clip curve
  `c·u/(1+|u|^p)^(1/p)` with p = 2…20 (Hardness) and Asym, lookahead limiter (1.5 ms hold-min + box filter + release,
  stereo link), safety clamp at the ceiling, Delta / Match, output, bypass. Clip + Limit puts the clipper 1.5 dB above
  the ceiling. The limiter delay is always in the path: latency = 64 (2×/4×, 0 in Eco) + lookahead − 1, mode-independent.
- `Source/Parameters.h` — parameter IDs (same keys as the prototype), ranges, formatting, 8 factory presets.
- `Source/PluginProcessor.*` — APVTS, presets as programs, bypass parameter, latency follows Quality and sample rate,
  meters (level after Push, output, total peak reduction) as atomics read by the GUI.
- `Source/PluginEditor.*`, `UIComponents.h` — GUI at 1160×372, scaled as a whole. Same layout as the prototype:
  Ceiling/Output column, big Push knob, Clip / Split / Limit groups, Monitor (Delta, Match, latency), Meters (In, GR, Out),
  bottom bar Mode / Quality / Presets.
- `.github/workflows/build-macos.yml` — universal AU + VST3, auval, DMG artifact with install script.
- `.github/workflows/build-windows.yml` — Windows x64 VST3 (MSVC, static runtime), pluginval strictness 5, zip artifact
  with `packaging/LEESMIJ-Windows.txt`.

## Decisions
- Manufacturer code `Btbt`, plug-in code `Cl96`, bundle id `com.boomtownbeats.clip96`. Never change these.
- Own algorithm and names; do not use other brands' names.
- Presets never change Quality, Delta, Match or Bypass.
- `g.setOpacity (1.0f)` before `drawImage`: drawing uses the current opacity left by earlier `setColour` calls.

## Testing
- `cmake -S . -B build-linux -G Ninja -DJUCE_DIR=<juce> -DCL96_BUILD_TESTS=ON && cmake --build build-linux --target Clip96RenderTest`
  then `xvfb-run -a build-linux/Clip96RenderTest_artefacts/Release/Clip96RenderTest <outdir>`: per-preset levels and
  ceiling check on a loud mix, measured vs reported latency per quality, Delta null test, state round trip, GUI PNGs.
