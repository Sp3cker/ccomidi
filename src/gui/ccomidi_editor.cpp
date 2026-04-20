#include "gui/ccomidi_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

#include "imgui.h"

#include "gui/editor_shell.h"
#include "plugin/param_ids.h"

namespace ccomidi {

namespace {

constexpr std::uint32_t kDefaultWidth = 980;
constexpr std::uint32_t kDefaultHeight = 620;

int field_count_for_type(CommandType type) {
  switch (type) {
  case CommandType::None:
    return 0;
  case CommandType::MemAcc0C:
  case CommandType::MemAcc10:
    return 4;
  default:
    return 1;
  }
}

const char *field_label(CommandType type, int field) {
  if (type == CommandType::MemAcc0C || type == CommandType::MemAcc10) {
    static const char *labels[] = {"Op", "Arg1", "Arg2", "Data"};
    return labels[field];
  }

  switch (field) {
  case 0:
    return "Value";
  case 1:
    return "B";
  case 2:
    return "C";
  case 3:
    return "D";
  default:
    return "";
  }
}

std::uint8_t floor_to_u8(double value, std::uint8_t minValue,
                         std::uint8_t maxValue) {
  const double floored = std::floor(value);
  if (floored <= static_cast<double>(minValue))
    return minValue;
  if (floored >= static_cast<double>(maxValue))
    return maxValue;
  return static_cast<std::uint8_t>(floored);
}

bool is_table_command_type(CommandType type) {
  return type == CommandType::None || !is_fixed_command_type(type);
}

void append_preview_triplet(std::string *preview, std::uint8_t channel,
                            std::uint8_t controller, std::uint8_t value) {
  if (!preview)
    return;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "B%X %02X %02X", channel, controller,
                value);
  if (!preview->empty())
    preview->append("  ");
  preview->append(buffer);
}

std::string preview_for_row(const UiSnapshot &snapshot, std::size_t row) {
  const CommandType type = static_cast<CommandType>(
      static_cast<int>(std::floor(snapshot.rows[row].type)));
  const bool enabled = std::floor(snapshot.rows[row].enabled) >= 1.0;
  if (!enabled || type == CommandType::None)
    return "-";

  const std::uint8_t channel = floor_to_u8(snapshot.outputChannel, 0, 15);
  const std::uint8_t value0 = floor_to_u8(snapshot.rows[row].values[0], 0, 127);
  const std::uint8_t value1 = floor_to_u8(snapshot.rows[row].values[1], 0, 127);
  const std::uint8_t value2 = floor_to_u8(snapshot.rows[row].values[2], 0, 127);
  const std::uint8_t value3 = floor_to_u8(snapshot.rows[row].values[3], 0, 127);

  std::string preview =
      "Ch " + std::to_string(static_cast<unsigned>(channel + 1)) + ": ";
  switch (type) {
  case CommandType::Mod:
    append_preview_triplet(&preview, channel, 0x01, value0);
    break;
  case CommandType::Volume:
    append_preview_triplet(&preview, channel, 0x07, value0);
    break;
  case CommandType::Pan:
    append_preview_triplet(&preview, channel, 0x0A, value0);
    break;
  case CommandType::BendRange:
    append_preview_triplet(&preview, channel, 0x14, value0);
    break;
  case CommandType::LfoSpeed:
    append_preview_triplet(&preview, channel, 0x15, value0);
    break;
  case CommandType::ModType:
    append_preview_triplet(&preview, channel, 0x16, value0);
    break;
  case CommandType::Tune:
    append_preview_triplet(&preview, channel, 0x18, value0);
    break;
  case CommandType::LfoDelay:
    append_preview_triplet(&preview, channel, 0x1A, value0);
    break;
  case CommandType::Priority21:
    append_preview_triplet(&preview, channel, 0x21, value0);
    break;
  case CommandType::Priority27:
    append_preview_triplet(&preview, channel, 0x27, value0);
    break;
  case CommandType::XcmdIecv:
    append_preview_triplet(&preview, channel, 0x1E, 0x08);
    append_preview_triplet(&preview, channel, 0x1D, value0);
    break;
  case CommandType::XcmdIecl:
    append_preview_triplet(&preview, channel, 0x1E, 0x09);
    append_preview_triplet(&preview, channel, 0x1D, value0);
    break;
  case CommandType::MemAcc0C:
    append_preview_triplet(&preview, channel, 0x0D, value0);
    append_preview_triplet(&preview, channel, 0x0E, value1);
    append_preview_triplet(&preview, channel, 0x0F, value2);
    append_preview_triplet(&preview, channel, 0x0C, value3);
    break;
  case CommandType::MemAcc10:
    append_preview_triplet(&preview, channel, 0x0D, value0);
    append_preview_triplet(&preview, channel, 0x0E, value1);
    append_preview_triplet(&preview, channel, 0x0F, value2);
    append_preview_triplet(&preview, channel, 0x10, value3);
    break;
  case CommandType::None:
    break;
  }

  return preview;
}

void draw_parameter_controls(Plugin *plugin, std::size_t row, CommandType type,
                             const UiRowSnapshot &rowSnapshot) {
  const int activeFields = field_count_for_type(type);
  if (activeFields == 0) {
    ImGui::TextDisabled("-");
    return;
  }

  if (activeFields == 1) {
    int value = static_cast<int>(rowSnapshot.values[0]);
    const std::string label =
        std::string(field_label(type, 0)) + "##v" + std::to_string(row) + "_0";
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt(label.c_str(), &value, 0, 127, "%d",
                         ImGuiSliderFlags_AlwaysClamp)) {
      apply_ui_param_change(
          plugin,
          row_param_id(static_cast<std::uint32_t>(row), RowParamSlot::Value0),
          value);
    }
    return;
  }

