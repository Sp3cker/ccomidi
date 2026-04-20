#ifndef CCOMIDI_PLUGIN_SHARED_H
#define CCOMIDI_PLUGIN_SHARED_H

#include <array>
#include <mutex>
#include <vector>

#include <clap/clap.h>

#include "core/sender_core.h"
#include "plugin/voicegroup_bridge.h"

namespace ccomidi {

struct EditorState;

struct UiRowSnapshot {
  double enabled = 0.0;
  double type = 0.0;
  std::array<double, kMaxCommandFields> values = {};
};

struct UiSnapshot {
  double outputChannel = 0.0;
  double program = 0.0;
  double programEnabled = 1.0;
  std::array<UiRowSnapshot, kMaxCommandRows> rows = {};
};

struct Plugin {
  clap_plugin_t clapPlugin = {};
  const clap_host_t *host = nullptr;
  std::mutex stateMutex = {};
  SenderCore core = {};
  bool active = false;
  EditorState *editor = nullptr;
  clap_id guiTimerId = CLAP_INVALID_ID;
  bool pendingUiChannelChange = false;
  bool pendingUiProgramChange = false;
  std::array<bool, kMaxCommandRows> pendingUiRowChanged = {};
  bool pendingParamInfoRescan = false;
  VoiceSlotLoad voiceLoad = {};
  // Cache-line-isolated: plugin_process reads pendingAddInstrumentIndex on
  // every block, and the editor reads availableSelection every frame once
  // availableInstruments is populated. Without alignas, both fields shared
  // the same cache line as whatever followed voiceLoad — causing false
  // sharing that manifested as UI lag the moment state.json loaded.
  // Index (into voiceLoad.availableInstruments) the UI has asked us to
  // transmit as a CC#98 LSB + CC#99 MSB pair. -1 means nothing pending.
  // Consumed by plugin_process. Valid range 0..16383 (14-bit).
  alignas(64) int pendingAddInstrumentIndex = -1;
  // Last-selected index in the "available instruments" dropdown. UI-local.
  alignas(64) int availableSelection = 0;
};

const char *command_type_name(CommandType type);
void fill_ui_snapshot(Plugin *plugin, UiSnapshot *snapshot);
void apply_ui_param_change(Plugin *plugin, clap_id paramId, double value);
void request_host_param_sync(Plugin *plugin);
void reload_voicegroup_if_changed(Plugin *plugin);

} // namespace ccomidi

#endif
