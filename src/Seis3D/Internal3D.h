#pragma once
#include "Seis3D_Sim.h"
#include "Cuda_Seis3D.cuh"
#include "imgui.h"
#include <stdexcept>
namespace Seis3D::Detail3D {
// Honor this panel's one-frame fold command without changing parameter values.
bool sectionHeader3D(const char* label,ImGuiTreeNodeFlags flags=ImGuiTreeNodeFlags_None);
// Draw the physical parameter groups in the control tab or a diagnostic panel.
void drawSimulationControls3D(Seis3D_State& s);
inline constexpr double gibibyte3D=1024.0*1024.0*1024.0;
// Decode an editable UTF-8 filesystem path.
std::filesystem::path uiPath3D(const char* text);
// Select a model import or independent export filename without changing the working directory.
void browseModelPath3D(Seis3D_State& s,bool save);
// Select an independent VTK ImageData export filename with Unicode path support.
void browseVtiModelPath3D(Seis3D_State& s);
// Select an independent VTI filename for a current wavefield snapshot.
void browseVtiWavefieldPath3D(Seis3D_State& s);
// Select an existing destination folder using the Windows Unicode folder picker.
void browseExportFolder3D(Seis3D_State& s);
// Select a directory and shared basename for the three SEG-Y components and geometry manifest.
void browseGatherDestination3D(Seis3D_State& s);
// Reject empty names, path traversal and Windows filename separators before any export starts.
std::string validatedExportPrefix3D(const char* prefix);
// Convert CUDA failures into a sandbox-local exception.
void requireCuda3D(cudaError_t result,const char* operation);
// Create a unique acquisition directory suffix.
std::string sessionSuffix3D();
// Load deterministic material/geometry defaults while preserving grid and boundary choices.
void applyModelPreset3D(SimulationContext3D& context,ModelKind3D kind);
// Describe the current synthetic model for the parameter panel.
const char* modelDescription3D(ModelKind3D kind);
// Allocate private rendering resources after the physical model is ready.
void initializeGraphics3D(Seis3D_State& s);
// Refresh scalar data as needed and render the current transfer functions.
void updateGraphics3D(Seis3D_State& s);
// Draw one orthogonal slice using its independent normalized zoom/pan camera.
void drawSlicePanel3D(Seis3D_State& s,int plane);
// Fit all slice cameras without changing physical slice positions or the solver.
void resetSliceViews3D(Seis3D_State& s);
// Switch only wave/material palettes, retaining all tuned display parameters and view choices.
void applyVisualPreset3D(Seis3D_State& s,int preset);
// Explicitly restore display parameters and palettes to the selected preset's defaults.
void resetDisplayParameters3D(Seis3D_State& s);
// Freeze the effective scale when leaving automatic mode so toggling modes does not change brightness.
void setAutomaticScale3D(Seis3D_State& s,bool automatic);
// Keep velocity components on one reference, preserving separate pressure-unit history and manual scale.
void selectWaveComponent3D(Seis3D_State& s,int component);
// Restore only the volume transfer recipe, preserving physical data and linked slice settings.
void applyVolumeLook3D(Seis3D_State& s,int look);
// Clear fields and live monitor samples before starting a new source wavelet.
void restartSimulation3D(Seis3D_State& s,bool running);
// Apply only pending frequency and its CPML profiles; preserve model, survey, captured records and display settings.
void applySourceFrequency3D(Seis3D_State& s);
// Pause/resume normally, or restart a completed run so the source is emitted again.
void toggleSimulation3D(Seis3D_State& s);
// Append a single, bounded monitor frame from the deployed receiver mesh.
void appendLiveTrace3D(Seis3D_State& s);
struct ReceiverSelection3D {
    std::vector<int> indices3D;
    int rowCount3D=0, row3D=0;
    Receiver3D anchor3D;
};
// Apply centered sliding-window RMS compensation to preview values only.
void compensateTraceAgc3D(std::vector<double>& values,const std::vector<double>& times,const TraceAgcSettings3D& settings);
// Select actual deployed/frozen coordinates, independently of pending survey edits.
ReceiverSelection3D selectReceiverRow3D(const std::vector<Receiver3D>& receivers,int mode,int row,int selected=0);
// Preserve bucket extrema in a bounded preview, using captured timestamps or chronological live ring rows.
TraceSeries3D buildTraceSeries3D(const Seis3D_State& s,bool captured,int receiver,int component,bool imported=false);
// Read a bounded, fixed-length, single-shot SEG-Y into independent CPU data with transactional replacement.
ImportedShot3D readShotSegy3D(const std::filesystem::path& path);
// Report import errors without stopping a running simulation or replacing an existing valid shot.
bool importShotRecord3D(Seis3D_State& s,const std::filesystem::path& path);
// Offer import controls even before any GPU model is initialized.
void drawShotImportControls3D(Seis3D_State& s);
// Draw a styled live monitor or a frozen, exportable receiver gather.
void drawTracePreview3D(Seis3D_State& s);
// Replace the simulation using validated procedural or imported material data.
void initializeSession3D(Seis3D_State& s,bool imported);
// Inspect all imported cells before replacing live GPU resources.
SimulationContext3D inspectModel3D(const std::filesystem::path& path,SimulationContext3D context);
// Stream a validated model into the newly allocated GPU session.
void importModel3D(Seis3D_State& s,const std::filesystem::path& path);
// Stream the current model to a new output file.
void exportModel3D(Seis3D_State& s);
// Stream initialized physical materials as VTK ImageData point arrays, excluding CPML and ghosts.
void exportVtiModel3D(Seis3D_State& s);
// Stream the currently selected physical wavefield component and simulation time as VTK point data.
void exportVtiWavefield3D(Seis3D_State& s);
// Fit receiver and source spreads to the current model dimensions.
void fitSurvey3D(Seis3D_State& s);
// Validate and upload the selected receiver geometry.
void configureReceivers3D(Seis3D_State& s);
// Freeze geometry and start a new shot gather.
int receiverSamplingStride3D(const Seis3D_State& s,double dt);
int receiverSamplingIntervalUs3D(const Seis3D_State& s,double dt);
void appendGatherSamples3D(Seis3D_State& s);
void startGather3D(Seis3D_State& s,int shotId);
// Export the frozen three-component gather and geometry.
void exportGather3D(Seis3D_State& s,const std::filesystem::path& directory);
// Select the next validated source in the shot spread.
void positionSequenceShot3D(Seis3D_State& s);
// Advance a bounded simulation batch and sample receivers at the requested cadence.
void advanceSimulation3D(Seis3D_State& s);
// Keep errors from UI operations local and retain captured data for retrying a failed export.
template<class Action>
void performAction3D(Seis3D_State& s, Action action) {
    const auto previousMessage=s.message3D;
    try { action(); if(s.message3D!=previousMessage) s.messageIsError3D=false; }
    catch (const std::exception& e) {
        s.running3D=false; s.sequence3D=false; s.recording3D=false;
        s.message3D=e.what(); s.messageIsError3D=true;
    }
}


} // namespace Seis3D::Detail3D
