#include "plugin/recorder/recorder_editor.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "imgui.h"

#include "gui/editor_shell.h"

namespace ccomidi {

namespace {

constexpr std::uint32_t kDefaultWidth = 640;
constexpr std::uint32_t kDefaultHeight = 360;
constexpr std::size_t kPathBufferSize = 512;

const char *render_mode_label(std::int32_t mode) {
  return mode == CLAP_RENDER_OFFLINE ? "offline" : "realtime";
}

} // namespace

struct RecorderEditorState {
  RecorderPlugin *plugin = nullptr;
  EditorShell *shell = nullptr;
  char pathBuffer[kPathBufferSize] = {};
  bool pathInitialized = false;
};

namespace {

void ensure_path_initialized(RecorderEditorState *editor,
                             const RecorderUiSnapshot &snapshot) {
  if (editor->pathInitialized)
    return;
  if (!snapshot.savePath.empty()) {
    std::snprintf(editor->pathBuffer, sizeof(editor->pathBuffer), "%s",
                  snapshot.savePath.c_str());
  } else {
    std::snprintf(editor->pathBuffer, sizeof(editor->pathBuffer), "%s",
                  "/tmp/ccomidi_recorder.mid");
  }
  editor->pathInitialized = true;
}

void draw_frame(void *userData, std::uint32_t width, std::uint32_t height) {
  auto *editor = static_cast<RecorderEditorState *>(userData);
  if (!editor || !editor->plugin)
    return;

  RecorderUiSnapshot snapshot;
  fill_recorder_ui_snapshot(editor->plugin, &snapshot);
  ensure_path_initialized(editor, snapshot);

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(
      ImVec2(static_cast<float>(width), static_cast<float>(height)));
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

  ImGui::Begin("##ccomidi_recorder", nullptr, flags);

  ImGui::TextUnformatted("ccomidi recorder");
  ImGui::SameLine();
  ImGui::TextDisabled("MIDI capture to SMF Type 1");
  ImGui::Separator();

  ImGui::Text("State: %s  |  Render: %s", snapshot.active ? "active" : "idle",
              render_mode_label(snapshot.renderMode));
  ImGui::Text("MIDI events: %zu", snapshot.midiEventCount);
  ImGui::Text("Tempo events: %zu", snapshot.tempoEventCount);
  ImGui::Text("Duration: %.3f s", snapshot.durationSeconds);
  if (snapshot.lastTempoBpm > 0.0)
    ImGui::Text("Last tempo: %.2f BPM", snapshot.lastTempoBpm);
  else
    ImGui::TextDisabled("Last tempo: (none observed)");

  ImGui::Spacing();
  ImGui::TextUnformatted("Output file");
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputText("##path", editor->pathBuffer, sizeof(editor->pathBuffer)))
    set_recorder_save_path(editor->plugin, editor->pathBuffer);

  ImGui::Spacing();
  if (ImGui::Button("Save SMF")) {
    std::string path(editor->pathBuffer);
    if (!path.empty())
      request_recorder_save(editor->plugin, path);
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear Buffer"))
    request_recorder_clear(editor->plugin);

  ImGui::Spacing();
  if (!snapshot.lastStatus.empty())
    ImGui::TextWrapped("%s", snapshot.lastStatus.c_str());
  else
    ImGui::TextDisabled("No save attempted yet.");

  ImGui::End();
}

} // namespace

RecorderEditorState *recorder_editor_create(RecorderPlugin *plugin) {
  auto *editor = new RecorderEditorState();
  editor->plugin = plugin;

  EditorShellConfig config = {};
  config.title = "ccomidi recorder";
  config.className = "ccomidi_recorder";


  EditorShellCallbacks callbacks = {};
  callbacks.userData = editor;
  callbacks.drawFrame = &draw_frame;

  editor->shell = editor_shell_create(plugin ? plugin->host : nullptr, config,
                                      callbacks);
  if (!editor->shell) {
    delete editor;
    return nullptr;
  }
  return editor;
}

void recorder_editor_prepare_destroy(RecorderEditorState *editor) {
  if (!editor)
    return;
  editor->plugin = nullptr;
  editor_shell_prepare_destroy(editor->shell);
}

void recorder_editor_destroy(RecorderEditorState *editor) {
  if (!editor)
    return;
  editor_shell_destroy(editor->shell);
  delete editor;
}

bool recorder_editor_show(RecorderEditorState *editor) {
  return editor && editor_shell_show(editor->shell);
}

bool recorder_editor_hide(RecorderEditorState *editor) {
  return editor && editor_shell_hide(editor->shell);
}

bool recorder_editor_set_parent(RecorderEditorState *editor,
                                std::uintptr_t nativeParent) {
  return editor && editor_shell_set_parent(editor->shell, nativeParent);
}

bool recorder_editor_set_size(RecorderEditorState *editor, std::uint32_t width,
                              std::uint32_t height) {
  return editor && editor_shell_set_size(editor->shell, width, height);
}

void recorder_editor_get_size(RecorderEditorState *editor,
                              std::uint32_t *width, std::uint32_t *height) {
  if (!editor) {
    if (width)
      *width = kDefaultWidth;
    if (height)
      *height = kDefaultHeight;
    return;
  }
  editor_shell_get_size(editor->shell, width, height);
}

bool recorder_editor_can_resize(RecorderEditorState *editor) {
  return editor && editor_shell_can_resize(editor->shell);
}

void recorder_editor_start_timer(RecorderEditorState *editor) {
  if (editor)
    editor_shell_start_timer(editor->shell);
}

void recorder_editor_stop_timer(RecorderEditorState *editor) {
  if (editor)
    editor_shell_stop_timer(editor->shell);
}

} // namespace ccomidi
