#include <clap/clap.h>
#include <clap/events.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/render.h>
#include <clap/ext/state.h>
#include <clap/fixedpoint.h>
#include <clap/plugin-features.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include "plugin/recorder/recorder_editor.h"
#include "plugin/recorder/recorder_plugin_shared.h"
#include "recorder/recorder_core.h"
#include "recorder/smf_writer.h"

namespace ccomidi {

RecorderPlugin *recorder_from_plugin(const clap_plugin_t *plugin) {
  return plugin ? static_cast<RecorderPlugin *>(plugin->plugin_data) : nullptr;
}

void request_recorder_save(RecorderPlugin *plugin, const std::string &path) {
  if (!plugin)
    return;
  {
    std::lock_guard<std::mutex> lock(plugin->saveMutex);
    plugin->savePath = path;
    plugin->pendingSave = true;
  }
  if (plugin->host && plugin->host->request_callback)
    plugin->host->request_callback(plugin->host);
}

void request_recorder_clear(RecorderPlugin *plugin) {
  if (!plugin)
    return;
  {
    std::lock_guard<std::mutex> lock(plugin->saveMutex);
    plugin->pendingClear = true;
  }
  if (plugin->host && plugin->host->request_callback)
    plugin->host->request_callback(plugin->host);
}

void set_recorder_save_path(RecorderPlugin *plugin, const std::string &path) {
  if (!plugin)
    return;
  std::lock_guard<std::mutex> lock(plugin->saveMutex);
  plugin->savePath = path;
}

void fill_recorder_ui_snapshot(RecorderPlugin *plugin,
                               RecorderUiSnapshot *snapshot) {
  if (!plugin || !snapshot)
    return;
  std::lock_guard<std::mutex> lock(plugin->saveMutex);
  snapshot->lastStatus = plugin->lastStatus;
  snapshot->savePath = plugin->savePath;
}

void destroy_recorder_editor(RecorderPlugin *plugin) {
  if (!plugin || !plugin->editor)
    return;
  RecorderEditorState *editor = plugin->editor;
  plugin->editor = nullptr;
  recorder_editor_prepare_destroy(editor);
  recorder_editor_destroy(editor);
}

bool plugin_init(const clap_plugin_t *plugin) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self)
    return false;
  self->core.reset();
  return true;
}

void plugin_destroy(const clap_plugin_t *plugin) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  destroy_recorder_editor(self);
  delete self;
}

bool plugin_activate(const clap_plugin_t *plugin, double sampleRate,
                     std::uint32_t minFrames, std::uint32_t maxFrames) {
  (void)minFrames;
  (void)maxFrames;
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self)
    return false;
  self->sampleRate = sampleRate;
  self->core.set_sample_rate(sampleRate);
  // Pre-reserve to avoid heap reallocation under the mutex on the audio
  // thread. ~10 minutes of dense MIDI fits in 64K events.
  self->core.reserve(65536, 1024);
  self->active.store(true);
  return true;
}

void plugin_deactivate(const clap_plugin_t *plugin) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (self)
    self->active.store(false);
}

bool plugin_start_processing(const clap_plugin_t *plugin) {
  (void)plugin;
  return true;
}

void plugin_stop_processing(const clap_plugin_t *plugin) { (void)plugin; }

void plugin_reset(const clap_plugin_t *plugin) { (void)plugin; }

