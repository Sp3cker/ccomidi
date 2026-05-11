#ifndef CCOMIDI_PLUGIN_SHARED_H
#define CCOMIDI_PLUGIN_SHARED_H

#include <array>
#include <mutex>
#include <utility>
#include <vector>

#include <clap/clap.h>

#include "core/sender_core.h"
#include "ipc/pc_bus.h"
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
  // GUI knob tweaks queued for emission as CLAP param events so the host
  // (e.g. FL Studio's "Last Tweaked Parameter") learns which param the user
  // touched. Drained from params.flush and process.
  std::vector<std::pair<clap_id, double>> pendingUiParamEvents = {};
  bool pendingParamInfoRescan = false;
  VoiceSlotLoad voiceLoad = {};
  // PC sidechannel writer. Opened in plugin_activate; survives across the
  // plugin's lifetime. publish() is RT-safe.
  ipc::PCBus pcBus = {};
};

const char *command_type_name(CommandType type);
void fill_ui_snapshot(Plugin *plugin, UiSnapshot *snapshot);
void apply_ui_param_change(Plugin *plugin, clap_id paramId, double value);
void request_host_param_sync(Plugin *plugin);
void reload_voicegroup_if_changed(Plugin *plugin);

} // namespace ccomidi

#endif
