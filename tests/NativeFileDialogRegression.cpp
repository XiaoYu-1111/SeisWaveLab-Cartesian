#include "NativeFileDialog.h"
#include <fstream>
#include <iostream>
#include <vector>

static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
#ifdef _WIN32
struct DialogProbe {bool initialized=false,timedOut=false;};
static UINT_PTR CALLBACK probe(HWND window,UINT message,WPARAM timer,LPARAM data) {
    if(message==WM_INITDIALOG) {
        auto* options=reinterpret_cast<OPENFILENAMEW*>(data);
        SetWindowLongPtrW(window,GWLP_USERDATA,options->lCustData);
        SetTimer(window,2,5000,nullptr);
    }
    auto* state=reinterpret_cast<DialogProbe*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NOTIFY&&state&&reinterpret_cast<OFNOTIFYW*>(data)->hdr.code==CDN_INITDONE) {
        state->initialized=true;
        // Close after Explorer has completed initialization, keeping the test unattended.
        SetTimer(window,1,150,nullptr);
    }
    if(message==WM_TIMER&&state) {
        KillTimer(window,timer);const auto parent=GetParent(window);
        if(timer==2) {state->timedOut=true;PostMessageW(parent,WM_COMMAND,IDCANCEL,0);}
        else PostMessageW(parent,WM_COMMAND,IDCANCEL,0);
    }
    return 0;
}
#endif
int main(int argc,char** argv) {
 try {
    const auto base=std::filesystem::absolute(argv[0]).parent_path();
    const auto folder=base/L"checks/native-dialog/中文目录";
    std::filesystem::create_directories(folder);
    const auto existing=folder/L"输入模型.vti";
    {std::ofstream file(existing);file<<"file selector fixture";}
    const std::vector<std::filesystem::path> paths={
        base/L"missing-dialog-folder/nested/model.vti",existing,folder,
        {},L"relative-folder/model.vti",base/L"invalid?.vti",L"Q:/not-present/model.vti"};
    for(const auto& path:paths) {
        const auto seed=SeisCommon::prepareFileDialogSeed(path,base);
        check(std::filesystem::is_directory(seed.directory),"Initial directory is not accessible");
        check(!seed.filename.empty()&&seed.filename.find_first_of(L"/\\:?")==std::wstring::npos,"Invalid initial filename");
    }
    check(SeisCommon::prepareFileDialogSeed(existing,base).directory==folder,"Unicode directory lost");
    check(SeisCommon::prepareFileDialogSeed(existing,base).filename==L"输入模型.vti","Unicode filename lost");
    check(SeisCommon::prepareFileDialogSeed(L"relative-folder/model.vti",base).directory==base,"Relative paths must resolve beside the program");
    std::cout<<"PASS: absent/nested/relative/empty/Unicode/invalid paths and directory-only input\n";
#ifdef _WIN32
    if(argc>1&&std::string(argv[1])=="--native-dialogs") {
        const auto cwd=std::filesystem::current_path();
        int index=0;
        for(bool save:{false,true}) for(const auto& path:paths) {
            std::cout<<"Native cancel "<<++index<<(save?" save":" open")<<std::endl;
            SeisCommon::NativeFileDialog dialog(path,base,L"VTK ImageData (*.vti)\0*.vti\0",L"vti");
            DialogProbe state;dialog.options.Flags|=OFN_ENABLEHOOK;dialog.options.lpfnHook=probe;
            dialog.options.lCustData=reinterpret_cast<LPARAM>(&state);
            check(!dialog.show(save)&&state.initialized&&!state.timedOut,"Native dialog failed to open/cancel");
            check(std::filesystem::current_path()==cwd,"File selector changed the working directory");
        }
        std::cout<<"PASS: 14 real Windows open/save cancellation cases including Unicode folders, with unchanged working directory\n";
    }
#endif
    return 0;
 } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