clap_process_status plugin_process(const clap_plugin_t *plugin,
                                   const clap_process_t *process) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self || !process)
    return CLAP_PROCESS_ERROR;

  bool isPlaying = true;
  if (process->transport)
    isPlaying = (process->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;

  if (isPlaying && process->transport &&
      (process->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) {
    self->core.set_tempo_in_block(0, process->transport->tempo);
  }

  if (isPlaying && process->transport &&
      (process->transport->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE)) {
    // every playing block with HAS_SECONDS_TIMELINE, we decode
    // loop_start_seconds / loop_end_seconds / song_pos_seconds from their
    // fixed-point form and push to the core. When IS_LOOP_ACTIVE is off, the
    // core clears hasLoop — so toggling loop off in the DAW just drops the
    // markers.
    const auto *tp = process->transport;
    const bool loopActive = (tp->flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE) != 0;
    const double factor = static_cast<double>(CLAP_SECTIME_FACTOR);
    const double songSec = static_cast<double>(tp->song_pos_seconds) / factor;
    const double loopStartSec =
        static_cast<double>(tp->loop_start_seconds) / factor;
    const double loopEndSec =
        static_cast<double>(tp->loop_end_seconds) / factor;
    self->core.update_loop_from_transport(loopActive, loopStartSec, loopEndSec,
                                          songSec);
  }

  const std::uint32_t count =
      process->in_events ? process->in_events->size(process->in_events) : 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    const clap_event_header_t *header =
        process->in_events->get(process->in_events, i);
    if (!header)
      continue;

    if (process->out_events)
      process->out_events->try_push(process->out_events, header);

    if (isPlaying && header->space_id == CLAP_CORE_EVENT_SPACE_ID) {
      if (header->type == CLAP_EVENT_MIDI) {
        const auto *midi = reinterpret_cast<const clap_event_midi_t *>(header);
        self->core.push_event_in_block(header->time, midi->data[0],
                                       midi->data[1], midi->data[2]);
      } else if (header->type == CLAP_EVENT_NOTE_ON ||
                 header->type == CLAP_EVENT_NOTE_OFF ||
                 header->type == CLAP_EVENT_NOTE_CHOKE) {
        const auto *note = reinterpret_cast<const clap_event_note_t *>(header);
        const std::uint8_t channel =
            static_cast<std::uint8_t>(note->channel & 0x0F);
        const std::uint8_t key = static_cast<std::uint8_t>(
            std::clamp<int>(static_cast<int>(note->key), 0, 127));
        std::uint8_t velocity = static_cast<std::uint8_t>(std::clamp<int>(
            static_cast<int>(std::lround(note->velocity * 127.0)), 0, 127));
        const bool isNoteOn = header->type == CLAP_EVENT_NOTE_ON;
        const std::uint8_t status =
            static_cast<std::uint8_t>((isNoteOn ? 0x90 : 0x80) | channel);
        if (isNoteOn && velocity == 0)
          velocity = 1;
        if (header->type == CLAP_EVENT_NOTE_CHOKE)
          velocity = 0;
        self->core.push_event_in_block(header->time, status, key, velocity);
      }
    }
  }

  if (isPlaying)
    self->core.advance_block(process->frames_count);

  // Zero the declared stereo audio output so the host receives defined
  // silence. See audio_ports_count for why the bus exists.
  if (process->audio_outputs && process->audio_outputs_count > 0) {
    const clap_audio_buffer_t &out = process->audio_outputs[0];
    for (std::uint32_t ch = 0; ch < out.channel_count; ++ch) {
      if (out.data32 && out.data32[ch])
        std::memset(out.data32[ch], 0, sizeof(float) * process->frames_count);
      if (out.data64 && out.data64[ch])
        std::memset(out.data64[ch], 0, sizeof(double) * process->frames_count);
    }
  }
  return CLAP_PROCESS_CONTINUE;
}

const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id);
void plugin_on_main_thread(const clap_plugin_t *plugin);

