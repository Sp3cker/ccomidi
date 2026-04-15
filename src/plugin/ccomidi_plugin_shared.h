#ifndef CCOMIDI_PLUGIN_SHARED_H
#define CCOMIDI_PLUGIN_SHARED_H

#include <array>
#include <mutex>

#include <clap/clap.h>

#include "core/sender_core.h"

namespace ccomidi {

struct EditorState;

struct UiRowSnapshot {
  double enabled = 0.0;
  double type = 0.0;
  std::array<double, kMaxCommandFields> values = {};
};

struct UiSnapshot {
  double outputChannel = 0.0;
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
  std::array<bool, kMaxCommandRows> pendingUiRowChanged = {};
  bool pendingParamInfoRescan = false;
};

const char *command_type_name(CommandType type);
void fill_ui_snapshot(Plugin *plugin, UiSnapshot *snapshot);
void apply_ui_param_change(Plugin *plugin, clap_id paramId, double value);
void request_host_param_sync(Plugin *plugin);

} // namespace ccomidi

#endif
