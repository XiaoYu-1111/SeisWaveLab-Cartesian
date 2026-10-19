#include "AppWidgets.h"
#include "AppTheme.h"
#include "ResourcePaths.h"
#include "implot.h"
#include <fstream>

namespace AppUi {
namespace {
ThemeId selectedTheme=ThemeId::CudaGreen;
bool savedPreference=true;
constexpr const char* themeNames[]={"CUDA Green","Ocean Blue","Graphite","Scientific Light","Midnight Violet","Teal Studio","Soft Jade","Paper","Arctic","Warm Ivory"};
const ThemePalette palettes[]={
    {{.035f, .055f, .052f, 1},{.065f, .095f, .09f, 1},{.10f, .17f, .14f, 1},{.10f, .17f, .14f, 1},{.91f, .96f, .93f, 1},{.55f, .68f, .62f, 1},{.46f, .86f, .27f, 1},{.22f, .4f, .29f, 1},false},
    {{.035f, .052f, .08f, 1},{.06f, .09f, .135f, 1},{.09f, .16f, .23f, 1},{.09f, .16f, .23f, 1},{.91f, .95f, .99f, 1},{.54f, .66f, .79f, 1},{.22f, .68f, .96f, 1},{.2f, .36f, .52f, 1},false},
    {{.065f, .07f, .08f, 1},{.105f, .115f, .13f, 1},{.17f, .18f, .20f, 1},{.17f, .18f, .20f, 1},{.93f, .94f, .96f, 1},{.6f, .63f, .68f, 1},{.72f, .77f, .84f, 1},{.31f, .34f, .39f, 1},false},
    {{.93f, .95f, .97f, 1},{.99f, .995f, 1, 1},{.88f, .92f, .96f, 1},{.88f, .92f, .96f, 1},{.12f, .18f, .24f, 1},{.35f, .43f, .51f, 1},{.035f, .36f, .64f, 1},{.65f, .74f, .82f, 1},true},
    {{.045f, .04f, .08f, 1},{.085f, .075f, .135f, 1},{.16f, .14f, .245f, 1},{.16f, .14f, .245f, 1},{.95f, .93f, .99f, 1},{.65f, .61f, .77f, 1},{.69f, .54f, .97f, 1},{.32f, .27f, .45f, 1},false},
    {{.025f, .065f, .075f, 1},{.045f, .105f, .12f, 1},{.08f, .19f, .205f, 1},{.08f, .19f, .205f, 1},{.91f, .97f, .97f, 1},{.54f, .71f, .73f, 1},{.16f, .80f, .76f, 1},{.18f, .40f, .42f, 1},false},
    {{.94f, .97f, .94f, 1},{.98f, .995f, .97f, 1},{.87f, .93f, .87f, 1},{.87f, .93f, .87f, 1},{.13f, .23f, .17f, 1},{.34f, .45f, .37f, 1},{.12f, .43f, .28f, 1},{.65f, .76f, .67f, 1},true},
    {{.96f, .945f, .91f, 1},{.995f, .985f, .96f, 1},{.92f, .905f, .865f, 1},{.92f, .905f, .865f, 1},{.16f, .20f, .19f, 1},{.36f, .41f, .38f, 1},{.10f, .40f, .32f, 1},{.68f, .70f, .63f, 1},true},
    {{.935f, .955f, .975f, 1},{.995f, .998f, 1, 1},{.91f, .945f, .975f, 1},{.91f, .945f, .975f, 1},{.10f, .17f, .26f, 1},{.34f, .43f, .53f, 1},{.13f, .34f, .66f, 1},{.65f, .74f, .83f, 1},true},
    {{.97f, .95f, .92f, 1},{1, .99f, .97f, 1},{.96f, .935f, .89f, 1},{.96f, .935f, .89f, 1},{.22f, .18f, .15f, 1},{.44f, .38f, .32f, 1},{.64f, .31f, .12f, 1},{.76f, .69f, .59f, 1},true}
};
constexpr int themeOrder[]={7,8,6,0,1,2,3,4,5,9};
static_assert(sizeof(themeNames)/sizeof(*themeNames)==int(ThemeId::Count));
static_assert(sizeof(palettes)/sizeof(*palettes)==int(ThemeId::Count));

// Blend opaque semantic colors for consistent control states across light and dark palettes.
ImVec4 blend(ImVec4 a,ImVec4 b,float weight) {
    return {a.x+(b.x-a.x)*weight,a.y+(b.y-a.y)*weight,a.z+(b.z-a.z)*weight,1};
}

// Apply semantic colors while retaining the application's font sizes and spacing configuration.
void configureThemeStyle(ImGuiStyle& style) {
    const auto& p=palette();
    if(p.light) ImGui::StyleColorsLight(&style);else ImGui::StyleColorsDark(&style);
    style.WindowPadding = {12, 10};
    style.FramePadding = {8, 5};
    style.ItemSpacing = {8, 7};
    style.FrameRounding = 4;
    style.TabRounding = 4;
    style.GrabRounding = 4;
    style.ChildRounding = 5;
    style.PopupRounding = 6;
    style.FrameBorderSize = 1;
    style.DisabledAlpha = .65f;
    auto *c = style.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.muted;
    c[ImGuiCol_WindowBg] = p.panel;
    c[ImGuiCol_ChildBg] = p.panel;
    c[ImGuiCol_PopupBg] = p.panel;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_FrameBg] = p.input;
    c[ImGuiCol_FrameBgHovered] = blend(p.input, p.accent, .2f);
    c[ImGuiCol_FrameBgActive] = blend(p.input, p.accent, .35f);
    c[ImGuiCol_Button] = blend(p.input, p.accent, .12f);
    c[ImGuiCol_ButtonHovered] = blend(p.input, p.accent, .3f);
    c[ImGuiCol_ButtonActive] = blend(p.input, p.accent, .5f);
    c[ImGuiCol_Header] = blend(p.panel, p.accent, .15f);
    c[ImGuiCol_HeaderHovered] = blend(p.panel, p.accent, .3f);
    c[ImGuiCol_HeaderActive] = blend(p.panel, p.accent, .45f);
    c[ImGuiCol_Tab] = p.input;
    c[ImGuiCol_TabSelected] = blend(p.panel, p.accent, .22f);
    c[ImGuiCol_TabHovered] = blend(p.panel, p.accent, .4f);
    c[ImGuiCol_TabDimmed] = p.panel;
    c[ImGuiCol_TabDimmedSelected] = p.input;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TitleBg] = p.background;
    c[ImGuiCol_TitleBgActive] = p.input;
    c[ImGuiCol_MenuBarBg] = p.background;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = blend(p.accent, p.text, .2f);
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_DockingPreview] = {p.accent.x, p.accent.y, p.accent.z, .35f};
    c[ImGuiCol_DockingEmptyBg] = p.background;
    c[ImGuiCol_ScrollbarBg] = p.background;
    c[ImGuiCol_ScrollbarGrab] = p.border;
    c[ImGuiCol_TextSelectedBg] = {p.accent.x, p.accent.y, p.accent.z, .3f};
}
// Apply the selected presentation style to the application and refresh ImPlot's automatic colors.
void applyTheme() {configureThemeStyle(ImGui::GetStyle());if(ImPlot::GetCurrentContext()) ImPlot::StyleColorsAuto();}
} // namespace

