#ifndef CCOMIDI_RECORDER_PLUGIN_SHARED_H
#define CCOMIDI_RECORDER_PLUGIN_SHARED_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <clap/clap.h>
#include <clap/ext/render.h>

#include "recorder/recorder_core.h"

namespace ccomidi {

struct RecorderEditorState;

struct RecorderPlugin {
  clap_plugin_t clapPlugin = {};
  const clap_host_t *host = nullptr;
  std::atomic<bool> active{false};
  std::atomic<std::int32_t> renderMode{CLAP_RENDER_REALTIME};
  double sampleRate = 44100.0;
  RecorderCore core;
  RecorderEditorState *editor = nullptr;

  std::mutex saveMutex;
  std::string savePath;
  std::string lastStatus;
  bool pendingSave = false;
  bool pendingClear = false;
};

void request_recorder_save(RecorderPlugin *plugin, const std::string &path);
void request_recorder_clear(RecorderPlugin *plugin);

struct RecorderUiSnapshot {
  std::size_t midiEventCount = 0;
  std::size_t tempoEventCount = 0;
  double durationSeconds = 0.0;
  double lastTempoBpm = 0.0;
  std::int32_t renderMode = CLAP_RENDER_REALTIME;
  bool active = false;
  std::string lastStatus;
  std::string savePath;
};

void fill_recorder_ui_snapshot(RecorderPlugin *plugin,
                               RecorderUiSnapshot *snapshot);
void set_recorder_save_path(RecorderPlugin *plugin, const std::string &path);

} // namespace ccomidi

#endif
