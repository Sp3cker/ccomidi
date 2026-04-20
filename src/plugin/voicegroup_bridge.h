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

// Records the .clap bundle path (from entry_init's pluginPath) so later
// bridge calls can find poryaaaa_state.json next to the bundle.
void voicegroup_bridge_set_plugin_path(const char *pluginPath);

// Returns the current mtime of state.json in ns (0 if not found). Fast
// (single stat). Use to decide whether to call load_state().
long long voicegroup_bridge_state_mtime();

// Reads poryaaaa_state.json and returns the parsed load. On failure, `slots`
// is empty and `error` describes the problem.
VoiceSlotLoad voicegroup_bridge_load_state();

}  // namespace ccomidi

#endif
