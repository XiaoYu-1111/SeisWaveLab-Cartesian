// Private export controls; capture samples and physical inputs are owned by AcquisitionState2D.
char exportDirectory2D[1024]{};
char exportPrefix2D[128]="seis2d_shot";
bool exportSession2D=true,exportCsv2D=true,exportModel2D=true;
std::filesystem::path activeExportDirectory2D,lastExportDirectory2D;
std::string exportDevice2D;

// Convert an editable UTF-8 field to a native Unicode path without depending on the working directory.
inline std::filesystem::path exportPath2D(const char* text) {
    auto path=std::filesystem::u8path(text);
    return path.is_absolute()?path:getExecutableDirectory()/path;
}
// Commit complete path text only when the entire Unicode value fits the editable field.
template<std::size_t Size>
void copyExportField2D(char (&field)[Size],const std::filesystem::path& path) {
    const auto value=pathToUtf8(path);
    if(value.size()>=Size) throw std::invalid_argument("Selected path or filename exceeds the field length.");
    std::memcpy(field,value.c_str(),value.size()+1);
}
// Report recoverable export/configuration failures inside the workspace instead of exiting the app.
template<class Action>
bool acquisitionAction2D(Action action) {
    try {action();return true;}
    catch(const std::exception& error) {popupMessage2D=error.what();showErrorPopup2D=true;return false;}
}
// Initialize a Unicode default output directory beside the deployed application.
inline void ensureExportDirectory2D() {
    if(!exportDirectory2D[0]) copyExportField2D(exportDirectory2D,getExecutableDirectory()/"exports2d");
}
// Freeze one output root for the complete single-shot or multi-shot acquisition session.
inline void beginExportSession2D(bool validateCapture=true) {
    ensureExportDirectory2D();validateExportPrefix2D(exportPrefix2D);
    if(validateCapture) {
    const int stride=getReceiverSamplingStride(simulationContext2D.dt,acquisition2D.samplingRateHz);
    const double interval=double(stride)*simulationContext2D.dt;
    const double samples=std::ceil(acquisition2D.totalDurationSec/interval);
    if(!std::isfinite(samples)||samples<1||samples>32767||std::llround(interval*1e6)<1||std::llround(interval*1e6)>32767)
        throw std::invalid_argument("Shot export requires 1-32767 samples and a 1-32767 microsecond interval. Adjust duration or sampling rate.");
    }
    activeExportDirectory2D=exportPath2D(exportDirectory2D);
    if(exportSession2D) activeExportDirectory2D/="session-"+std::to_string(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
// Choose a directory through the native COM picker without changing process resource lookup.
inline void browseExportDirectory2D() {
#ifdef _WIN32
    struct DialogResources {
        bool uninitialize=false;IFileOpenDialog* dialog=nullptr;IShellItem* item=nullptr;
        ~DialogResources(){if(item)item->Release();if(dialog)dialog->Release();if(uninitialize)CoUninitialize();}
    } resources;
    const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    resources.uninitialize=SUCCEEDED(init);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE) throw std::runtime_error("Cannot initialize output folder selector.");
    HRESULT result=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&resources.dialog));
    DWORD flags=0;if(SUCCEEDED(result)) result=resources.dialog->GetOptions(&flags);
    if(SUCCEEDED(result)) result=resources.dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
    if(SUCCEEDED(result)) result=resources.dialog->Show(GetActiveWindow());
    if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    if(SUCCEEDED(result)) result=resources.dialog->GetResult(&resources.item);
    PWSTR path=nullptr;if(SUCCEEDED(result)) result=resources.item->GetDisplayName(SIGDN_FILESYSPATH,&path);
    if(FAILED(result)) throw std::runtime_error("Cannot select an output directory.");
    const std::filesystem::path selected(path);CoTaskMemFree(path);copyExportField2D(exportDirectory2D,selected);
#endif
}
// Select a package basename and parent location; the actual result is a numbered directory, not one file.
inline void browsePackageName2D() {
#ifdef _WIN32
    ensureExportDirectory2D();wchar_t file[32768]{};
    const auto initial=(exportPath2D(exportDirectory2D)/std::filesystem::u8path(exportPrefix2D)).wstring();
    if(initial.size()>=std::size(file)) throw std::invalid_argument("Output path is too long.");
    std::copy(initial.begin(),initial.end(),file);OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);
    dialog.hwndOwner=GetActiveWindow();dialog.lpstrFile=file;dialog.nMaxFile=DWORD(std::size(file));
    dialog.lpstrTitle=L"Choose shot package basename and location";dialog.lpstrFilter=L"Shot package basename\0*.*\0";
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_NOREADONLYRETURN;
    if(GetSaveFileNameW(&dialog)) {
        const std::filesystem::path selected(file);validateExportPrefix2D(pathToUtf8(selected.filename()));
        copyExportField2D(exportDirectory2D,selected.parent_path());copyExportField2D(exportPrefix2D,selected.filename());
    } else if(CommDlgExtendedError()) throw std::runtime_error("Cannot open package name selector.");
#endif
}
// Offer one consistent destination for manual, completed and multi-shot records.
inline void drawExportDestination2D() {
    ensureExportDirectory2D();
    if(parameterGroup2D("Shot package output")) {
        ImGui::BeginDisabled(acquisition2D.isRecording||autoAcquisition2D.state==AUTO_ACQ_RUNNING);
        AppUi::Widgets::TextUnformatted("Export directory");ImGui::SetNextItemWidth(-1);
        if(AppUi::Widgets::InputText("##ExportDirectory2D",exportDirectory2D,sizeof(exportDirectory2D))) activeExportDirectory2D.clear();
        if(AppUi::Widgets::Button("Browse export folder")) acquisitionAction2D([]{browseExportDirectory2D();activeExportDirectory2D.clear();});
        AppUi::Widgets::TextUnformatted("Shot file prefix");ImGui::SetNextItemWidth(-1);
        AppUi::Widgets::InputText("##ExportPrefix2D",exportPrefix2D,sizeof(exportPrefix2D));
        if(AppUi::Widgets::Button("Choose package name / location")) acquisitionAction2D([]{browsePackageName2D();activeExportDirectory2D.clear();});
        if(AppUi::Widgets::Checkbox("Create session subfolder",&exportSession2D)) activeExportDirectory2D.clear();
        AppUi::Widgets::Checkbox("Include time-column Vz CSV",&exportCsv2D);
        AppUi::Widgets::Checkbox("Archive frozen model and wavelet",&exportModel2D);
        ImGui::EndDisabled();
        AppUi::Widgets::TextWrapped("Each shot is a new numbered folder. Existing packages are never overwritten.");
        if(!lastExportDirectory2D.empty()) {
            AppUi::Widgets::TextWrapped("Last package: %s",pathToUtf8(lastExportDirectory2D.filename()).c_str());
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",pathToUtf8(lastExportDirectory2D).c_str());
            if(AppUi::Widgets::SmallButton("Copy export path")) ImGui::SetClipboardText(pathToUtf8(lastExportDirectory2D).c_str());
        }
    }
}
