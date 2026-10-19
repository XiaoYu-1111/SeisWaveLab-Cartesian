#pragma once
#include "imgui.h"

namespace AppUi {
// Keep workspace commands out of text editing, active controls, popups and OS/editor combinations.
inline bool workspaceShortcutsAvailable(bool allowShift=false) {
    const auto& io=ImGui::GetIO();
    return !io.WantTextInput&&!ImGui::IsAnyItemActive()
        &&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)
        &&!io.KeyCtrl&&!io.KeyAlt&&!io.KeySuper&&(allowShift||!io.KeyShift);
}

// Advance a bounded display selection in either direction without resetting its associated parameters.
inline int cycleDisplaySelection(int current,int count,bool reverse) {
    if(count<=0) return 0;
    const int normalized=(current%count+count)%count;
    return (normalized+(reverse?count-1:1))%count;
}
} // namespace AppUi
