#include "Common.h"
#include "HomeScreen.h"
#include "AppInfo.h"
#include "AppWidgets.h"
#include "AppTheme.h"
#include <algorithm>

namespace {
struct ManualEntry {const char* title;const char* body;};
constexpr const char* manualSections[]={"Quick start","Model and boundaries","Source and acquisition",
    "Wavefield and traces","Import and export","Mouse and troubleshooting","2D simulation and records","About SeisWave Lab"};
constexpr ManualEntry twoDimensional[]={
    {"Compute backend","2D and 3D offer Auto, CPU / OpenMP and CUDA. Apply the backend after selecting CPU workers. Auto falls back to CPU when CUDA or graphics interop is unavailable. Switching rebuilds the session; stop acquisition first."},
    {"Current Parameters","The parameter tab beside controls compares active values with unapplied drafts. Update parameters applies edits; changing dimensions rebuilds the model. Discard pending changes restores 2D drafts. Acquisition locks rebuild actions."},
    {"CPU / CUDA efficiency comparison","2D benchmarks an isolated copy of the current model. 3D uses a 32 cubed standard model with CPML 8. Both exclude four warm-up steps and time twenty steps, reporting CPU/CUDA time ratio and Vz relative L2 error. Raw live data is retained."},
    {"Physical model files","Use VTI as the primary 2D/3D model format: one file contains Vp, Vs, Density and spacing. In 2D, SEG-Y property groups remain available for external exchange. Receiver time series use SEG-Y. Loading a model resets the wavefield."},
    {"2D docked workspace","Use Workspace to choose Modeling, Wavefield + records or Record focus. Drag panel tabs to arrange controls, canvas and records. Layout changes retain model, raw records and display gain. Wheel zoom and middle-drag pan remain available during capture."},
    {"2D model and time step","Open the 2D sandbox. Use Simulation to set grid size, spacing, time step and PML; use Model Edit or Model I/O to define the medium. Changing the grid clears the physical model. Keep dt within the displayed safe limit."},
    {"2D source and capture","Set the source before recording. In Data Export, deploy a receiver line and choose TRIGGER & ACQUIRE for a new shot, or START CAPTURE to record the running fields on a new local timeline. Model, source and receiver edits are disabled during capture."},
    {"2D seismic record viewer","Record viewer opens a frozen Vz/Vx snapshot. Browse SEG-Y and Import shot for display load external data independently. Choose Receiver section or Single trace, then adjust selection and Trace options."},
    {"2D gain and reset behavior","Trace gain controls wiggle width; Heatmap range controls density saturation. AGC enhances weak arrivals in a display copy. Component changes and imports retain these settings. Reset View fits the plot; Reset viewer display restores presentation defaults only."},
    {"2D shot package export","Data Export lets you choose an output folder and package basename, including Unicode paths. Each shot creates a new folder with raw Vx/Vz SEG-Y, geometry and a hash manifest; CSV and frozen model/source arrays are optional. Existing packages are never overwritten. A package is not a restart checkpoint."},
    {"Detailed 2D documentation","The repository includes docs/user/Seis2D_User_Guide.md with real CUDA screenshots, a model-to-recording workflow, SEG-Y limits and troubleshooting. Code organization and regression results are in docs/development/SEIS2D_REFACTOR_AND_VIEWER.md."}};
constexpr ManualEntry quickStart[]={
    {"1. Enter the 3D sandbox","Choose the 3D card on Home. The initial grid is 300 x 50 x 300, with 100 receivers per X line and 5 Y rows (500 total). Acquisition defaults to one shot with 1000 samples, and the receiver limit is 500. The initial view uses Vz, Paper scientific and Coolwarm; Hold observed peak is off."},
    {"2. Initialize the model","In Simulation, choose a model preset, grid spacing and boundary mode, then initialize or rebuild the session. Check the memory budget and safe time step before increasing the grid."},
    {"3. Run or capture","Run provides a live monitor. For an exportable shot, open Acquisition, deploy the receivers and choose Capture current source. Capturing restarts the wavefield and records Vx, Vy and Vz."},
    {"4. Inspect and export","Select a receiver or one receiver row. Adjust Trace gain or AGC to inspect weak arrivals. Choose the export folder and filename prefix, then export the captured three-component SEG-Y record."}};
constexpr ManualEntry models[]={
    {"Choose the physical model","Procedural presets define Vp, Vs and density. A VTI model replaces the physical grid and materials after complete geometry and material validation."},
    {"Choose the boundary mode","Free surface plus CPML models a traction-free top and absorbing other faces. All absorbing CPML absorbs at all six faces. All reflecting walls returns wave energy from every face."},
    {"Boundary visibility","Show boundary layer changes the visible region only. It does not enable or disable absorption. Rebuild the session to apply changes to the physical boundary mode or CPML thickness."},
    {"Grid size and stability","The total physical cell budget is 300 x 300 x 400. The solver also checks padded allocation and VRAM. Safe dt is computed from maximum Vp, grid spacing and the spatial stencil."}};
constexpr ManualEntry acquisition[]={
    {"Place the source","In Source, set XYZ, source mechanism, amplitude and direction. Source depth must be at least two cells. Source wavelet and Simulation share a pending Ricker frequency. Apply frequency + reset fields or a new capture updates the source and CPML while preserving the model and display gain."},
    {"Deploy the receivers","X line fits one row at the middle of Y; Y line fits one column at the middle of X. Grid presets create multiple rows. Fit spread updates editable positions; Deploy mesh applies them to the GPU."},
    {"Record a shot","Set samples per shot and sample stride. Actual sample interval is shown in milliseconds. Capture current source creates a frozen record; Stop capture retains the partial record for inspection or export."},
    {"Run a shot spread","Configure shots per line, source line count and spacing. Start spread captures and exports each shot in order. Each shot has its own number; validate the output location and prefix before starting."}};
constexpr ManualEntry display[]={
    {"Wavefield colors and scale","Choose Vx, Vy, Vz, velocity magnitude or pressure. Visual preset changes palettes while preserving tuned gain and opacity. Reset display parameters is the explicit reset control. Wave gain and Trace gain are independent."},
    {"Automatic scale and peak history","With Hold observed peak off, colors track the current amplitude reference. Turn it on to keep a stable scale across time. Manual scale is useful when comparing amplitudes between runs."},
    {"Select a trace or receiver row","Receiver traces can show a single three-component trace, all receivers, one X row or one Y row. A captured record keeps its original geometry even when you edit the next receiver spread."},
    {"Enhance weak arrivals with AGC","Open Trace options and enable Automatic gain compensation. Adjust window, strength, maximum boost and noise floor. Start with 120 ms and 20-50x maximum boost. AGC changes relative display amplitudes; SEG-Y exports use original samples."}};
constexpr ManualEntry files[]={
    {"Import a model","Select an uncompressed raw-appended or ASCII VTI ImageData with PointData Vp, Vs, Density in SI units. Import requires an equal-spacing Cartesian grid with origin (0,0,0). Curved grids must be resampled. Click Validate + import 3D model to apply."},
    {"Export a model","Model export uses VTK ImageData (.vti). Choose model output selects an independent filename. The initialized physical grid, Vp, Vs and Density are exported without CPML or ghosts; Z is positive depth. Existing files are preserved."},
    {"Choose record filenames","Acquisition provides Browse export folder, Shot file prefix and Choose export name / location. A prefix named survey produces survey_0001_vx.sgy, survey_0001_vy.sgy, survey_0001_vz.sgy and survey_0001_geometry.csv."},
    {"Organize exports and import records","Create session subfolder keeps each capture or spread in a timestamped folder. Turn it off to write directly into the selected folder. Existing files require another name. Import shot record opens SEG-Y independently of the GPU model."}};
constexpr ManualEntry controls[]={
    {"3D keyboard shortcuts","Q cycles wave palettes, W cycles components, and E cycles model palettes. Hold Shift to cycle backwards. Space runs or pauses, N advances one step, C resets fields, R resets views, G toggles the grid, B toggles boundary visibility, and V toggles volume rendering. F1 opens shortcut help; Tab returns Home. Editing controls and open popups suspend shortcuts; Ctrl/Alt combinations are reserved. Gains and raw records are retained."},
    {"Slice navigation","Wheel zooms around the cursor; middle drag pans; left click links orthogonal slice positions. Reset view fits one slice; Reset views or R fits all slices."},
    {"Volume navigation","Left drag orbits, right drag zooms and middle drag pans. Reset camera restores the initial camera. Volume gain, density and sampling quality can be adjusted independently of slice colors."},
    {"Docking and language","Drag a window tab to dock or rearrange panels. Workspace presets restore useful layouts. The language selector changes interface text without resetting the simulation or display parameters."},
    {"If the record is empty","Confirm the model is initialized, receivers are deployed and the source is inside the model. Run supplies live data; Capture creates exportable samples. Check the selected data source, component, gain and plot fit."}};

// Render one documented step with theme-aware hierarchy and text wrapping at the available width.
void drawManualEntry(const ManualEntry& entry,int index) {
    const auto role=index%2?AppUi::FunctionRole::Display:AppUi::FunctionRole::Model;
    ImGui::PushFont(nullptr,27);
    AppUi::Widgets::TextColored(AppUi::functionColor(role),"%s",entry.title);ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+(std::min)(1100.0f,ImGui::GetContentRegionAvail().x));
    AppUi::Widgets::TextWrapped("%s",entry.body);ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0,24));ImGui::Separator();ImGui::Dummy(ImVec2(0,18));
}
// Pick a chapter without changing its persistent identity when the interface language changes.
void drawManualChapter(int chapter) {
    const ManualEntry* entries=nullptr;std::size_t count=0;
    switch(chapter) {
    case 0:entries=quickStart;count=std::size(quickStart);break;
    case 1:entries=models;count=std::size(models);break;
    case 2:entries=acquisition;count=std::size(acquisition);break;
    case 3:entries=display;count=std::size(display);break;
    case 4:entries=files;count=std::size(files);break;
    case 5:entries=controls;count=std::size(controls);break;
    case 6:entries=twoDimensional;count=std::size(twoDimensional);break;
    default:
        ImGui::PushFont(nullptr,32);AppUi::Widgets::TextColored(AppUi::palette().text,"%s",AppUi::productName);ImGui::PopFont();
        AppUi::Widgets::Text("Version: %s",AppUi::productVersion);
        AppUi::Widgets::Text("3D module: %s",AppUi::moduleName3D);
        AppUi::Widgets::TextWrapped("Interactive 2D and 3D elastic wave modeling, visualization, acquisition and SEG-Y analysis.");
        ImGui::Dummy(ImVec2(0,20));AppUi::Widgets::SeparatorText("Technology");
        AppUi::Widgets::TextWrapped("C++20 / NVIDIA CUDA / OpenGL 4.3 / Dear ImGui Docking / ImPlot");
        ImGui::Dummy(ImVec2(0,20));AppUi::Widgets::TextUnformatted("XIAO YU / MIT LICENSE");
        return;
    }
    for(std::size_t i=0;i<count;++i) drawManualEntry(entries[i],int(i));
}
}

