#pragma once

#include "imgui.h"
#include <pugl/pugl.h>

IMGUI_IMPL_API bool ImGui_ImplPugl_Init(PuglView *view);
IMGUI_IMPL_API void ImGui_ImplPugl_Shutdown();
IMGUI_IMPL_API void ImGui_ImplPugl_NewFrame();
IMGUI_IMPL_API void ImGui_ImplPugl_ProcessEvent(const PuglEvent *event);
