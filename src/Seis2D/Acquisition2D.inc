// Private implementation fragment, included once by Simulation2D.cpp.
// Validate each receiver against the current grid before replacing GPU indices.
inline void syncReceiversToGpu() {
    std::vector<int> receiverIndices;
    receiverIndices.reserve(acquisition2D.receivers.size());
    for (const ReceiverPosition2D& receiver : acquisition2D.receivers) {
        if (receiver.x < 0 || receiver.x >= simulationContext2D.NX || receiver.y < 0 || receiver.y >= simulationContext2D.NZ) {
            throw std::invalid_argument("Receiver coordinates are outside the model; redeploy the receiver array");
        }
        receiverIndices.push_back(receiver.y * simulationContext2D.NX + receiver.x);
    }
    updateGpuReceivers(gpuData2D, receiverIndices);
    simulationContext2D.rcv_grid_idx = std::move(receiverIndices);
    simulationContext2D.num_rcv = static_cast<int>(simulationContext2D.rcv_grid_idx.size());
    acquisition2D.numChannels = simulationContext2D.num_rcv;
}

// Initializes and advances one single-shot simulation using the current setup.
inline void triggerSingleShot(GLHandles& gl, SimState& state) {
    multiSourceMode2D=false;
    if(freeTop2D()) editSrcZ2D=std::max(1,editSrcZ2D);
    acquisition2D.isRecording=false;
    acquisition2D.captureStartStep=0;

    simulationContext2D.src_z_idx = editSrcZ2D;
    simulationContext2D.src_idx = editSrcZ2D * simulationContext2D.NX + editSrcX2D;
    simulationContext2D.src_angle = editAngle2D;
    simulationContext2D.src_type = editSourceType2D;
    simulationContext2D.src_amp = editAmp2D;

    generateRickerWavelet(simulationContext2D.wavelet, simulationContext2D.nt, simulationContext2D.dt, sourceFrequency2D, sourceDelay2D);

    currentIt2D = 0;
    state.running = true;
    elapsedRunWallTime2D = 0.0f;
    activeSources2D.clear();

    releaseGpuSimulation(gpuData2D);
    initializeExternalSimulation2D(gpuData2D, simulationContext2D);

    syncReceiversToGpu();
    // Freeze shot geometry at emission, independently from editable controls used for the next shot.
    if(acquisition2D.capturedSamples==0) {
        acquisition2D.capturedSourceX=editSrcX2D;acquisition2D.capturedSourceZ=
            simulationContext2D.flag_type==3&&simulationContext2D.upFlag?std::max(editSrcZ2D,1):editSrcZ2D;
        acquisition2D.capturedGridSpacing=simulationContext2D.h;acquisition2D.capturedReceivers=acquisition2D.receivers;
        auto& frozen=acquisition2D.frozenModel;
        frozen.sourceX=editSrcX2D;frozen.sourceZ=acquisition2D.capturedSourceZ;frozen.sourceType=editSourceType2D;
        frozen.sourceAmplitude=editAmp2D;frozen.sourceAngle=editAngle2D;frozen.frequency=sourceFrequency2D;
        frozen.delay=sourceDelay2D;frozen.captureStartStep=0;frozen.wavelet=simulationContext2D.wavelet;
    }

    std::fill(simulationContext2D.record_vx.begin(), simulationContext2D.record_vx.end(), 0.0f);
    std::fill(simulationContext2D.record_vz.begin(), simulationContext2D.record_vz.end(), 0.0f);
}

// Export only valid raw samples with frozen physical inputs and millimetre coordinate precision.
inline void exportCapturePackage2D(bool notify=true) {
    const auto& recorder=acquisition2D;
    if(activeExportDirectory2D.empty()) beginExportSession2D(false);
    if(recorder.capturedSamples<1||recorder.capturedReceivers.size()!=recorder.recorded_vz.size())
        throw std::invalid_argument("No valid captured samples or matching frozen receiver geometry.");
    std::vector<SeismicIO::TraceMetadata> geometry(recorder.capturedReceivers.size());
    const auto coordinate=[&](int grid) {
        const double millimetres=double(grid)*recorder.capturedGridSpacing*1000;
        if(!std::isfinite(millimetres)||std::abs(millimetres)>std::numeric_limits<int32_t>::max())
            throw std::invalid_argument("Shot coordinate exceeds SEG-Y millimetre range.");
        return int32_t(std::llround(millimetres));
    };
    for(std::size_t i=0;i<geometry.size();++i) {
        auto& g=geometry[i];g.fieldRecordNum=recorder.currentFileIndex;g.traceInRecord=int(i)+1;g.coordinateScalar=-1000;
        g.sourceX=coordinate(recorder.capturedSourceX);g.sourceY=coordinate(recorder.capturedSourceZ);
        g.groupX=coordinate(recorder.capturedReceivers[i].x);g.groupY=coordinate(recorder.capturedReceivers[i].y);
    }
    ShotExportRequest2D request;request.directory=activeExportDirectory2D;request.prefix=exportPrefix2D;
    request.shot=recorder.currentFileIndex;request.samples=recorder.capturedSamples;request.sampleInterval=recorder.actualSampleInterval;
    request.version=AppUi::productVersion;request.device=exportDevice2D;request.includeCsv=exportCsv2D;request.includeModel=exportModel2D;
    lastExportDirectory2D=exportShotPackage2D(request,recorder.frozenModel,
        capturedTraceCopy2D(recorder.recorded_vx,recorder.capturedSamples),
        capturedTraceCopy2D(recorder.recorded_vz,recorder.capturedSamples),geometry);
    if(notify) {popupMessage2D="Shot package saved: "+pathToUtf8(lastExportDirectory2D);showSuccessPopup2D=true;}
}
