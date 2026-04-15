#include "gui/ccomidi_editor.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef __APPLE__
#include "gui/metal/pugl_mac_metal.h"
#include "imgui_impl_metal.h"
#else
#include "backends/imgui_impl_opengl3.h"
#include <pugl/gl.h>
#endif
#include <pugl/pugl.h>

#include "imgui.h"

#include "gui/imgui_impl_pugl.h"
#include "plugin/param_ids.h"

namespace ccomidi {

constexpr std::uintptr_t kRenderTimerId = 1;
constexpr std::uint32_t kDefaultWidth = 980;
constexpr std::uint32_t kDefaultHeight = 620;

struct EditorState {
  Plugin *plugin = nullptr;
  const clap_host_t *host = nullptr;
  PuglWorld *world = nullptr;
  PuglView *view = nullptr;
  ImGuiContext *imgui = nullptr;
  bool realized = false;
  bool renderInited = false;
  bool embedded = false;
  bool wasClosed = false;
  bool destroying = false;
  std::uint32_t width = kDefaultWidth;
  std::uint32_t height = kDefaultHeight;
};

namespace {

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

void draw_parameter_controls(EditorState *editor, std::size_t row,
                             CommandType type,
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
          editor->plugin,
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
          editor->plugin,
          row_param_id(static_cast<std::uint32_t>(row),
                       static_cast<RowParamSlot>(
                           static_cast<int>(RowParamSlot::Value0) + field)),
          value);
    }
    ImGui::PopItemWidth();
  }
}

bool is_plain_space_key(const PuglEvent *event) {
  if (!event || event->type != PUGL_KEY_PRESS)
    return false;
  const PuglMods mods = event->key.state;
  return event->key.key == PUGL_KEY_SPACE &&
         (mods & (PUGL_MOD_SHIFT | PUGL_MOD_CTRL | PUGL_MOD_ALT |
                  PUGL_MOD_SUPER)) == 0;
}

