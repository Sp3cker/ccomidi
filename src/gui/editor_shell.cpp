#include "gui/editor_shell.h"

#include <cstdint>

#ifdef __APPLE__
#include "gui/metal/pugl_mac_metal.h"
#include "imgui_impl_metal.h"
#else
#include "backends/imgui_impl_opengl3.h"
#include <pugl/gl.h>
#endif
#include <pugl/pugl.h>

#include <clap/ext/gui.h>

#include "imgui.h"

#include "gui/imgui_impl_pugl.h"

namespace ccomidi {

namespace {

constexpr std::uintptr_t kRenderTimerId = 1;

bool is_plain_space_key(const PuglEvent *event) {
  if (!event || event->type != PUGL_KEY_PRESS)
    return false;
  const PuglMods mods = event->key.state;
  return event->key.key == PUGL_KEY_SPACE &&
         (mods & (PUGL_MOD_SHIFT | PUGL_MOD_CTRL | PUGL_MOD_ALT |
                  PUGL_MOD_SUPER)) == 0;
}

} // namespace

struct EditorShell {
  const clap_host_t *host = nullptr;
  EditorShellCallbacks callbacks = {};
  PuglWorld *world = nullptr;
  PuglView *view = nullptr;
  ImGuiContext *imgui = nullptr;
  bool realized = false;
  bool renderInited = false;
  bool embedded = false;
  bool wasClosed = false;
  bool destroying = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t defaultWidth = 0;
  std::uint32_t defaultHeight = 0;
};

namespace {

void render_frame(EditorShell *shell) {
  if (!shell)
    return;

#ifdef __APPLE__
  auto *metalContext =
      static_cast<PuglMetalContext *>(puglGetContext(shell->view));
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

  if (shell->callbacks.drawFrame)
    shell->callbacks.drawFrame(shell->callbacks.userData, shell->width,
                               shell->height);

  ImGui::Render();
#ifdef __APPLE__
  ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(),
                                 metalContext->commandBuffer,
                                 metalContext->renderEncoder);
#else
  glViewport(0, 0, static_cast<int>(shell->width),
             static_cast<int>(shell->height));
  glClearColor(0.10f, 0.11f, 0.12f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}

void tick(EditorShell *shell) {
  if (!shell || shell->destroying || !shell->world)
    return;
  if (shell->view && shell->realized)
    puglObscureView(shell->view);
  puglUpdate(shell->world, 0.0);
}

PuglStatus on_pugl_event(PuglView *view, const PuglEvent *event) {
  auto *shell = static_cast<EditorShell *>(puglGetHandle(view));
  if (!shell)
    return PUGL_SUCCESS;

  ImGui::SetCurrentContext(shell->imgui);

  switch (event->type) {
  case PUGL_REALIZE:
    if (!shell->renderInited) {
#ifdef __APPLE__
      auto *metalContext =
          static_cast<PuglMetalContext *>(puglGetContext(shell->view));
      if (!metalContext || !metalContext->device)
        break;
      ImGui_ImplMetal_Init(metalContext->device);
#else
      ImGui_ImplOpenGL3_Init("#version 150");
#endif
      shell->renderInited = true;
    }
    break;
  case PUGL_UNREALIZE:
    if (shell->renderInited) {
#ifdef __APPLE__
      ImGui_ImplMetal_Shutdown();
#else
      ImGui_ImplOpenGL3_Shutdown();
#endif
      shell->renderInited = false;
    }
    break;
  case PUGL_CONFIGURE:
    shell->width = event->configure.width;
    shell->height = event->configure.height;
    break;
  case PUGL_UPDATE:
    puglObscureView(view);
    break;
  case PUGL_EXPOSE:
    if (shell->renderInited)
      render_frame(shell);
    break;
  case PUGL_TIMER:
    if (event->timer.id == kRenderTimerId)
      tick(shell);
    break;
  case PUGL_CLOSE:
    shell->wasClosed = true;
    editor_shell_stop_timer(shell);
    if (!shell->destroying && shell->host) {
      const auto *hostGui = static_cast<const clap_host_gui_t *>(
          shell->host->get_extension(shell->host, CLAP_EXT_GUI));
      if (hostGui && hostGui->closed)
        hostGui->closed(shell->host, false);
    }
    break;
  case PUGL_BUTTON_PRESS:
    puglGrabFocus(view);
    ImGui_ImplPugl_ProcessEvent(event);
    break;
  case PUGL_KEY_PRESS:
    if (shell->embedded && is_plain_space_key(event) &&
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

EditorShell *editor_shell_create(const clap_host_t *host,
                                 const EditorShellConfig &config,
                                 const EditorShellCallbacks &callbacks) {
  auto *shell = new EditorShell();
  shell->host = host;
  shell->callbacks = callbacks;
  shell->defaultWidth = config.defaultWidth;
  shell->defaultHeight = config.defaultHeight;
  shell->width = config.defaultWidth;
  shell->height = config.defaultHeight;

  shell->world = puglNewWorld(PUGL_MODULE, 0);
  if (!shell->world) {
    delete shell;
    return nullptr;
  }

  puglSetWorldString(shell->world, PUGL_CLASS_NAME, config.className);
  shell->view = puglNewView(shell->world);
  if (!shell->view) {
    puglFreeWorld(shell->world);
    delete shell;
    return nullptr;
  }

#ifdef __APPLE__
  puglSetBackend(shell->view, puglMetalBackend());
#else
  puglSetBackend(shell->view, puglGlBackend());
  puglSetViewHint(shell->view, PUGL_CONTEXT_API, PUGL_OPENGL_API);
  puglSetViewHint(shell->view, PUGL_CONTEXT_VERSION_MAJOR, 3);
  puglSetViewHint(shell->view, PUGL_CONTEXT_VERSION_MINOR, 3);
  puglSetViewHint(shell->view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE);
  puglSetViewHint(shell->view, PUGL_DOUBLE_BUFFER, 1);
#endif
  puglSetViewHint(shell->view, PUGL_RESIZABLE, 1);
  // Drop OS-level key repeats. ImGui derives its own repeat cadence from how
  // long a key is held (DownDuration), so backspace/arrow-hold still work.
  // Without this, macOS key-repeat on Cmd+V fires the paste shortcut multiple
  // times per tap.
  puglSetViewHint(shell->view, PUGL_IGNORE_KEY_REPEAT, 1);
  puglSetSizeHint(shell->view, PUGL_DEFAULT_SIZE, config.defaultWidth,
                  config.defaultHeight);
  puglSetSizeHint(shell->view, PUGL_MIN_SIZE, config.minWidth, config.minHeight);
  puglSetViewString(shell->view, PUGL_WINDOW_TITLE, config.title);
  puglSetHandle(shell->view, shell);
  puglSetEventFunc(shell->view, on_pugl_event);

  shell->imgui = ImGui::CreateContext();
  ImGui::SetCurrentContext(shell->imgui);
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(config.uiScale);
  io.FontGlobalScale = config.uiScale;
  ImGui_ImplPugl_Init(shell->view);

  return shell;
}

void editor_shell_prepare_destroy(EditorShell *shell) {
  if (!shell)
    return;
  shell->destroying = true;
  editor_shell_stop_timer(shell);
}

void editor_shell_destroy(EditorShell *shell) {
  if (!shell)
    return;

  editor_shell_prepare_destroy(shell);
  ImGui::SetCurrentContext(shell->imgui);
  if (shell->view && shell->renderInited) {
    puglEnterContext(shell->view);
#ifdef __APPLE__
    ImGui_ImplMetal_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
#endif
    shell->renderInited = false;
    puglLeaveContext(shell->view);
  }
  ImGui_ImplPugl_Shutdown();
  if (shell->view)
    puglFreeView(shell->view);
  if (shell->imgui)
    ImGui::DestroyContext(shell->imgui);
  if (shell->world)
    puglFreeWorld(shell->world);
  delete shell;
}

bool editor_shell_set_parent(EditorShell *shell, std::uintptr_t nativeParent) {
  if (!shell || !shell->view || shell->realized)
    return false;

  puglSetParent(shell->view, reinterpret_cast<PuglNativeView>(nativeParent));
  const PuglStatus status = puglRealize(shell->view);
  if (status != PUGL_SUCCESS)
    return false;
  shell->embedded = true;
  shell->realized = true;
  return true;
}

bool editor_shell_show(EditorShell *shell) {
  if (!shell || !shell->view)
    return false;

  if (!shell->realized) {
    const PuglStatus status = puglRealize(shell->view);
    if (status != PUGL_SUCCESS)
      return false;
    shell->realized = true;
  }

  puglShow(shell->view,
           shell->embedded ? PUGL_SHOW_PASSIVE : PUGL_SHOW_RAISE);
  return true;
}

bool editor_shell_hide(EditorShell *shell) {
  if (!shell || !shell->view)
    return false;
  editor_shell_stop_timer(shell);
  puglHide(shell->view);
  return true;
}

bool editor_shell_set_size(EditorShell *shell, std::uint32_t width,
                           std::uint32_t height) {
  if (!shell || !shell->view)
    return false;

  double scale = puglGetScaleFactor(shell->view);
  if (scale < 1.0)
    scale = 1.0;
  puglSetSizeHint(shell->view, PUGL_CURRENT_SIZE,
                  static_cast<PuglSpan>(width * scale),
                  static_cast<PuglSpan>(height * scale));
  return true;
}

void editor_shell_get_size(const EditorShell *shell, std::uint32_t *width,
                           std::uint32_t *height) {
  if (!width || !height)
    return;
  if (!shell) {
    *width = 0;
    *height = 0;
    return;
  }

  double scale = shell->view ? puglGetScaleFactor(shell->view) : 1.0;
  if (scale < 1.0)
    scale = 1.0;
  *width = static_cast<std::uint32_t>(shell->width / scale);
  *height = static_cast<std::uint32_t>(shell->height / scale);
}

bool editor_shell_can_resize(const EditorShell *shell) {
  return shell && shell->embedded;
}

bool editor_shell_was_closed(const EditorShell *shell) {
  return shell && shell->wasClosed;
}

void editor_shell_start_timer(EditorShell *shell) {
  if (!shell || !shell->view || !shell->realized)
    return;
  puglStartTimer(shell->view, kRenderTimerId, 1.0 / 60.0);
}

void editor_shell_stop_timer(EditorShell *shell) {
  if (!shell || !shell->view || !shell->realized)
    return;
  puglStopTimer(shell->view, kRenderTimerId);
}

} // namespace ccomidi
