# patches/

Patches applied to vendored submodules at CMake configure time. The top-level
`CMakeLists.txt` greps each patched file for a sentinel string and calls
`git apply` only when the sentinel is absent — safe against re-runs and
against `git submodule update --init --recursive` wiping the submodule's
working tree.

## clap-wrapper-midi-event-bridge.patch

Target: `third_party/clap-wrapper/src/detail/vst3/process.cpp`
Pinned SHA: `62bf4f193d8dac354a946c1e0cffda9174317042`

Upstream `clap-wrapper` drops `CLAP_EVENT_MIDI` on both sides of the
VST3<->CLAP bridge. This patch fills both gaps:

* **Output side (`enqueueOutputEvent`)**: translate `CLAP_EVENT_MIDI` by
  status-byte nibble into `Vst::Event::kNoteOnEvent` / `kNoteOffEvent` /
  `kPolyPressureEvent` / `kLegacyMIDICCOutEvent` so raw MIDI emitted by the
  CLAP plugin actually reaches the VST3 host.
* **Input side (`processInputEvents`)**: decode incoming
  `kLegacyMIDICCOutEvent` back into `CLAP_EVENT_MIDI` with the right
  status byte (CC, Program Change, Pitch Bend, Channel Pressure,
  Polyphonic Key Pressure) so downstream CLAP code handles it unchanged.

Sentinel: `kLegacyMIDICCOutEvent` — absent in the pristine `62bf4f1`
`process.cpp`, present once this patch is applied.

## Upstreaming

The plan is to open a PR against `free-audio/clap-wrapper` with the same
translation; if it lands we bump the submodule SHA and delete this
directory.