// Expose only presentation state; no simulation module owns this preference.
ThemeId currentTheme() {return selectedTheme;}
// Reject invalid persisted values before indexing the palette array.
const ThemePalette& themePalette(ThemeId id) {
    const int index=int(id);return palettes[index>=0&&index<int(ThemeId::Count)?index:0];
}
// Read the palette selected by the application toolbar.
const ThemePalette& palette() {return themePalette(selectedTheme);}
// Apply opacity consistently for both light and dark draw-list surfaces.
ImU32 color(ImVec4 value,float opacity) {value.w*=opacity;return ImGui::ColorConvertFloat4ToU32(value);}

// Keep functional colors consistent across themes while retaining readable luminance on each surface.
ImVec4 functionColor(FunctionRole role) {
    const bool light=palette().light;
    switch(role) {
    case FunctionRole::Simulation:return light?ImVec4(.20f,.46f,.23f,1):ImVec4(.40f,.78f,.43f,1);
    case FunctionRole::Model:return light?ImVec4(.24f,.39f,.64f,1):ImVec4(.42f,.63f,.93f,1);
    case FunctionRole::Display:return light?ImVec4(.12f,.46f,.45f,1):ImVec4(.25f,.76f,.73f,1);
    case FunctionRole::Acquisition:return light?ImVec4(.48f,.30f,.63f,1):ImVec4(.76f,.55f,.94f,1);
    case FunctionRole::Warning:return light?ImVec4(.66f,.42f,.12f,1):ImVec4(.95f,.70f,.28f,1);
    case FunctionRole::Danger:return light?ImVec4(.70f,.25f,.19f,1):ImVec4(.95f,.42f,.35f,1);
    default:return palette().accent;
    }
}

