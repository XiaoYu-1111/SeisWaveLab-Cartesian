#pragma once
#include "imgui.h"

// Application presentation service; contains no simulation state, GPU handles or solver parameters.
namespace AppUi {
// Append new themes so existing saved theme indices retain their meaning.
enum class ThemeId { CudaGreen, OceanBlue, Graphite, ScientificLight, MidnightViolet, TealStudio, SoftJade, AmberLab=SoftJade, PaperSand,
    ArcticLab, WarmIvory, Count };
enum class FunctionRole { Simulation, Source, Model, Acquisition, Display, Navigation, Warning, Danger };
struct ThemePalette {
    ImVec4 background, panel, raised, input, text, muted, accent, border;
    bool light = false;
};
// Return the selected application theme and its presentation colors.
ThemeId currentTheme();
// Return a palette without changing the current selection.
const ThemePalette& themePalette(ThemeId id);
// Return the current application's presentation palette.
const ThemePalette& palette();
// Read a validated saved preference and apply colors after creating ImGui/ImPlot contexts.
void initializeTheme();
// Apply a theme; optional persistence keeps regression checks from changing user preferences.
void setTheme(ThemeId id,bool persist=true);
// Draw a compact selector, retaining any disk-write error for an adjacent explanatory tooltip.
bool drawThemeSelector(const char* label,float width=200.0f);
// Draw the theme submenu for workspace menus.
void drawThemeMenu();
// Convert a semantic color to a draw-list color with an optional opacity multiplier.
ImU32 color(ImVec4 value,float opacity=1.0f);
// Return a stable functional accent, with a darker variant for readable light-theme labels.
ImVec4 functionColor(FunctionRole role);
// Tint local controls and tabs by purpose without modifying the saved theme or simulation state.
class FunctionStyleScope {
public:
    explicit FunctionStyleScope(FunctionRole role,bool prominent=false);
    ~FunctionStyleScope();
    FunctionStyleScope(const FunctionStyleScope&)=delete;
    FunctionStyleScope& operator=(const FunctionStyleScope&)=delete;
private:
    int colorCount=0;
    ThemeId themeOnEntry=ThemeId::CudaGreen;
};
// Preserve readable text over legacy dark simulation canvases while the selected theme is light.
class ThemePanelScope {
public:
    // Restore active theme colors locally inside an enclosing legacy contrast scope.
    ThemePanelScope();
    // Balance all local color overrides without changing the selected theme.
    ~ThemePanelScope();
    ThemePanelScope(const ThemePanelScope&)=delete;
    ThemePanelScope& operator=(const ThemePanelScope&)=delete;
};
class LegacyContrastScope {
public:
    // Scope only presentation colors; selected preference and simulation state remain application owned.
    explicit LegacyContrastScope(bool legacyCanvas);
    // Restore the active application colors after rendering the legacy route.
    ~LegacyContrastScope();
    LegacyContrastScope(const LegacyContrastScope&)=delete;
    LegacyContrastScope& operator=(const LegacyContrastScope&)=delete;
private:
    int colorCount=0;
    ThemeId themeOnEntry=ThemeId::CudaGreen;
};
} // namespace AppUi
