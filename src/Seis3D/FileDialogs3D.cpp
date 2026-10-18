#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#endif
#include "Internal3D.h"
#include "ResourcePaths.h"
#include <algorithm>
#include <cstring>

namespace Seis3D::Detail3D {
namespace {
// Commit a selected Unicode path only when the complete UTF-8 value fits its editable field.
template<std::size_t Size>
void copySelectedPath3D(char (&field)[Size],const std::filesystem::path& path) {
    const auto value=pathToUtf8(path);
    if(value.size()>=Size) throw std::runtime_error("Selected path or filename exceeds the field length.");
    std::memcpy(field,value.c_str(),value.size()+1);
}
#ifdef _WIN32
// Use a native file selector while retaining resource lookup relative to the deployed program.
bool chooseFile3D(const std::filesystem::path& current,const wchar_t* filter,const wchar_t* extension,
                  bool save,std::filesystem::path& selected) {
    wchar_t file[32768]{};
    const auto initial=(std::filesystem::is_directory(current.parent_path())?current:current.filename()).wstring();
    if(initial.size()>=std::size(file)) throw std::runtime_error("Selected path or filename exceeds the field length.");
    std::copy(initial.begin(),initial.end(),file);
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();
    dialog.lpstrFile=file;dialog.nMaxFile=DWORD(std::size(file));dialog.lpstrFilter=filter;
    dialog.lpstrDefExt=extension;
    const auto initialFolder=getExecutableDirectory().wstring();dialog.lpstrInitialDir=initialFolder.c_str();
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_NOREADONLYRETURN:OFN_FILEMUSTEXIST);
    const bool accepted=(save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))==TRUE;
    if(accepted) selected=std::filesystem::path(file);
    else if(CommDlgExtendedError()) throw std::runtime_error("Windows could not open the file selector.");
    return accepted;
}
#endif
}

// Model input and output paths are independent, so exporting never redirects the next import.
void browseModelPath3D(Seis3D_State& s,bool save) {
#ifdef _WIN32
    std::filesystem::path selected;
    if(chooseFile3D(uiPath3D(save?s.modelExportPath3D:s.modelPath3D),
        L"Seis3D model\0*.s3dm\0All files\0*.*\0",L"s3dm",save,selected)) {
        if(save) copySelectedPath3D(s.modelExportPath3D,selected);
        else copySelectedPath3D(s.modelPath3D,selected);
    }
#else
    throw std::runtime_error("Edit the path field directly on this platform.");
#endif
}

// Keep the VTI save path independent of both S3DM output and model input.
void browseVtiModelPath3D(Seis3D_State& s) {
#ifdef _WIN32
    std::filesystem::path selected3D;
    if(chooseFile3D(uiPath3D(s.vtiModelExportPath3D),
        L"VTK ImageData\0*.vti\0All files\0*.*\0",L"vti",true,selected3D))
        copySelectedPath3D(s.vtiModelExportPath3D,selected3D);
#else
    throw std::runtime_error("Edit the path field directly on this platform.");
#endif
}

// Keep snapshot output separate from model exports so either filename can be reused safely.
void browseVtiWavefieldPath3D(Seis3D_State& s) {
#ifdef _WIN32
    std::filesystem::path selected3D;
    if(chooseFile3D(uiPath3D(s.vtiWavefieldExportPath3D),
        L"VTK ImageData wavefield\0*.vti\0All files\0*.*\0",L"vti",true,selected3D))
        copySelectedPath3D(s.vtiWavefieldExportPath3D,selected3D);
#else
    throw std::runtime_error("Edit the path field directly on this platform.");
#endif
}

// Use COM's modern folder picker with balanced initialization, including an existing apartment.
void browseExportFolder3D(Seis3D_State& s) {
#ifdef _WIN32
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE) throw std::runtime_error("Windows could not open the folder selector.");
    IFileOpenDialog* dialog=nullptr;IShellItem* selected=nullptr;
    std::filesystem::path result;
    HRESULT status=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(SUCCEEDED(status)) {
        DWORD options=0;status=dialog->GetOptions(&options);
        if(SUCCEEDED(status)) status=dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
        IShellItem* initial=nullptr;
        const auto current=uiPath3D(s.exportDirectory3D);
        if(SUCCEEDED(SHCreateItemFromParsingName(current.c_str(),nullptr,IID_PPV_ARGS(&initial)))) {
            dialog->SetFolder(initial);initial->Release();
        }
        if(SUCCEEDED(status)) status=dialog->Show(GetActiveWindow());
        if(SUCCEEDED(status)) status=dialog->GetResult(&selected);
        if(SUCCEEDED(status)) {
            PWSTR path=nullptr;status=selected->GetDisplayName(SIGDN_FILESYSPATH,&path);
            if(SUCCEEDED(status)) {result=path;CoTaskMemFree(path);}
        }
    }
    if(selected) selected->Release();
    if(dialog) dialog->Release();
    if(SUCCEEDED(initialized)) CoUninitialize();
    if(status==HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    if(FAILED(status)) throw std::runtime_error("Windows could not open the folder selector.");
    copySelectedPath3D(s.exportDirectory3D,result);
#else
    throw std::runtime_error("Edit the path field directly on this platform.");
#endif
}

// Choose a family basename; numbered component suffixes are added by the SEG-Y exporter.
void browseGatherDestination3D(Seis3D_State& s) {
#ifdef _WIN32
    const auto initial=uiPath3D(s.exportDirectory3D)/uiPath3D((std::string(s.exportPrefix3D)+".sgy").c_str());
    std::filesystem::path selected;
    if(!chooseFile3D(initial,L"SEG-Y basename\0*.sgy\0All files\0*.*\0",L"sgy",true,selected)) return;
    const auto prefix=pathToUtf8(selected.stem());
    validatedExportPrefix3D(prefix.c_str());
    if(prefix.size()>=sizeof(s.exportPrefix3D)||pathToUtf8(selected.parent_path()).size()>=sizeof(s.exportDirectory3D))
        throw std::runtime_error("Selected path or filename exceeds the field length.");
    copySelectedPath3D(s.exportDirectory3D,selected.parent_path());
    std::memcpy(s.exportPrefix3D,prefix.c_str(),prefix.size()+1);
#else
    throw std::runtime_error("Edit the path field directly on this platform.");
#endif
}

// Keep output names within the selected folder and reject Windows' ambiguous trailing characters.
std::string validatedExportPrefix3D(const char* prefix) {
    const std::string value=prefix;
    if(value.empty()||value=="."||value==".."||value.back()=='.'||value.back()==' '
        ||value.find_first_of("<>:\"/\\|?*")!=std::string::npos
        ||std::any_of(value.begin(),value.end(),[](unsigned char c){return c<32;}))
        throw std::runtime_error("Choose a file prefix without path separators or reserved characters.");
    return value;
}
} // namespace Seis3D::Detail3D
