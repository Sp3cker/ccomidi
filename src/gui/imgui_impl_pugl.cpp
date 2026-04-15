#include "gui/imgui_impl_pugl.h"

#include <cfloat>
#include <cstring>
#include <string>

#include <pugl/gl.h>

struct ImGui_ImplPugl_Data {
  PuglView *view = nullptr;
  double time = 0.0;
  std::string clipboardText;
};

static ImGui_ImplPugl_Data *ImGui_ImplPugl_GetBackendData() {
  return ImGui::GetCurrentContext()
             ? static_cast<ImGui_ImplPugl_Data *>(
                   ImGui::GetIO().BackendPlatformUserData)
             : nullptr;
}

static const char *ImGui_ImplPugl_GetClipboardText(void *) {
  ImGui_ImplPugl_Data *bd = ImGui_ImplPugl_GetBackendData();
  if (!bd)
    return "";
  puglPaste(bd->view);
  return bd->clipboardText.c_str();
}

static void ImGui_ImplPugl_SetClipboardText(void *, const char *text) {
  ImGui_ImplPugl_Data *bd = ImGui_ImplPugl_GetBackendData();
  if (!bd)
    return;
  puglSetClipboard(bd->view, "text/plain", text, std::strlen(text) + 1);
}

static ImGuiKey PuglKeyToImGuiKey(std::uint32_t key) {
  switch (key) {
  case PUGL_KEY_TAB:
    return ImGuiKey_Tab;
  case PUGL_KEY_LEFT:
    return ImGuiKey_LeftArrow;
  case PUGL_KEY_RIGHT:
    return ImGuiKey_RightArrow;
  case PUGL_KEY_UP:
    return ImGuiKey_UpArrow;
  case PUGL_KEY_DOWN:
    return ImGuiKey_DownArrow;
  case PUGL_KEY_PAGE_UP:
    return ImGuiKey_PageUp;
  case PUGL_KEY_PAGE_DOWN:
    return ImGuiKey_PageDown;
  case PUGL_KEY_HOME:
    return ImGuiKey_Home;
  case PUGL_KEY_END:
    return ImGuiKey_End;
  case PUGL_KEY_INSERT:
    return ImGuiKey_Insert;
  case PUGL_KEY_DELETE:
    return ImGuiKey_Delete;
  case PUGL_KEY_BACKSPACE:
    return ImGuiKey_Backspace;
  case PUGL_KEY_SPACE:
    return ImGuiKey_Space;
  case PUGL_KEY_ENTER:
    return ImGuiKey_Enter;
  case PUGL_KEY_ESCAPE:
    return ImGuiKey_Escape;
  case PUGL_KEY_PAD_ENTER:
    return ImGuiKey_KeypadEnter;
  case PUGL_KEY_F1:
    return ImGuiKey_F1;
  case PUGL_KEY_F2:
    return ImGuiKey_F2;
  case PUGL_KEY_F3:
    return ImGuiKey_F3;
  case PUGL_KEY_F4:
    return ImGuiKey_F4;
  case PUGL_KEY_F5:
    return ImGuiKey_F5;
  case PUGL_KEY_F6:
    return ImGuiKey_F6;
  case PUGL_KEY_F7:
    return ImGuiKey_F7;
  case PUGL_KEY_F8:
    return ImGuiKey_F8;
  case PUGL_KEY_F9:
    return ImGuiKey_F9;
  case PUGL_KEY_F10:
    return ImGuiKey_F10;
  case PUGL_KEY_F11:
    return ImGuiKey_F11;
  case PUGL_KEY_F12:
    return ImGuiKey_F12;
  case PUGL_KEY_SHIFT_L:
    return ImGuiKey_LeftShift;
  case PUGL_KEY_SHIFT_R:
    return ImGuiKey_RightShift;
  case PUGL_KEY_CTRL_L:
    return ImGuiKey_LeftCtrl;
  case PUGL_KEY_CTRL_R:
    return ImGuiKey_RightCtrl;
  case PUGL_KEY_ALT_L:
    return ImGuiKey_LeftAlt;
  case PUGL_KEY_ALT_R:
    return ImGuiKey_RightAlt;
  case PUGL_KEY_SUPER_L:
    return ImGuiKey_LeftSuper;
  case PUGL_KEY_SUPER_R:
    return ImGuiKey_RightSuper;
  case PUGL_KEY_MENU:
    return ImGuiKey_Menu;
  case PUGL_KEY_CAPS_LOCK:
    return ImGuiKey_CapsLock;
  case PUGL_KEY_SCROLL_LOCK:
    return ImGuiKey_ScrollLock;
  case PUGL_KEY_NUM_LOCK:
    return ImGuiKey_NumLock;
  case PUGL_KEY_PRINT_SCREEN:
    return ImGuiKey_PrintScreen;
  case PUGL_KEY_PAUSE:
    return ImGuiKey_Pause;
  case PUGL_KEY_PAD_0:
    return ImGuiKey_Keypad0;
  case PUGL_KEY_PAD_1:
    return ImGuiKey_Keypad1;
  case PUGL_KEY_PAD_2:
    return ImGuiKey_Keypad2;
  case PUGL_KEY_PAD_3:
    return ImGuiKey_Keypad3;
  case PUGL_KEY_PAD_4:
    return ImGuiKey_Keypad4;
  case PUGL_KEY_PAD_5:
    return ImGuiKey_Keypad5;
  case PUGL_KEY_PAD_6:
    return ImGuiKey_Keypad6;
  case PUGL_KEY_PAD_7:
    return ImGuiKey_Keypad7;
  case PUGL_KEY_PAD_8:
    return ImGuiKey_Keypad8;
  case PUGL_KEY_PAD_9:
    return ImGuiKey_Keypad9;
  case PUGL_KEY_PAD_DECIMAL:
    return ImGuiKey_KeypadDecimal;
  case PUGL_KEY_PAD_DIVIDE:
    return ImGuiKey_KeypadDivide;
  case PUGL_KEY_PAD_MULTIPLY:
    return ImGuiKey_KeypadMultiply;
  case PUGL_KEY_PAD_SUBTRACT:
    return ImGuiKey_KeypadSubtract;
  case PUGL_KEY_PAD_ADD:
    return ImGuiKey_KeypadAdd;
  case PUGL_KEY_PAD_EQUAL:
    return ImGuiKey_KeypadEqual;
  default:
    break;
  }

  if (key >= 'a' && key <= 'z')
    return static_cast<ImGuiKey>(ImGuiKey_A + (key - 'a'));
  if (key >= 'A' && key <= 'Z')
    return static_cast<ImGuiKey>(ImGuiKey_A + (key - 'A'));
  if (key >= '0' && key <= '9')
    return static_cast<ImGuiKey>(ImGuiKey_0 + (key - '0'));

  switch (key) {
  case '\'':
    return ImGuiKey_Apostrophe;
  case ',':
    return ImGuiKey_Comma;
  case '-':
    return ImGuiKey_Minus;
  case '.':
    return ImGuiKey_Period;
  case '/':
    return ImGuiKey_Slash;
  case ';':
    return ImGuiKey_Semicolon;
  case '=':
    return ImGuiKey_Equal;
  case '[':
    return ImGuiKey_LeftBracket;
  case '\\':
    return ImGuiKey_Backslash;
  case ']':
    return ImGuiKey_RightBracket;
  case '`':
    return ImGuiKey_GraveAccent;
  default:
    return ImGuiKey_None;
  }
}