  const float fullWidth = ImGui::GetContentRegionAvail().x;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float controlWidth =
      (fullWidth - (spacing * static_cast<float>(activeFields - 1))) /
      static_cast<float>(activeFields);

  for (int field = 0; field < activeFields; ++field) {
    if (field > 0)
      ImGui::SameLine();

    ImGui::PushItemWidth(controlWidth);
    int value = static_cast<int>(rowSnapshot.values[field]);
    const std::string label = std::string(field_label(type, field)) + "##v" +
                              std::to_string(row) + "_" + std::to_string(field);
    if (ImGui::SliderInt(label.c_str(), &value, 0, 127, "%d",
                         ImGuiSliderFlags_AlwaysClamp)) {
      apply_ui_param_change(
          plugin,
          row_param_id(static_cast<std::uint32_t>(row),
                       static_cast<RowParamSlot>(
                           static_cast<int>(RowParamSlot::Value0) + field)),
          value);
    }
    ImGui::PopItemWidth();
  }
}

void draw_frame(void *userData, std::uint32_t width, std::uint32_t height) {
  auto *plugin = static_cast<Plugin *>(userData);
  if (!plugin)
    return;

  reload_voicegroup_if_changed(plugin);

  UiSnapshot snapshot = {};
  fill_ui_snapshot(plugin, &snapshot);

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(
      ImVec2(static_cast<float>(width), static_cast<float>(height)));
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

  ImGui::Begin("##ccomidi", nullptr, flags);

  ImGui::TextUnformatted("ccomidi");
  ImGui::SameLine();
  ImGui::TextDisabled("MIDI CC sender");
  ImGui::Separator();

  int outputChannel = static_cast<int>(std::floor(snapshot.outputChannel)) + 1;
  if (ImGui::SliderInt("Output Channel", &outputChannel, 1, 16))
    apply_ui_param_change(plugin, kParamOutputChannel,
                          static_cast<double>(outputChannel - 1));

  bool programEnabled = std::floor(snapshot.programEnabled) >= 1.0;
  if (ImGui::Checkbox("Emit Program Change", &programEnabled))
    apply_ui_param_change(plugin, kParamProgramEnabled,
                          programEnabled ? 1.0 : 0.0);

  int program = static_cast<int>(std::floor(snapshot.program));
  program = std::clamp(program, 0, 127);
  ImGui::BeginDisabled(!programEnabled);
  if (!plugin->voiceLoad.slots.empty()) {
    const VoiceSlot *current = nullptr;
    for (const VoiceSlot &slot : plugin->voiceLoad.slots) {
      if (slot.program == program) {
        current = &slot;
        break;
      }
    }
    char preview[288];
    if (current)
      std::snprintf(preview, sizeof(preview), "%03d  %s", current->program,
                    current->name.c_str());
    else
      std::snprintf(preview, sizeof(preview), "%03d  (empty slot)", program);
    if (ImGui::BeginCombo("Program", preview)) {
      for (const VoiceSlot &slot : plugin->voiceLoad.slots) {
        char label[288];
        std::snprintf(label, sizeof(label), "%03d  %s", slot.program,
                      slot.name.c_str());
        const bool selected = slot.program == program;
        if (ImGui::Selectable(label, selected))
          apply_ui_param_change(plugin, kParamProgram,
                                static_cast<double>(slot.program));
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  } else {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Voicegroup: %s",
                       plugin->voiceLoad.error.c_str());
    if (!plugin->voiceLoad.statePath.empty())
      ImGui::TextDisabled("state: %s", plugin->voiceLoad.statePath.c_str());
  }
  ImGui::EndDisabled();

  ImGui::Spacing();

  if (ImGui::BeginTable("fixed_rows", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthFixed,
                            120.0f);
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch,
                            2.2f);
    ImGui::TableHeadersRow();

    for (std::size_t row = 0; row < kFixedCommandRowCount; ++row) {
      const CommandType type = fixed_command_type_for_row(row);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(command_type_name(type));

      ImGui::TableSetColumnIndex(1);
      bool enabled = std::floor(snapshot.rows[row].enabled) >= 1.0;
      if (ImGui::Checkbox(("##fixed_enabled" + std::to_string(row)).c_str(),
                          &enabled))
        apply_ui_param_change(plugin,
                              row_param_id(static_cast<std::uint32_t>(row),
                                           RowParamSlot::Enabled),
                              enabled ? 1.0 : 0.0);

      ImGui::TableSetColumnIndex(2);
      draw_parameter_controls(plugin, row, type, snapshot.rows[row]);

      ImGui::TableSetColumnIndex(3);
      ImGui::TextWrapped("%s", preview_for_row(snapshot, row).c_str());
    }

    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextUnformatted("Additional Commands");
  if (ImGui::BeginTable("rows", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch,
                            1.4f);
    ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthStretch,
                            2.2f);
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch,
                            2.8f);
    ImGui::TableHeadersRow();

    for (std::size_t row = kFixedCommandRowCount; row < kMaxCommandRows;
         ++row) {
      const CommandType type = static_cast<CommandType>(
          static_cast<int>(std::floor(snapshot.rows[row].type)));

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%02u",
                  static_cast<unsigned>(row - kFixedCommandRowCount + 1));

      ImGui::TableSetColumnIndex(1);
      bool enabled = std::floor(snapshot.rows[row].enabled) >= 1.0;
      if (ImGui::Checkbox(("##enabled" + std::to_string(row)).c_str(),
                          &enabled))
        apply_ui_param_change(plugin,
                              row_param_id(static_cast<std::uint32_t>(row),
                                           RowParamSlot::Enabled),
                              enabled ? 1.0 : 0.0);

      ImGui::TableSetColumnIndex(2);
      int typeIndex = static_cast<int>(type);
      if (ImGui::BeginCombo(("##type" + std::to_string(row)).c_str(),
                            command_type_name(type))) {
        for (int candidate = static_cast<int>(CommandType::None);
             candidate <= static_cast<int>(CommandType::MemAcc10);
             ++candidate) {
          const auto candidateType = static_cast<CommandType>(candidate);
          if (!is_table_command_type(candidateType))
            continue;
          const bool selected = candidate == typeIndex;
          if (ImGui::Selectable(command_type_name(candidateType), selected)) {
            apply_ui_param_change(plugin,
                                  row_param_id(static_cast<std::uint32_t>(row),
                                               RowParamSlot::Type),
                                  static_cast<double>(candidate));
          }
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableSetColumnIndex(3);
      draw_parameter_controls(plugin, row, type, snapshot.rows[row]);

      ImGui::TableSetColumnIndex(4);
      ImGui::TextWrapped("%s", preview_for_row(snapshot, row).c_str());
    }

    ImGui::EndTable();
  }

  ImGui::End();
}

} // namespace