void draw_editor(EditorState *editor) {
  if (!editor || !editor->plugin)
    return;

  UiSnapshot snapshot = {};
  fill_ui_snapshot(editor->plugin, &snapshot);

#ifdef __APPLE__
  auto *metalContext =
      static_cast<PuglMetalContext *>(puglGetContext(editor->view));
  if (!metalContext || !metalContext->renderPassDescriptor ||
      !metalContext->commandBuffer || !metalContext->renderEncoder) {
    return;
  }
  ImGui_ImplMetal_NewFrame(metalContext->renderPassDescriptor);
#else
  ImGui_ImplOpenGL3_NewFrame();
#endif
  ImGui_ImplPugl_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(static_cast<float>(editor->width),
                                  static_cast<float>(editor->height)));
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
    apply_ui_param_change(editor->plugin, kParamOutputChannel,
                          static_cast<double>(outputChannel - 1));

  ImGui::Spacing();
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

    for (std::size_t row = 0; row < kMaxCommandRows; ++row) {
      const CommandType type = static_cast<CommandType>(
          static_cast<int>(std::floor(snapshot.rows[row].type)));

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%02u", static_cast<unsigned>(row + 1));

      ImGui::TableSetColumnIndex(1);
      bool enabled = std::floor(snapshot.rows[row].enabled) >= 1.0;
      if (ImGui::Checkbox(("##enabled" + std::to_string(row)).c_str(),
                          &enabled))
        apply_ui_param_change(editor->plugin,
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
          const bool selected = candidate == typeIndex;
          if (ImGui::Selectable(
                  command_type_name(static_cast<CommandType>(candidate)),
                  selected)) {
            apply_ui_param_change(editor->plugin,
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
      draw_parameter_controls(editor, row, type, snapshot.rows[row]);

      ImGui::TableSetColumnIndex(4);
      ImGui::TextWrapped("%s", preview_for_row(snapshot, row).c_str());
    }

    ImGui::EndTable();
  }

  ImGui::End();

  ImGui::Render();
#ifdef __APPLE__
  ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(),
                                 metalContext->commandBuffer,
                                 metalContext->renderEncoder);
#else
  glViewport(0, 0, static_cast<int>(editor->width),
             static_cast<int>(editor->height));
  glClearColor(0.10f, 0.11f, 0.12f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}

PuglStatus on_pugl_event(PuglView *view, const PuglEvent *event) {
  auto *editor = static_cast<EditorState *>(puglGetHandle(view));
  if (!editor)
    return PUGL_SUCCESS;

  ImGui::SetCurrentContext(editor->imgui);

  switch (event->type) {
  case PUGL_REALIZE:
    if (!editor->renderInited) {
#ifdef __APPLE__
      auto *metalContext =
          static_cast<PuglMetalContext *>(puglGetContext(editor->view));
      if (!metalContext || !metalContext->device)
        break;
      ImGui_ImplMetal_Init(metalContext->device);
#else
      ImGui_ImplOpenGL3_Init("#version 150");
#endif
      editor->renderInited = true;
    }
    break;
  case PUGL_UNREALIZE:
    if (editor->renderInited) {
#ifdef __APPLE__
      ImGui_ImplMetal_Shutdown();
#else
      ImGui_ImplOpenGL3_Shutdown();
#endif
      editor->renderInited = false;
    }
    break;
  case PUGL_CONFIGURE:
    editor->width = event->configure.width;
    editor->height = event->configure.height;
    break;
  case PUGL_UPDATE:
    puglObscureView(view);
    break;
  case PUGL_EXPOSE:
    if (editor->renderInited)
      draw_editor(editor);
    break;
  case PUGL_TIMER:
    if (event->timer.id == kRenderTimerId)
      editor_tick(editor);
    break;
  case PUGL_CLOSE:
    editor->wasClosed = true;
    editor_stop_internal_timer(editor);
    if (!editor->destroying && editor->host) {
      const auto *hostGui = static_cast<const clap_host_gui_t *>(
          editor->host->get_extension(editor->host, CLAP_EXT_GUI));
      if (hostGui && hostGui->closed)
        hostGui->closed(editor->host, false);
    }
    break;
  case PUGL_BUTTON_PRESS:
    puglGrabFocus(view);
    ImGui_ImplPugl_ProcessEvent(event);
    break;
  case PUGL_KEY_PRESS:
    if (editor->embedded && is_plain_space_key(event) &&
        !ImGui::GetIO().WantTextInput)
      return PUGL_UNSUPPORTED;
    ImGui_ImplPugl_ProcessEvent(event);
    break;
  default:
    ImGui_ImplPugl_ProcessEvent(event);
    break;
  }

  return PUGL_SUCCESS;
}

} // namespace

EditorState *editor_create(Plugin *plugin) {
  auto *editor = new EditorState();
  editor->plugin = plugin;
  editor->host = plugin ? plugin->host : nullptr;
  editor->world = puglNewWorld(PUGL_MODULE, 0);
  if (!editor->world) {
    delete editor;
    return nullptr;
  }

  puglSetWorldString(editor->world, PUGL_CLASS_NAME, "ccomidi");
  editor->view = puglNewView(editor->world);
  if (!editor->view) {
    puglFreeWorld(editor->world);
    delete editor;
    return nullptr;
  }

#ifdef __APPLE__
  puglSetBackend(editor->view, puglMetalBackend());
#else
  puglSetBackend(editor->view, puglGlBackend());
  puglSetViewHint(editor->view, PUGL_CONTEXT_API, PUGL_OPENGL_API);
  puglSetViewHint(editor->view, PUGL_CONTEXT_VERSION_MAJOR, 3);
  puglSetViewHint(editor->view, PUGL_CONTEXT_VERSION_MINOR, 3);
  puglSetViewHint(editor->view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE);
  puglSetViewHint(editor->view, PUGL_DOUBLE_BUFFER, 1);
#endif
  puglSetViewHint(editor->view, PUGL_RESIZABLE, 1);
  puglSetSizeHint(editor->view, PUGL_DEFAULT_SIZE, kDefaultWidth,
                  kDefaultHeight);
  puglSetSizeHint(editor->view, PUGL_MIN_SIZE, 720, 420);
  puglSetViewString(editor->view, PUGL_WINDOW_TITLE, "ccomidi");
  puglSetHandle(editor->view, editor);
  puglSetEventFunc(editor->view, on_pugl_event);

  editor->imgui = ImGui::CreateContext();
  ImGui::SetCurrentContext(editor->imgui);
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.FontGlobalScale = 1.0f;
  ImGui::StyleColorsDark();
  ImGui_ImplPugl_Init(editor->view);

  return editor;
}