static void UpdateModifiers(ImGuiIO &io, PuglMods mods) {
  io.AddKeyEvent(ImGuiMod_Ctrl, (mods & PUGL_MOD_CTRL) != 0);
  io.AddKeyEvent(ImGuiMod_Shift, (mods & PUGL_MOD_SHIFT) != 0);
  io.AddKeyEvent(ImGuiMod_Alt, (mods & PUGL_MOD_ALT) != 0);
  io.AddKeyEvent(ImGuiMod_Super, (mods & PUGL_MOD_SUPER) != 0);
}

bool ImGui_ImplPugl_Init(PuglView *view) {
  ImGuiIO &io = ImGui::GetIO();
  IM_ASSERT(io.BackendPlatformUserData == nullptr);

  auto *bd = IM_NEW(ImGui_ImplPugl_Data)();
  bd->view = view;
  io.BackendPlatformUserData = bd;
  io.BackendPlatformName = "imgui_impl_pugl";
  io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
  io.SetClipboardTextFn = ImGui_ImplPugl_SetClipboardText;
  io.GetClipboardTextFn = ImGui_ImplPugl_GetClipboardText;
  return true;
}

void ImGui_ImplPugl_Shutdown() {
  ImGui_ImplPugl_Data *bd = ImGui_ImplPugl_GetBackendData();
  IM_ASSERT(bd != nullptr);

  ImGuiIO &io = ImGui::GetIO();
  io.BackendPlatformUserData = nullptr;
  io.BackendPlatformName = nullptr;
  IM_DELETE(bd);
}