const char *s_features[] = {
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t s_descriptor = {
    .clap_version = CLAP_VERSION,
    .id = "com.sallegrezza.ccomidi.recorder",
    .name = "ccomidi recorder",
    .vendor = "sallegrezza",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description =
        "Buffers incoming MIDI and writes SMF Type 1 on user command",
    .features = s_features,
};

const clap_plugin_t s_pluginPrototype = {
    .desc = &s_descriptor,
    .plugin_data = nullptr,
    .init = plugin_init,
    .destroy = plugin_destroy,
    .activate = plugin_activate,
    .deactivate = plugin_deactivate,
    .start_processing = plugin_start_processing,
    .stop_processing = plugin_stop_processing,
    .reset = plugin_reset,
    .process = plugin_process,
    .get_extension = plugin_get_extension,
    .on_main_thread = plugin_on_main_thread,
};

bool gui_is_api_supported(const clap_plugin_t *plugin, const char *api,
                          bool isFloating) {
  (void)plugin;
  if (isFloating)
    return true;
#if defined(_WIN32)
  return api && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#elif defined(__APPLE__)
  return api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#else
  return api && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
}

bool gui_get_preferred_api(const clap_plugin_t *plugin, const char **api,
                           bool *isFloating) {
  (void)plugin;
  if (!api || !isFloating)
    return false;
  *isFloating = false;
#if defined(_WIN32)
  *api = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
  *api = CLAP_WINDOW_API_COCOA;
#else
  *api = CLAP_WINDOW_API_X11;
#endif
  return true;
}

bool gui_create(const clap_plugin_t *plugin, const char *api, bool isFloating) {
  (void)api;
  (void)isFloating;
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self || self->editor)
    return false;
  self->editor = recorder_editor_create(self);
  return self->editor != nullptr;
}

void gui_destroy(const clap_plugin_t *plugin) {
  destroy_recorder_editor(recorder_from_plugin(plugin));
}

bool gui_set_scale(const clap_plugin_t *plugin, double scale) {
  (void)plugin;
  (void)scale;
  return false;
}

bool gui_get_size(const clap_plugin_t *plugin, std::uint32_t *width,
                  std::uint32_t *height) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self || !self->editor)
    return false;
  recorder_editor_get_size(self->editor, width, height);
  return true;
}

bool gui_can_resize(const clap_plugin_t *plugin) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  return self && self->editor && recorder_editor_can_resize(self->editor);
}

bool gui_get_resize_hints(const clap_plugin_t *plugin,
                          clap_gui_resize_hints_t *hints) {
  (void)plugin;
  if (!hints)
    return false;
  hints->can_resize_horizontally = true;
  hints->can_resize_vertically = true;
  hints->preserve_aspect_ratio = false;
  hints->aspect_ratio_width = 0;
  hints->aspect_ratio_height = 0;
  return true;
}

bool gui_adjust_size(const clap_plugin_t *plugin, std::uint32_t *width,
                     std::uint32_t *height) {
  (void)plugin;
  (void)width;
  (void)height;
  return true;
}

bool gui_set_size(const clap_plugin_t *plugin, std::uint32_t width,
                  std::uint32_t height) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  return self && self->editor &&
         recorder_editor_set_size(self->editor, width, height);
}

bool gui_set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self || !self->editor || !window)
    return false;

  std::uintptr_t nativeParent = 0;
#if defined(_WIN32)
  nativeParent = reinterpret_cast<std::uintptr_t>(window->win32);
#elif defined(__APPLE__)
  nativeParent = reinterpret_cast<std::uintptr_t>(window->cocoa);
#else
  nativeParent = static_cast<std::uintptr_t>(window->x11);
#endif
  return recorder_editor_set_parent(self->editor, nativeParent);
}

bool gui_set_transient(const clap_plugin_t *plugin,
                       const clap_window_t *window) {
  (void)plugin;
  (void)window;
  return true;
}

void gui_suggest_title(const clap_plugin_t *plugin, const char *title) {
  (void)plugin;
  (void)title;
}

bool gui_show(const clap_plugin_t *plugin) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self || !self->editor)
    return false;
  const bool shown = recorder_editor_show(self->editor);
  if (shown)
    recorder_editor_start_timer(self->editor);
  return shown;
}

bool gui_hide(const clap_plugin_t *plugin) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self || !self->editor)
    return false;
  return recorder_editor_hide(self->editor);
}

