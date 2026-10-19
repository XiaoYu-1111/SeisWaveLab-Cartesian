#pragma once
#include "TraceProcessing.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Seis2D {
struct RecordData2D {
    std::vector<std::vector<float>> traces;
    std::vector<double> firstTimes;
    double sampleInterval=.001;
    std::string name,units="file units";
};
struct ViewerCache2D {
    std::uint64_t revision=0;
    int first=-1,count=0,component=-1;
    Seismic::AgcSettings agc;
    bool normalize=false;
    double peak=0,rawPeak=0,timeMin=0,timeMax=0;
    int heatmapRows=0;
    std::vector<Seismic::TracePreview> previews;
    std::vector<double> heatmap;
};
struct ViewerState2D {
    bool documentationCapture=false;
    std::uint32_t dockingClass=0;
    bool focusRequest=false;
    bool isOpen=false,showWiggle=true,showHeatmap=true,positiveFill=true,normalizeEach=false,autoFit=false,fitRequest=true;
    bool showAllTraces=true,signedFill=false;
    int viewMode=0,component=0,firstTrace=0,visibleTraces=100,selectedTrace=0,palette=1,recordStyle=3;
    float gain=1,heatmapRange=1,densityOpacity=.85f;
    Seismic::AgcSettings agc;
    std::array<RecordData2D,2> records;
    bool dualComponent=false;
    std::uint64_t revision=1;
    ViewerCache2D cache;
    char importPath[1024]{};
    std::string status;
    bool error=false;
};
// Read bounded, fixed-length SEG-Y records, preserving per-trace delay and rejecting invalid payloads.
RecordData2D readRecordSegy2D(const std::filesystem::path& path);
// Replace data transactionally; errors retain the previous record and all display controls.
bool importRecord2D(ViewerState2D& state,const std::filesystem::path& path);
// Refresh only the selected bounded display cache when data, AGC or normalization changes.
void prepareViewerCache2D(ViewerState2D& state);
// Restore display defaults only after an explicit user action; retain loaded raw records.
void resetViewerDisplay2D(ViewerState2D& state);
// Draw an independent themed viewer for external files or snapshots of the current 2D capture.
void renderRecordViewer2D(ViewerState2D& state);
}
