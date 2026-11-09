# `include/tvk/audio` — Layer 8: Retro Synth & Audio Subsystem

- **Layer**: Layer 8 (`tvk/audio`)
- **Responsibility**: Zero-dependency cross-platform audio output (`ALSA`/`PulseAudio`, `WinMM`, `AudioQueue`, `NullAudioBackend`), real-time polyphonic audio mixing, 3D spatial attenuation/panning, and procedural retro chiptune/SFX synthesis (`SynthPatch` + `ADSREnvelope`).
- **Dependency Rules**: Depends on Layer 0 (`tvk/core`) and Layer 2 (`tvk/math`).
- **Technical Specification**: [`docs/specs/06_ASSETS_UI_AND_AUDIO.md`](../../../docs/specs/06_ASSETS_UI_AND_AUDIO.md) | [`ROADMAP.md` (Phase 7)](../../../ROADMAP.md)

## Planned Phase 7 Headers
- `AudioEngine.h` — Cross-platform non-blocking audio device backend, 16-voice polyphonic mixer, and 3D positional audio attenuation.
- `Sound.h` — `Waveform` (`Square`, `Pulse25`, `Triangle`, `Sawtooth`, `WhiteNoise`, `LFSRNoise`), `ADSREnvelope`, `SynthPatch`, and 16-bit PCM `.wav` buffer loader.