void ImGui_ImplPugl_NewFrame() {
  ImGui_ImplPugl_Data *bd = ImGui_ImplPugl_GetBackendData();
  IM_ASSERT(bd != nullptr);

  ImGuiIO &io = ImGui::GetIO();
  const PuglArea size = puglGetSizeHint(bd->view, PUGL_CURRENT_SIZE);
  io.DisplaySize =
      ImVec2(static_cast<float>(size.width), static_cast<float>(size.height));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

  double currentTime = puglGetTime(puglGetWorld(bd->view));
  if (currentTime <= bd->time)
    currentTime = bd->time + 0.00001;
  io.DeltaTime = bd->time > 0.0 ? static_cast<float>(currentTime - bd->time)
                                : (1.0f / 60.0f);
  bd->time = currentTime;

  if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
    return;

  ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
  PuglCursor puglCursor = PUGL_CURSOR_ARROW;
  switch (cursor) {
  case ImGuiMouseCursor_TextInput:
    puglCursor = PUGL_CURSOR_CARET;
    break;
  case ImGuiMouseCursor_ResizeAll:
    puglCursor = PUGL_CURSOR_ALL_SCROLL;
    break;
  case ImGuiMouseCursor_ResizeNS:
    puglCursor = PUGL_CURSOR_UP_DOWN;
    break;
  case ImGuiMouseCursor_ResizeEW:
    puglCursor = PUGL_CURSOR_LEFT_RIGHT;
    break;
  case ImGuiMouseCursor_ResizeNESW:
    puglCursor = PUGL_CURSOR_UP_RIGHT_DOWN_LEFT;
    break;
  case ImGuiMouseCursor_ResizeNWSE:
    puglCursor = PUGL_CURSOR_UP_LEFT_DOWN_RIGHT;
    break;
  case ImGuiMouseCursor_Hand:
    puglCursor = PUGL_CURSOR_HAND;
    break;
  case ImGuiMouseCursor_NotAllowed:
    puglCursor = PUGL_CURSOR_NO;
    break;
  default:
    puglCursor = PUGL_CURSOR_ARROW;
    break;
  }
  puglSetCursor(bd->view, puglCursor);
}

void ImGui_ImplPugl_ProcessEvent(const PuglEvent *event) {
  ImGui_ImplPugl_Data *bd = ImGui_ImplPugl_GetBackendData();
  if (!bd)
    return;

  ImGuiIO &io = ImGui::GetIO();
  switch (event->type) {
  case PUGL_FOCUS_IN:
    io.AddFocusEvent(true);
    break;
  case PUGL_FOCUS_OUT:
    io.AddFocusEvent(false);
    break;
  case PUGL_KEY_PRESS:
  case PUGL_KEY_RELEASE: {
    const bool pressed = event->type == PUGL_KEY_PRESS;
    UpdateModifiers(io, event->key.state);
    const ImGuiKey key = PuglKeyToImGuiKey(event->key.key);
    if (key != ImGuiKey_None)
      io.AddKeyEvent(key, pressed);
    break;
  }
  case PUGL_TEXT:
    io.AddInputCharacter(event->text.character);
    break;
  case PUGL_BUTTON_PRESS:
  case PUGL_BUTTON_RELEASE: {
    const bool pressed = event->type == PUGL_BUTTON_PRESS;
    UpdateModifiers(io, event->button.state);
    if (event->button.button < 5)
      io.AddMouseButtonEvent(static_cast<int>(event->button.button), pressed);
    break;
  }
  case PUGL_MOTION:
    UpdateModifiers(io, event->motion.state);
    io.AddMousePosEvent(static_cast<float>(event->motion.x),
                        static_cast<float>(event->motion.y));
    break;
  case PUGL_SCROLL:
    UpdateModifiers(io, event->scroll.state);
    io.AddMouseWheelEvent(static_cast<float>(event->scroll.dx),
                          static_cast<float>(event->scroll.dy));
    break;
  case PUGL_POINTER_OUT:
    if (event->crossing.mode == PUGL_CROSSING_NORMAL)
      io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    break;
  case PUGL_DATA_OFFER: {
    const std::uint32_t count = puglGetNumClipboardTypes(bd->view);
    for (std::uint32_t i = 0; i < count; ++i) {
      const char *type = puglGetClipboardType(bd->view, i);
      if (type && std::strcmp(type, "text/plain") == 0) {
        puglAcceptOffer(bd->view, &event->offer, i);
        break;
      }
    }
    break;
  }
  case PUGL_DATA: {
    std::size_t len = 0;
    const void *data = puglGetClipboard(bd->view, event->data.typeIndex, &len);
    if (data && len > 0) {
      bd->clipboardText.assign(static_cast<const char *>(data), len);
      if (!bd->clipboardText.empty() && bd->clipboardText.back() == '\0')
        bd->clipboardText.pop_back();
    }
    break;
  }
  default:
    break;
  }
}