void editor_prepare_destroy(EditorState *editor) {
  if (!editor)
    return;

  editor->destroying = true;
  editor->plugin = nullptr;
  editor_stop_internal_timer(editor);
}

void editor_destroy(EditorState *editor) {
  if (!editor)
    return;

  editor_prepare_destroy(editor);
  ImGui::SetCurrentContext(editor->imgui);
  if (editor->view && editor->renderInited) {
    puglEnterContext(editor->view);
#ifdef __APPLE__
    ImGui_ImplMetal_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
#endif
    editor->renderInited = false;
    puglLeaveContext(editor->view);
  }
  ImGui_ImplPugl_Shutdown();
  if (editor->view)
    puglFreeView(editor->view);
  if (editor->imgui)
    ImGui::DestroyContext(editor->imgui);
  if (editor->world)
    puglFreeWorld(editor->world);
  delete editor;
}

bool editor_set_parent(EditorState *editor, std::uintptr_t nativeParent) {
  if (!editor || !editor->view || editor->realized)
    return false;

  puglSetParent(editor->view, reinterpret_cast<PuglNativeView>(nativeParent));
  const PuglStatus status = puglRealize(editor->view);
  if (status != PUGL_SUCCESS)
    return false;
  editor->embedded = true;
  editor->realized = true;
  return true;
}

bool editor_show(EditorState *editor) {
  if (!editor || !editor->view)
    return false;

  if (!editor->realized) {
    const PuglStatus status = puglRealize(editor->view);
    if (status != PUGL_SUCCESS)
      return false;
    editor->realized = true;
  }

  puglShow(editor->view,
           editor->embedded ? PUGL_SHOW_PASSIVE : PUGL_SHOW_RAISE);
  return true;
}

bool editor_hide(EditorState *editor) {
  if (!editor || !editor->view)
    return false;
  editor_stop_internal_timer(editor);
  puglHide(editor->view);
  return true;
}

bool editor_set_size(EditorState *editor, std::uint32_t width,
                     std::uint32_t height) {
  if (!editor || !editor->view)
    return false;

  double scale = puglGetScaleFactor(editor->view);
  if (scale < 1.0)
    scale = 1.0;
  puglSetSizeHint(editor->view, PUGL_CURRENT_SIZE,
                  static_cast<PuglSpan>(width * scale),
                  static_cast<PuglSpan>(height * scale));
  return true;
}

void editor_get_size(EditorState *editor, std::uint32_t *width,
                     std::uint32_t *height) {
  if (!width || !height)
    return;
  if (!editor) {
    *width = kDefaultWidth;
    *height = kDefaultHeight;
    return;
  }

  double scale = editor->view ? puglGetScaleFactor(editor->view) : 1.0;
  if (scale < 1.0)
    scale = 1.0;
  *width = static_cast<std::uint32_t>(editor->width / scale);
  *height = static_cast<std::uint32_t>(editor->height / scale);
}

bool editor_can_resize(EditorState *editor) {
  return editor && editor->embedded;
}

bool editor_was_closed(EditorState *editor) {
  return editor && editor->wasClosed;
}

void editor_tick(EditorState *editor) {
  if (!editor || editor->destroying || !editor->world)
    return;

  if (editor->view && editor->realized)
    puglObscureView(editor->view);

  puglUpdate(editor->world, 0.0);
}

void editor_start_internal_timer(EditorState *editor) {
  if (!editor || !editor->view || !editor->realized)
    return;
  puglStartTimer(editor->view, kRenderTimerId, 1.0 / 60.0);
}

void editor_stop_internal_timer(EditorState *editor) {
  if (!editor || !editor->view || !editor->realized)
    return;
  puglStopTimer(editor->view, kRenderTimerId);
}

} // namespace ccomidi
