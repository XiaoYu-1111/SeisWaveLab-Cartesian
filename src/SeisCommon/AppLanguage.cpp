#include "AppLanguage.h"
#include "ResourcePaths.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AppUi {
namespace {
struct Translation {const char* english;const char* chinese;};
constexpr Translation translations[]={
#include "AppTranslations.inc"
};
Language selectedLanguage=Language::English;
bool savedLanguage=true;
// Build a fixed bidirectional dictionary once; unknown external text stays untouched.
const std::unordered_map<std::string,const Translation*>& dictionary() {
    static const auto entries=[] {
        std::unordered_map<std::string,const Translation*> result;
        for(const auto& item:translations) {result.emplace(item.english,&item);result.emplace(item.chinese,&item);}
        return result;
    }();return entries;
}
// Keep translated widget strings alive across multiple controls and arguments in a single frame.
const char* cachedLabel(const char* source) {
    static std::unordered_map<std::string,std::string> cache[2];
    auto& values=cache[int(selectedLanguage)];auto found=values.find(source);
    if(found!=values.end()) return found->second.c_str();
    std::string original(source);const auto marker=original.find("##");
    const std::string visible=original.substr(0,marker);
    std::string result=text(visible.c_str());
    const auto stable=original.find("###");
    // Internal host names and technical labels have no translation; retain their existing seed.
    if(dictionary().find(visible)==dictionary().end()) result=original;
    else result+="###"+(stable==std::string::npos?original:original.substr(stable+3));
    return values.emplace(original,std::move(result)).first->second.c_str();
}
// Extract complete printf conversions so translations cannot change argument type or order.
std::vector<std::string> formatArguments(const char* value) {
    std::vector<std::string> result;const std::string input(value);
    for(std::size_t i=0;i<input.size();++i) if(input[i]=='%') {
        if(i+1<input.size()&&input[i+1]=='%') {++i;continue;}
        const auto begin=i++;
        while(i<input.size()&&std::strchr("diuoxXfFeEgGaAcspn",input[i])==nullptr) ++i;
        result.push_back(input.substr(begin,i-begin+1));
    }return result;
}
}
// Read-only access for theme-independent language checks and screenshot restoration.
Language currentLanguage() {return selectedLanguage;}
// Read a versioned preference; invalid or unavailable settings use English.
void initializeLanguage() {
    std::ifstream input(getExecutableDirectory()/"ui_language.ini");std::string tag;int value=0;
    if(input>>tag>>value&&tag=="SeisUiLanguage1"&&value>=0&&value<=1) selectedLanguage=Language(value);
    else selectedLanguage=Language::English;
}
// Change only application text; never rebuild models, windows, fonts or rendering state.
void setLanguage(Language language,bool persist) {
    if(language!=Language::English&&language!=Language::Chinese) return;
    selectedLanguage=language;
    if(persist) {
        std::ofstream output(getExecutableDirectory()/"ui_language.ini",std::ios::trunc);
        output<<"SeisUiLanguage1 "<<int(language)<<'\n';output.flush();savedLanguage=bool(output);
    }
}
// Resolve either English or Chinese source wording to the selected language.
const char* text(const char* source) {
    if(!source) return source;
    const auto& entries=dictionary();const auto found=entries.find(source);
    return found==entries.end()?source:selectedLanguage==Language::English?found->second->english:found->second->chinese;
}
// Hidden IDs and existing ### suffixes are unchanged; other labels use a canonical suffix in both languages.
const char* label(const char* source) {return !source||source[0]=='#'?source:cachedLabel(source);}
// Migrate old legacy window settings once; existing stable Seis3D dock IDs need no migration.
const char* windowLabel(const char* source) {
    const auto* translated=label(source);const auto oldId=ImHashStr(source),newId=ImHashStr(translated);
    if(oldId!=newId&&!ImGui::FindWindowSettingsByID(newId)) {
        if(const auto* old=ImGui::FindWindowSettingsByID(oldId)) {
            const ImGuiWindowSettings copy=*old;
            auto* migrated=ImGui::CreateNewWindowSettings(translated);
            *migrated=copy;migrated->ID=newId;migrated->WantApply=true;
        }
    }
    return translated;
}
// Cache translated packed lists using the full list rather than its first item as the key.
const char* items(const char* source) {
    if(!source) return source;
    const char* end=source;while(*end) end+=std::strlen(end)+1;
    const std::string key(source,std::size_t(end-source)+1);
    static std::unordered_map<std::string,std::string> cache[2];auto& values=cache[int(selectedLanguage)];
    auto found=values.find(key);if(found!=values.end()) return found->second.c_str();
    std::string result;for(const char* p=source;*p;p+=std::strlen(p)+1) {result+=text(p);result+='\0';}result+='\0';
    return values.emplace(key,std::move(result)).first->second.c_str();
}
// Keep language names recognizable in either language and preserve selection order.
bool drawLanguageSelector(float width) {
    int value=int(selectedLanguage);ImGui::SetNextItemWidth(width);
    const bool changed=ImGui::Combo("##AppLanguage",&value,"English\0简体中文\0");
    if(changed) setLanguage(Language(value));
    if(!savedLanguage&&ImGui::IsItemHovered()) ImGui::SetTooltip("%s",text("Language applied; preference could not be saved."));
    return changed;
}
// Offer a stable language menu without changing the surrounding popup identity.
void drawLanguageMenu() {
    if(ImGui::BeginMenu(label("Language"))) {
        if(ImGui::MenuItem("English",nullptr,selectedLanguage==Language::English)) setLanguage(Language::English);
        if(ImGui::MenuItem("简体中文",nullptr,selectedLanguage==Language::Chinese)) setLanguage(Language::Chinese);
        ImGui::EndMenu();
    }
}
// Validate immutable resource data independently of the GPU and ImGui context.
bool validateLanguageCatalog() {
    std::unordered_set<std::string> english;
    for(const auto& item:translations)
        if(!english.emplace(item.english).second||formatArguments(item.english)!=formatArguments(item.chinese)) return false;
    return true;
}
}
