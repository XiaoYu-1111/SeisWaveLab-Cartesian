#pragma once
#include "AppLanguage.h"
#include "imgui.h"
#include <utility>
#include <type_traits>
#include <vector>

// Localized application controls. ImGui itself and its internal identifiers remain unchanged.
namespace AppUi::Widgets {
// Translate textual printf arguments (status names, descriptions), preserving numeric arguments.
template<class T> decltype(auto) argument(T&& value) {
    if constexpr(std::is_convertible_v<T,const char*>) return text(value);
    else return std::forward<T>(value);
}
// Render a translated format with the original typed arguments.
template<class... A> void Text(const char* format,A&&... args) {ImGui::Text(text(format),argument(std::forward<A>(args))...);}
// Render wrapping help text in the selected language.
template<class... A> void TextWrapped(const char* format,A&&... args) {ImGui::TextWrapped(text(format),argument(std::forward<A>(args))...);}
// Preserve disabled text styling while translating its contents.
template<class... A> void TextDisabled(const char* format,A&&... args) {ImGui::TextDisabled(text(format),argument(std::forward<A>(args))...);}
// Preserve semantic colors while translating status and diagnostic text.
template<class... A> void TextColored(const ImVec4& color,const char* format,A&&... args) {ImGui::TextColored(color,text(format),argument(std::forward<A>(args))...);}
// Translate tooltips without altering their numeric measurements.
template<class... A> void SetTooltip(const char* format,A&&... args) {ImGui::SetTooltip(text(format),argument(std::forward<A>(args))...);}
// Keep explicit text ranges unchanged; ordinary null-terminated strings use the catalog.
inline void TextUnformatted(const char* value,const char* end=nullptr) {ImGui::TextUnformatted(end?value:text(value),end);}
// Translate a separator caption, which has no persistent widget identity.
inline void SeparatorText(const char* value) {ImGui::SeparatorText(text(value));}
// Measure the same translated text that will be rendered in responsive toolbars.
inline ImVec2 CalcTextSize(const char* value,const char* end=nullptr,bool hide=false,float wrap=-1) {return ImGui::CalcTextSize(end?value:text(value),end,hide,wrap);}

// Forward standard widget arguments while retaining stable IDs across language switches.
#define APP_LOCALIZED_WIDGET(Name) template<class... A> decltype(auto) Name(const char* key,A&&... args) {return ImGui::Name(label(key),std::forward<A>(args)...);}
APP_LOCALIZED_WIDGET(BeginTabItem)
APP_LOCALIZED_WIDGET(BeginMenu)
APP_LOCALIZED_WIDGET(SmallButton)
APP_LOCALIZED_WIDGET(Checkbox)
APP_LOCALIZED_WIDGET(RadioButton)
APP_LOCALIZED_WIDGET(MenuItem)
APP_LOCALIZED_WIDGET(Selectable)
APP_LOCALIZED_WIDGET(CollapsingHeader)
APP_LOCALIZED_WIDGET(SliderInt)
APP_LOCALIZED_WIDGET(SliderFloat)
APP_LOCALIZED_WIDGET(SliderFloat2)
APP_LOCALIZED_WIDGET(SliderFloat3)
APP_LOCALIZED_WIDGET(SliderInt2)
APP_LOCALIZED_WIDGET(InputInt)
APP_LOCALIZED_WIDGET(InputInt2)
APP_LOCALIZED_WIDGET(InputInt3)
APP_LOCALIZED_WIDGET(InputFloat)
APP_LOCALIZED_WIDGET(InputFloat2)
APP_LOCALIZED_WIDGET(InputFloat3)
APP_LOCALIZED_WIDGET(InputDouble)
APP_LOCALIZED_WIDGET(InputText)
APP_LOCALIZED_WIDGET(InputTextMultiline)
APP_LOCALIZED_WIDGET(DragFloat)
APP_LOCALIZED_WIDGET(DragFloat2)
APP_LOCALIZED_WIDGET(DragFloat3)
APP_LOCALIZED_WIDGET(DragInt)
APP_LOCALIZED_WIDGET(ColorEdit3)
APP_LOCALIZED_WIDGET(ColorEdit4)
#undef APP_LOCALIZED_WIDGET
// Preserve existing saved placement for windows that previously had no explicit stable suffix.
inline bool Begin(const char* key,bool* open=nullptr,ImGuiWindowFlags flags=0) {return ImGui::Begin(windowLabel(key),open,flags);}
// Accept both explicit ImVec2 and legacy brace-initialized button sizes.
inline bool Button(const char* key,const ImVec2& size=ImVec2(0,0)) {return ImGui::Button(label(key),size);}
// Translate both the caption and preview of custom combo menus.
inline bool BeginCombo(const char* key,const char* preview,ImGuiComboFlags flags=0) {return ImGui::BeginCombo(label(key),text(preview),flags);}
// Translate custom-painted UI captions with the same font and clipping as their original drawing.
inline void drawText(ImDrawList* draw,const ImVec2& position,ImU32 color,const char* value,const char* end=nullptr) {
    draw->AddText(position,color,end?value:text(value),end);
}
// Preserve explicit text ranges while translating ordinary drawn labels.
inline void drawText(ImDrawList* draw,ImFont* font,float size,const ImVec2& position,ImU32 color,const char* value,const char* end=nullptr,float wrap=0,const ImVec4* clip=nullptr) {
    draw->AddText(font,size,position,color,end?value:text(value),end,wrap,clip);
}
// Preserve packed-list option indices while translating each visible entry.
inline bool Combo(const char* key,int* selected,const char* entries,int height=-1) {return ImGui::Combo(label(key),selected,items(entries),height);}
// Translate fixed arrays used by theme and legacy selectors without changing their storage.
inline bool Combo(const char* key,int* selected,const char* const entries[],int count,int height=-1) {
    std::vector<const char*> translated;translated.reserve(count);
    for(int i=0;i<count;++i) translated.push_back(text(entries[i]));
    return ImGui::Combo(label(key),selected,translated.data(),count,height);
}
}
