# `src/audio` — Layer 8 Implementation (`tvk/audio`)

- **Layer**: Layer 8 (`tvk/audio`)
- **Public Headers**: [`include/tvk/audio/`](../../include/tvk/audio/README.md)
- **Technical Specification**: [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](../../docs/specs/06_ASSETS_UI_AND_AUDIO.md) | [`ROADMAP.md` (Phase 7)](../../ROADMAP.md)

## Planned Phase 7 Sources
- `AudioEngine.cpp` — Zero-dependency OS audio device backends (`ALSA`/`PulseAudio`, `WinMM`, `AudioQueue`, `NullAudioBackend`) and polyphonic mixer thread.
- `Sound.cpp` — Procedural `SynthPatch` oscillator + `ADSREnvelope` synthesizer and 16-bit PCM `.wav` loader.