// Scope all purpose-dependent widget colors; labels retain the theme's high-contrast foreground.
FunctionStyleScope::FunctionStyleScope(FunctionRole role,bool prominent) {
    themeOnEntry=currentTheme();
    const auto& p=palette();const auto accent=functionColor(role);
    const auto push=[&](ImGuiCol slot,ImVec4 value){ImGui::PushStyleColor(slot,value);++colorCount;};
    (void)prominent;
    push(ImGuiCol_Button,blend(p.panel,accent,.19f));
    push(ImGuiCol_ButtonHovered,blend(p.panel,accent,.33f));
    push(ImGuiCol_ButtonActive,blend(p.panel,accent,.48f));
    push(ImGuiCol_Header,blend(p.panel,accent,.16f));
    push(ImGuiCol_HeaderHovered,blend(p.panel,accent,.29f));
    push(ImGuiCol_HeaderActive,blend(p.panel,accent,.42f));
    push(ImGuiCol_Border,blend(p.panel,accent,.60f));
}

// Return every pushed color even when a panel exits early or an operation throws.
FunctionStyleScope::~FunctionStyleScope() {
    ImGui::PopStyleColor(colorCount);
    if(currentTheme()!=themeOnEntry) applyTheme();
}

// Load only a known version and bounded theme index; missing or malformed files retain CUDA Green.
void initializeTheme() {
    std::ifstream input(getExecutableDirectory()/"ui_theme.ini");
    std::string signature;int index=-1;input>>signature>>index;
    selectedTheme=(signature=="SeisUiTheme1"||signature=="SeisUiTheme2")&&index>=0&&index<int(ThemeId::Count)?ThemeId(index):ThemeId::CudaGreen;
    if(signature=="SeisUiTheme1"&&index==6) selectedTheme=ThemeId::WarmIvory;
    applyTheme();
}
// Persist a small, versioned preference beside the executable, keeping generated files under outer out.
void setTheme(ThemeId id,bool persist) {
    if(int(id)<0||id>=ThemeId::Count) return;
    selectedTheme=id;applyTheme();
    if(persist) {
        std::ofstream output(getExecutableDirectory()/"ui_theme.ini",std::ios::trunc);
        output<<"SeisUiTheme2 "<<int(id)<<'\n';output.flush();savedPreference=bool(output);
    }
}
// Offer the same theme collection in the home screen and workspace display settings.
bool drawThemeSelector(const char* label,float width) {
    int index=0;const char* names[10];
    for(int i=0;i<10;++i) {names[i]=themeNames[themeOrder[i]];if(themeOrder[i]==int(currentTheme())) index=i;}
    ImGui::SetNextItemWidth(width);
    const bool changed=AppUi::Widgets::Combo(label,&index,names,10);
    if(changed) setTheme(ThemeId(themeOrder[index]));
    if(!savedPreference&&ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("Theme applied. Could not save ui_theme.ini beside the executable.");
    return changed;
}
// Reuse the selector choices in compact workspace menus.
void drawThemeMenu() {
    if(AppUi::Widgets::BeginMenu("Interface theme")) {
        for(int index:themeOrder) if(AppUi::Widgets::MenuItem(themeNames[index],nullptr,currentTheme()==ThemeId(index))) setTheme(ThemeId(index));
        ImGui::EndMenu();
    }
}
// Legacy routes draw translucent panels over dark GL canvases; retain their text contrast locally.
ThemePanelScope::ThemePanelScope() {
    ImGuiStyle desired=ImGui::GetStyle();configureThemeStyle(desired);
    for(int i=0;i<ImGuiCol_COUNT;++i) ImGui::PushStyleColor(ImGuiCol(i),desired.Colors[i]);
}
// Restore the enclosing legacy canvas colors after drawing a modern themed tool window.
ThemePanelScope::~ThemePanelScope() {ImGui::PopStyleColor(ImGuiCol_COUNT);}

LegacyContrastScope::LegacyContrastScope(bool legacyCanvas) {
    themeOnEntry=currentTheme();
    if(!legacyCanvas||!palette().light) return;
    ImGuiStyle fallback=ImGui::GetStyle();ImGui::StyleColorsDark(&fallback);
    fallback.Colors[ImGuiCol_CheckMark]=palette().accent;
    fallback.Colors[ImGuiCol_SliderGrab]=palette().accent;
    for(int i=0;i<ImGuiCol_COUNT;++i) ImGui::PushStyleColor(ImGuiCol(i),fallback.Colors[i]);
    colorCount=ImGuiCol_COUNT;
}
// Restore the outer application theme without changing its persisted selection.
LegacyContrastScope::~LegacyContrastScope() {
    if(colorCount) ImGui::PopStyleColor(colorCount);
    if(currentTheme()!=themeOnEntry) applyTheme();
}
} // namespace AppUi
