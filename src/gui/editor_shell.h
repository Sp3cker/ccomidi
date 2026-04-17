#ifndef CCOMIDI_EDITOR_SHELL_H
#define CCOMIDI_EDITOR_SHELL_H

#include <cstdint>

#include <clap/host.h>

namespace ccomidi {

struct EditorShell;

struct EditorShellCallbacks {
  void *userData = nullptr;
  void (*drawFrame)(void *userData, std::uint32_t width,
                    std::uint32_t height) = nullptr;
};

struct EditorShellConfig {
  const char *title = "ccomidi";
  const char *className = "ccomidi";
  std::uint32_t defaultWidth = 980;
  std::uint32_t defaultHeight = 620;
  std::uint32_t minWidth = 720;
  std::uint32_t minHeight = 420;
  float uiScale = 1.75f;
};

EditorShell *editor_shell_create(const clap_host_t *host,
                                 const EditorShellConfig &config,
                                 const EditorShellCallbacks &callbacks);
void editor_shell_prepare_destroy(EditorShell *shell);
void editor_shell_destroy(EditorShell *shell);
bool editor_shell_set_parent(EditorShell *shell, std::uintptr_t nativeParent);
bool editor_shell_show(EditorShell *shell);
bool editor_shell_hide(EditorShell *shell);
bool editor_shell_set_size(EditorShell *shell, std::uint32_t width,
                           std::uint32_t height);
void editor_shell_get_size(const EditorShell *shell, std::uint32_t *width,
                           std::uint32_t *height);
bool editor_shell_can_resize(const EditorShell *shell);
bool editor_shell_was_closed(const EditorShell *shell);
void editor_shell_start_timer(EditorShell *shell);
void editor_shell_stop_timer(EditorShell *shell);

} // namespace ccomidi

#endif
