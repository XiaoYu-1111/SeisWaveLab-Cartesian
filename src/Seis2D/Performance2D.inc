// Benchmarks own their fields; the live session and captured traces remain untouched.
struct Benchmark2D {
 GPUSimData cpu,gpu;SimulationContext context;int step=0,target=24;bool running=false;
 double cpuMs=0,gpuMs=0,relativeError=0;std::string status;
 void clear() {releaseGpuSimulation(cpu);releaseGpuSimulation(gpu);running=false;}
} benchmark2D;
inline void drawPerformanceControls2D(SimState& state) {
 if(parameterGroup2D("Compute backend",false)) {
  ImGui::BeginDisabled(acquisition2D.isRecording||benchmark2D.running);
  AppUi::Widgets::Combo("Backend",&selectedBackend2D,"Auto (CUDA / CPU fallback)\0CPU / OpenMP\0CUDA\0");
  AppUi::Widgets::SliderInt("CPU workers",&workerCount2D,1,cpuWorkerLimit2D());
  if(AppUi::Widgets::Button("Apply backend")) {state.running=false;resetSimRequested2D=true;}
  ImGui::EndDisabled();
  AppUi::Widgets::Text("%s: %s",AppUi::text("Current"),gpuData2D.cpu?"CPU / OpenMP":"CUDA");
  if(!backendStatus2D.empty()) AppUi::Widgets::TextWrapped("%s",backendStatus2D.c_str());
 }
 if(parameterGroup2D("Time step cost",false)) {
  AppUi::Widgets::Text("%.3f ms / step | %.1f steps/s",stepMilliseconds2D,stepsPerSecond2D);
  AppUi::Widgets::Text("%.2f M cells/s",stepsPerSecond2D*gpuData2D.total_grid/1e6);
  AppUi::Widgets::TextWrapped("Solver wall time includes CUDA synchronization; display and file export are excluded.");
 }
 if(parameterGroup2D("CPU / CUDA efficiency comparison",false)) {
  AppUi::Widgets::TextWrapped("Compare identical models, sources and time steps in isolated fields. Lower time is faster; relative L2 checks the resulting Vz wavefield.");
  ImGui::BeginDisabled(benchmark2D.running||acquisition2D.isRecording||state.running);
  if(AppUi::Widgets::Button("Run CPU / CUDA comparison")) {
   benchmark2D.clear();benchmark2D.context=simulationContext2D;
   try {
    if(!getCudaDeviceInfo().success) throw std::runtime_error("CUDA unavailable: comparison requires a supported CUDA device.");
    initializeGpuSimulation(benchmark2D.cpu,benchmark2D.context,true);benchmark2D.cpu.workers=workerCount2D;
    initializeGpuSimulation(benchmark2D.gpu,benchmark2D.context,false);
    benchmark2D.step=0;benchmark2D.cpuMs=benchmark2D.gpuMs=0;benchmark2D.running=true;
    benchmark2D.status="Running comparison...";
   } catch(const std::exception& e) {benchmark2D.clear();benchmark2D.status=e.what();}
  }
  ImGui::EndDisabled();
  if(benchmark2D.running) {
   try {
    auto start=std::chrono::steady_clock::now();
    runGpuSimulationStep(benchmark2D.cpu,benchmark2D.step,benchmark2D.context,-1);
    double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    start=std::chrono::steady_clock::now();
    runGpuSimulationStep(benchmark2D.gpu,benchmark2D.step,benchmark2D.context,-1);CUDA_CHECK(cudaDeviceSynchronize());
    double gpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    // Four warm-up steps are excluded from throughput measurements.
    if(benchmark2D.step>=4) {benchmark2D.cpuMs+=cpu;benchmark2D.gpuMs+=gpu;}
    ++benchmark2D.step;
    if(benchmark2D.step==benchmark2D.target) {
     std::vector<float> cpuField(benchmark2D.context.total_grid),gpuField(cpuField.size());
     copyWavefieldToHost(benchmark2D.cpu,cpuField.data());copyWavefieldToHost(benchmark2D.gpu,gpuField.data());
     double diff=0,norm=0;for(size_t k=0;k<cpuField.size();++k) {diff+=std::pow(double(cpuField[k])-gpuField[k],2);norm+=double(cpuField[k])*cpuField[k];}
     benchmark2D.relativeError=std::sqrt(diff/std::max(norm,1e-60));benchmark2D.clear();benchmark2D.status="Comparison complete";
    }
   } catch(const std::exception& e) {benchmark2D.clear();benchmark2D.status=e.what();}
   ImGui::ProgressBar(float(benchmark2D.step)/benchmark2D.target,ImVec2(-1,0));
   if(AppUi::Widgets::Button("Cancel comparison")) {benchmark2D.clear();benchmark2D.status="Cancelled";}
  }
  if(!benchmark2D.status.empty()) AppUi::Widgets::TextWrapped("%s",benchmark2D.status.c_str());
  if(benchmark2D.step==benchmark2D.target) {
   AppUi::Widgets::Text("CPU %.3f ms | CUDA %.3f ms",benchmark2D.cpuMs/20,benchmark2D.gpuMs/20);
   AppUi::Widgets::Text("CPU / CUDA %.2fx | relative L2 %.3e",benchmark2D.cpuMs/std::max(benchmark2D.gpuMs,1e-9),benchmark2D.relativeError);
  }
 }
}
