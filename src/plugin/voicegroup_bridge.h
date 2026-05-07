#ifndef CCOMIDI_VOICEGROUP_BRIDGE_H
#define CCOMIDI_VOICEGROUP_BRIDGE_H

#include <string>
#include <vector>

namespace ccomidi {

struct VoiceSlot {
  int program;        // 0-127, the MIDI Program Change value
  std::string name;   // sample basename, never empty
};

struct VoiceSlotLoad {
  std::vector<VoiceSlot> slots;
  std::string statePath;   // path the bridge looked at (empty if unknown)
  std::string error;       // empty on success; otherwise why no slots are shown
  long long mtimeNs = 0;   // mtime of state file when parsed, 0 if not found
};

// Returns the current mtime of the state file in ns (0 if not found). Fast
// (single stat). Use to decide whether to call load_state().
//
// The state file lives at a fixed per-user location (on macOS:
// `~/Library/Application Support/poryaaaa/state.json`) so that it works
// across CLAP and VST3 installations without knowing where the plugin
// bundle was placed.
long long voicegroup_bridge_state_mtime();

// Reads the state file and returns the parsed load. On failure, `slots` is
// empty and `error` describes the problem.
VoiceSlotLoad voicegroup_bridge_load_state();

}  // namespace ccomidi

#endif