const clap_plugin_gui_t s_gui = {
    .is_api_supported = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create = gui_create,
    .destroy = gui_destroy,
    .set_scale = gui_set_scale,
    .get_size = gui_get_size,
    .can_resize = gui_can_resize,
    .get_resize_hints = gui_get_resize_hints,
    .adjust_size = gui_adjust_size,
    .set_size = gui_set_size,
    .set_parent = gui_set_parent,
    .set_transient = gui_set_transient,
    .suggest_title = gui_suggest_title,
    .show = gui_show,
    .hide = gui_hide,
};

std::uint32_t audio_ports_count(const clap_plugin_t *plugin, bool isInput) {
  (void)plugin;
  // Ableton rejects VST3 plugins without an audio output bus. Declare a
  // stereo output; `plugin_process` writes silence to it. MIDI still flows
  // through the event buses.
  return isInput ? 0u : 1u;
}

bool audio_ports_get(const clap_plugin_t *plugin, std::uint32_t index,
                     bool isInput, clap_audio_port_info_t *info) {
  (void)plugin;
  if (isInput || index != 0 || !info)
    return false;
  std::memset(info, 0, sizeof(*info));
  info->id = 0;
  std::snprintf(info->name, sizeof(info->name), "%s", "Audio Output");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

const clap_plugin_audio_ports_t s_audioPorts = {
    .count = audio_ports_count,
    .get = audio_ports_get,
};

std::uint32_t note_ports_count(const clap_plugin_t *plugin, bool isInput) {
  (void)plugin;
  (void)isInput;
  return 1u;
}

bool note_ports_get(const clap_plugin_t *plugin, std::uint32_t index,
                    bool isInput, clap_note_port_info_t *info) {
  (void)plugin;
  if (index != 0 || !info)
    return false;

  std::memset(info, 0, sizeof(*info));
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
  std::snprintf(info->name, sizeof(info->name), "%s",
                isInput ? "MIDI Input" : "MIDI Output");
  return true;
}

const clap_plugin_note_ports_t s_notePorts = {
    .count = note_ports_count,
    .get = note_ports_get,
};

bool render_has_hard_realtime_requirement(const clap_plugin_t *plugin) {
  (void)plugin;
  return false;
}

// Note: renderMode is atomic and exchange returns the prior value, so the
// transition detection is race-free even if the host were to call render_set
// from an unexpected thread.
bool render_set(const clap_plugin_t *plugin, clap_plugin_render_mode mode) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self)
    return false;
  const std::int32_t prev =
      self->renderMode.exchange(static_cast<std::int32_t>(mode));
  const bool wasOffline = prev == CLAP_RENDER_OFFLINE;
  const bool nowOffline = mode == CLAP_RENDER_OFFLINE;
  if (!wasOffline && nowOffline) {
    self->core.reset();
  } else if (wasOffline && !nowOffline) {
    {
      std::lock_guard<std::mutex> lock(self->saveMutex);
      self->pendingSave = true;
    }
    if (self->host && self->host->request_callback)
      self->host->request_callback(self->host);
  }
  return true;
}

const clap_plugin_render_t s_render = {
    .has_hard_realtime_requirement = render_has_hard_realtime_requirement,
    .set = render_set,
};

constexpr std::uint32_t kStateVersion = 1;

bool state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self || !stream)
    return false;

  std::string path;
  {
    std::lock_guard<std::mutex> lock(self->saveMutex);
    path = self->savePath;
  }

  const std::uint32_t version = kStateVersion;
  if (stream->write(stream, &version, sizeof(version)) !=
      static_cast<std::int64_t>(sizeof(version)))
    return false;

  const std::uint32_t pathLen = static_cast<std::uint32_t>(path.size());
  if (stream->write(stream, &pathLen, sizeof(pathLen)) !=
      static_cast<std::int64_t>(sizeof(pathLen)))
    return false;

  if (pathLen > 0 && stream->write(stream, path.data(), pathLen) !=
                         static_cast<std::int64_t>(pathLen))
    return false;

  return true;
}