struct EditorState {
  Plugin *plugin = nullptr;
  EditorShell *shell = nullptr;
};

EditorState *editor_create(Plugin *plugin) {
  auto *editor = new EditorState();
  editor->plugin = plugin;

  EditorShellConfig config = {};
  config.title = "ccomidi";
  config.className = "ccomidi";




  EditorShellCallbacks callbacks = {};
  callbacks.userData = plugin;
  callbacks.drawFrame = &draw_frame;

  editor->shell = editor_shell_create(plugin ? plugin->host : nullptr, config,
                                      callbacks);
  if (!editor->shell) {
    delete editor;
    return nullptr;
  }
  return editor;
}

void editor_prepare_destroy(EditorState *editor) {
  if (!editor)
    return;
  editor->plugin = nullptr;
  editor_shell_prepare_destroy(editor->shell);
}

void editor_destroy(EditorState *editor) {
  if (!editor)
    return;
  editor_shell_destroy(editor->shell);
  delete editor;
}

bool editor_show(EditorState *editor) {
  return editor && editor_shell_show(editor->shell);
}

bool editor_hide(EditorState *editor) {
  return editor && editor_shell_hide(editor->shell);
}

bool editor_set_parent(EditorState *editor, std::uintptr_t nativeParent) {
  return editor && editor_shell_set_parent(editor->shell, nativeParent);
}

bool editor_set_size(EditorState *editor, std::uint32_t width,
                     std::uint32_t height) {
  return editor && editor_shell_set_size(editor->shell, width, height);
}

void editor_get_size(EditorState *editor, std::uint32_t *width,
                     std::uint32_t *height) {
  if (!editor) {
    if (width)
      *width = kDefaultWidth;
    if (height)
      *height = kDefaultHeight;
    return;
  }
  editor_shell_get_size(editor->shell, width, height);
}

bool editor_can_resize(EditorState *editor) {
  return editor && editor_shell_can_resize(editor->shell);
}

bool editor_was_closed(EditorState *editor) {
  return editor && editor_shell_was_closed(editor->shell);
}

void editor_start_internal_timer(EditorState *editor) {
  if (editor)
    editor_shell_start_timer(editor->shell);
}

void editor_stop_internal_timer(EditorState *editor) {
  if (editor)
    editor_shell_stop_timer(editor->shell);
}

} // namespace ccomidi
