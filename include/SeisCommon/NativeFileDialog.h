#pragma once
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <cstdio>
#endif

namespace SeisCommon {
struct FileDialogSeed {
    std::filesystem::path directory;
    std::wstring filename;
};

// A missing model/output directory must never become an invalid initial filename.
inline FileDialogSeed prepareFileDialogSeed(const std::filesystem::path& current,
        const std::filesystem::path& base,const std::wstring& fallback=L"model.vti") {
    auto isDirectory=[](const std::filesystem::path& p) {
        std::error_code ec;return !p.empty()&&std::filesystem::is_directory(p,ec);
    };
    auto path=current.empty()?base/std::filesystem::path(fallback):current;
    if(path.is_relative()) path=base/path;
    FileDialogSeed seed;
    seed.directory=isDirectory(path)?path:path.parent_path();
    seed.filename=isDirectory(path)?fallback:path.filename().wstring();
    while(!isDirectory(seed.directory)) {
        const auto parent=seed.directory.parent_path();
        if(parent.empty()||parent==seed.directory) {seed.directory=base;break;}
        seed.directory=parent;
    }
    if(!isDirectory(seed.directory)) throw std::runtime_error("No accessible initial folder for the file selector.");
    if(seed.filename.empty()||seed.filename==L"."||seed.filename==L".."
        ||seed.filename.find_first_of(L"<>:\"/\\|?*")!=std::wstring::npos
        ||std::any_of(seed.filename.begin(),seed.filename.end(),[](wchar_t c){return c<32;})
        ||seed.filename.back()==L'.'||seed.filename.back()==L' ')
        seed.filename=fallback;
    return seed;
}

#ifdef _WIN32
// Own all strings referenced by OPENFILENAME for the entire modal call.
struct NativeFileDialog {
    std::wstring directory;
    wchar_t filename[32768]{};
    OPENFILENAMEW options{};
    NativeFileDialog(const std::filesystem::path& current,const std::filesystem::path& base,
            const wchar_t* filter,const wchar_t* extension,const wchar_t* fallback=L"model.vti") {
        const auto seed=prepareFileDialogSeed(current,base,fallback);
        directory=seed.directory.wstring();
        if(seed.filename.size()>=std::size(filename)) throw std::runtime_error("Selected filename is too long.");
        std::copy(seed.filename.begin(),seed.filename.end(),filename);
        options.lStructSize=sizeof(options);options.hwndOwner=GetActiveWindow();
        options.lpstrFile=filename;options.nMaxFile=DWORD(std::size(filename));
        options.lpstrInitialDir=directory.c_str();options.lpstrFilter=filter;
        options.lpstrDefExt=extension;options.nFilterIndex=1;
        options.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY;
    }
    NativeFileDialog(const NativeFileDialog&)=delete;
    NativeFileDialog& operator=(const NativeFileDialog&)=delete;
    bool show(bool save) {
        options.Flags|=save?OFN_NOREADONLYRETURN:OFN_FILEMUSTEXIST;
        const auto workingDirectory=std::filesystem::current_path();
        const bool accepted=(save?GetSaveFileNameW(&options):GetOpenFileNameW(&options))==TRUE;
        const auto code=CommDlgExtendedError();
        // OFN_NOCHANGEDIR alone is not reliable for every Windows open dialog implementation.
        std::filesystem::current_path(workingDirectory);
        if(accepted) return true;
        if(code) {
            char message[128]{};std::snprintf(message,sizeof(message),"Windows file selector failed (0x%08lX).",static_cast<unsigned long>(code));
            throw std::runtime_error(message);
        }
        return false; // Cancel is not an error and never commits an edited path.
    }
};
inline bool chooseNativeFile(const std::filesystem::path& current,const std::filesystem::path& base,
        const wchar_t* filter,const wchar_t* extension,bool save,std::filesystem::path& selected,
        const wchar_t* fallback=L"model.vti") {
    NativeFileDialog dialog(current,base,filter,extension,fallback);
    if(!dialog.show(save)) return false;
    selected=std::filesystem::path(dialog.filename);return true;
}
#endif
}