bool state_load(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self || !stream)
    return false;

  std::uint32_t version = 0;
  if (stream->read(stream, &version, sizeof(version)) !=
      static_cast<std::int64_t>(sizeof(version)))
    return false;
  if (version != kStateVersion)
    return false;

  std::uint32_t pathLen = 0;
  if (stream->read(stream, &pathLen, sizeof(pathLen)) !=
      static_cast<std::int64_t>(sizeof(pathLen)))
    return false;

  std::string path;
  if (pathLen > 0) {
    path.resize(pathLen);
    if (stream->read(stream, path.data(), pathLen) !=
        static_cast<std::int64_t>(pathLen))
      return false;
  }

  std::lock_guard<std::mutex> lock(self->saveMutex);
  self->savePath = std::move(path);
  return true;
}

const clap_plugin_state_t s_state = {
    .save = state_save,
    .load = state_load,
};

const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id) {
  (void)plugin;
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
    return &s_audioPorts;
  if (std::strcmp(id, CLAP_EXT_GUI) == 0)
    return &s_gui;
  if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
    return &s_notePorts;
  if (std::strcmp(id, CLAP_EXT_RENDER) == 0)
    return &s_render;
  if (std::strcmp(id, CLAP_EXT_STATE) == 0)
    return &s_state;
  return nullptr;
}

void plugin_on_main_thread(const clap_plugin_t *plugin) {
  RecorderPlugin *self = recorder_from_plugin(plugin);
  if (!self)
    return;

  bool doSave = false;
  bool doClear = false;
  std::string path;
  {
    std::lock_guard<std::mutex> lock(self->saveMutex);
    doSave = self->pendingSave;
    doClear = self->pendingClear;
    path = self->savePath;
    self->pendingSave = false;
    self->pendingClear = false;
  }

  if (doClear)
    self->core.reset();

  if (doSave && !path.empty()) {
    RecorderCore::Snapshot snap = self->core.snapshot();
    SmfWriteOptions options;
    options.ppq = 96;
    options.fallbackBpm = 120.0;
    const bool ok = write_smf1(path, snap, options);
    std::lock_guard<std::mutex> lock(self->saveMutex);
    self->lastStatus =
        (ok ? std::string("Saved: ") : std::string("Save failed: ")) + path;
  }
}

std::uint32_t factory_get_plugin_count(const clap_plugin_factory_t *factory) {
  (void)factory;
  return 1;
}

const clap_plugin_descriptor_t *
factory_get_plugin_descriptor(const clap_plugin_factory_t *factory,
                              std::uint32_t index) {
  (void)factory;
  return index == 0 ? &s_descriptor : nullptr;
}

const clap_plugin_t *factory_create_plugin(const clap_plugin_factory_t *factory,
                                           const clap_host_t *host,
                                           const char *pluginId) {
  (void)factory;
  if (!host || !pluginId || std::strcmp(pluginId, s_descriptor.id) != 0)
    return nullptr;
  if (!clap_version_is_compatible(host->clap_version))
    return nullptr;

  auto *self = new (std::nothrow) RecorderPlugin();
  if (!self)
    return nullptr;

  self->host = host;
  self->clapPlugin = s_pluginPrototype;
  self->clapPlugin.plugin_data = self;
  return &self->clapPlugin;
}

const clap_plugin_factory_t s_factory = {
    .get_plugin_count = factory_get_plugin_count,
    .get_plugin_descriptor = factory_get_plugin_descriptor,
    .create_plugin = factory_create_plugin,
};

bool entry_init(const char *pluginPath) {
  (void)pluginPath;
  return true;
}

void entry_deinit(void) {}

const void *entry_get_factory(const char *factoryId) {
  if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
    return &s_factory;
  return nullptr;
}

} // namespace ccomidi

extern "C" const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = ccomidi::entry_init,
    .deinit = ccomidi::entry_deinit,
    .get_factory = ccomidi::entry_get_factory,
};
