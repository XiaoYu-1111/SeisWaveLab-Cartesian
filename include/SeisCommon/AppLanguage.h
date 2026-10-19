#pragma once
#include <cstddef>

// Application-owned presentation preference; independent of the 2-D and 3-D physical states.
namespace AppUi {
enum class Language { English, Chinese };
// Return the active UI language without reading settings repeatedly.
Language currentLanguage();
// Load a validated preference from the executable's output directory.
void initializeLanguage();
// Apply a language immediately; tests and screenshots can avoid persisting it.
void setLanguage(Language language,bool persist=true);
// Translate plain text or a printf format; unknown diagnostics retain their original wording.
const char* text(const char* source);
// Translate a visible caption using a language-independent canonical English identity.
const char* label(const char* source);
// Reuse older saved window placement when introducing a canonical ID for a legacy window.
const char* windowLabel(const char* source);
// Translate a double-null-terminated combo list without changing option order or indices.
const char* items(const char* source);
// Draw the same bilingual selector on the homepage and in workspaces.
bool drawLanguageSelector(float width=180.0f);
// Draw language choices inside an existing menu or popup.
void drawLanguageMenu();
// Check catalog uniqueness and printf argument compatibility for regression checks.
bool validateLanguageCatalog();
}
