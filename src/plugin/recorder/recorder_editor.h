#ifndef CCOMIDI_RECORDER_EDITOR_H
#define CCOMIDI_RECORDER_EDITOR_H

#include <cstdint>

#include "plugin/recorder/recorder_plugin_shared.h"

namespace ccomidi {

RecorderEditorState *recorder_editor_create(RecorderPlugin *plugin);
void recorder_editor_prepare_destroy(RecorderEditorState *editor);
void recorder_editor_destroy(RecorderEditorState *editor);
bool recorder_editor_show(RecorderEditorState *editor);
bool recorder_editor_hide(RecorderEditorState *editor);
bool recorder_editor_set_parent(RecorderEditorState *editor,
                                std::uintptr_t nativeParent);
bool recorder_editor_set_size(RecorderEditorState *editor, std::uint32_t width,
                              std::uint32_t height);
void recorder_editor_get_size(RecorderEditorState *editor,
                              std::uint32_t *width, std::uint32_t *height);
bool recorder_editor_can_resize(RecorderEditorState *editor);
void recorder_editor_start_timer(RecorderEditorState *editor);
void recorder_editor_stop_timer(RecorderEditorState *editor);

} // namespace ccomidi

#endif
