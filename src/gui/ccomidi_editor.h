#ifndef CCOMIDI_EDITOR_H
#define CCOMIDI_EDITOR_H

#include <cstdint>

#include <clap/ext/gui.h>

#include "plugin/ccomidi_plugin_shared.h"

namespace ccomidi {

EditorState *editor_create(Plugin *plugin);
void editor_prepare_destroy(EditorState *editor);
void editor_destroy(EditorState *editor);
bool editor_show(EditorState *editor);
bool editor_hide(EditorState *editor);
bool editor_set_parent(EditorState *editor, std::uintptr_t nativeParent);
bool editor_set_size(EditorState *editor, std::uint32_t width, std::uint32_t height);
void editor_get_size(EditorState *editor, std::uint32_t *width, std::uint32_t *height);
bool editor_can_resize(EditorState *editor);
bool editor_was_closed(EditorState *editor);
void editor_tick(EditorState *editor);
void editor_start_internal_timer(EditorState *editor);
void editor_stop_internal_timer(EditorState *editor);

} // namespace ccomidi

#endif