// Present a responsive, scrollable guide as an independent application route with no GPU lifetime.
AppScreen drawOperationManualScreen(int width,int height) {
    (void)width;(void)height;
    static int chapter=0;
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(viewport->WorkSize);ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(24,20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushFont(nullptr,23);
    AppUi::Widgets::Begin("Operating manual###OperationManual",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    AppScreen route=AppScreen::UserManual;
    if(AppUi::Widgets::Button("Home")) route=AppScreen::Intro0;
    ImGui::SameLine();if(AppUi::Widgets::Button("Open 2D sandbox")) route=AppScreen::SeisSim_GPU;
    ImGui::SameLine();if(AppUi::Widgets::Button("Open 3D sandbox")) route=AppScreen::SeisSim_3D;
    ImGui::SameLine();AppUi::drawLanguageSelector(150);
    ImGui::Dummy(ImVec2(0,12));ImGui::PushFont(nullptr,36);AppUi::Widgets::TextUnformatted("Operating manual");ImGui::PopFont();
    AppUi::Widgets::TextDisabled("%s | v%s | %s",AppUi::productName,AppUi::productVersion,AppUi::moduleName3D);
    AppUi::Widgets::TextDisabled("MODEL / SOURCE / ACQUISITION / DISPLAY / EXPORT");ImGui::Separator();ImGui::Dummy(ImVec2(0,8));
    const bool wide=ImGui::GetContentRegionAvail().x>=1000;
    if(wide) {
        ImGui::BeginChild("ManualChapters",ImVec2(340,0),ImGuiChildFlags_Borders);
        for(int i=0;i<int(std::size(manualSections));++i)
            if(AppUi::Widgets::Selectable(manualSections[i],chapter==i,0,ImVec2(0,44))) chapter=i;
        ImGui::EndChild();ImGui::SameLine();
    } else {
        ImGui::SetNextItemWidth(-1);AppUi::Widgets::Combo("##ManualChapter",&chapter,manualSections,int(std::size(manualSections)));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(28,24));
    ImGui::BeginChild("ManualContent",ImVec2(0,0),ImGuiChildFlags_Borders);
    drawManualChapter(chapter);
    ImGui::EndChild();ImGui::PopStyleVar();ImGui::End();ImGui::PopFont();ImGui::PopStyleVar(3);
    return route;
}
