// Single-owner 2D state. No GPU allocation occurs before entering the workspace.
static SimulationContext simulationContext2D;
static GPUSimData        gpuData2D;
static bool              initialized2D = false;

inline bool resetSimRequested2D = false;
inline bool resetViewportRequested2D = false;

inline int   gridWidth2D = 1000;
inline int   gridHeight2D = 500;
inline float sourceFrequency2D = 100.0f;
inline float sourceDelay2D = 1.0f / sourceFrequency2D;
inline float appliedSourceFrequency2D=100.0f,appliedSourceDelay2D=.01f;

inline float tempDt2D = 0.0002f;
inline float tempDx2D = 1.0f;
inline int   tempNt2D = 10000;
inline int   tempPml2D = 50;

inline int   currentIt2D = 0;
inline float elapsedRunWallTime2D = 0.0f;
inline float colorScale2D = 100.0f;
inline int   showComponent2D = 2;     // 0: velocity magnitude, 1: Vx, 2: Vz.
inline int   waveStyle2D = 1;
inline int   modelStyle2D = 4;
inline int   stepsPerFrame2D = 20;
inline bool  showHUD2D = true;
inline bool  showMonitorPar2D = false;

inline int   editSrcX2D = 500;
inline int   editSrcZ2D = 125;
inline float editAngle2D = 0.0f;
inline float editAmp2D = 1.0f;
inline int   editSourceType2D = 0;

inline float      viewZoom2D = 1.0f;
inline ImVec2     viewOffset2D = { 0.0f, 0.0f };
inline bool       firstAlignNeeded2D = false;
inline Parameters parameters2D;

inline bool                  multiSourceMode2D = true;
inline std::vector<GPUSource> activeSources2D;
inline bool                  infiniteMode2D = false;

inline int   hoverMx2D = 0;
inline int   hoverMy2D = 0;
inline float hoverVp2D = 0.0f;
inline float hoverVs2D = 0.0f;
inline float hoverRho2D = 0.0f;
inline bool  hoverValid2D = false;

inline ImVec4 uiAccent2D = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);

inline bool isMouseInside2D = false;

inline int currentScene2D = SCENE_TWO_LAYER;

inline float editVp2D = 2000.0f;
inline float editVs2D = 1400.0f;
inline float editDensity2D = 2000.0f;

inline GLuint modelTex2D = 0;

inline bool  showGrid2D = true;
inline float uiGridSpacing2D = 10.0f;
inline float uiGridOpacity2D = 1.0f;

inline static bool        showSuccessPopup2D = false;
inline static bool        showErrorPopup2D = false;
inline static std::string popupMessage2D = "";

inline float cachedMaxVp2D = 0.0f;
inline float cachedMinVp2D = 0.0f;
inline float cachedMaxVs2D = 0.0f;
inline float cachedMinVs2D = 0.0f;
inline float cachedMaxRho2D = 0.0f;
inline float cachedMinRho2D = 0.0f;
inline float cachedVramMb2D = 0.0f;

inline bool convertRho2D=true;

inline float editSegyDx2D = 1.0f;
inline float exportTargetDx2D = 1.0f;

inline ReplayManager replay2D;

struct ReceiverPosition2D {
    int x;
    int y;
};

struct AcquisitionState2D {
    float samplingRateHz = 10000.0f;
    float totalDurationSec = 0.5f;
    int   numChannels = 100;
    int   startX = 100;
    int   endX = 900;
    int   receiverDepth = 150;

    std::vector<ReceiverPosition2D> receivers;
    bool isShowarray = true;
    bool isRecording = false;

    float totalTimer = 0.0f;
    int   currentFileIndex = 1;
    int samplingStepStride = 1;
    int capturedSamples = 0;
    int captureStartStep = 0;
    int capturedSourceX=0,capturedSourceZ=0;
    float capturedGridSpacing=1;
    std::vector<ReceiverPosition2D> capturedReceivers;
    float actualSampleInterval = 0.0f;
    FrozenModel2D frozenModel;

    std::vector<std::vector<float>> recorded_vx;
    std::vector<std::vector<float>> recorded_vz;

    // Start a new local recording timeline and reserve bounded two-component host buffers.
    void initBuffer() {
        if (numChannels <= 0 || !std::isfinite(totalDurationSec) || totalDurationSec <= 0.0f) {
            throw std::invalid_argument("Recording requires positive duration and channel count");
        }
        const int newStride = getReceiverSamplingStride(simulationContext2D.dt, samplingRateHz);
        const float newInterval = newStride * simulationContext2D.dt;
        const double sampleCount = std::ceil(totalDurationSec / static_cast<double>(newInterval));
        if (!std::isfinite(sampleCount) || sampleCount > (std::numeric_limits<int>::max)()) {
            throw std::overflow_error("Recording sample count is too large");
        }
        int expectedPoints = std::max(1, static_cast<int>(sampleCount));
        if (std::uint64_t(expectedPoints)*numChannels*2*sizeof(float)>512ull*1024*1024)
            throw std::invalid_argument("2D capture exceeds the 512 MiB host budget.");
        FrozenModel2D candidate;
        auto frozenReceivers=receivers;
        const int sourceX=simulationContext2D.src_idx%simulationContext2D.NX;
        const int requestedZ=simulationContext2D.src_idx/simulationContext2D.NX;
        const int sourceZ=simulationContext2D.flag_type==3&&simulationContext2D.upFlag?std::max(requestedZ,1):requestedZ;
        candidate.nx=simulationContext2D.NX;candidate.nz=simulationContext2D.NZ;
        candidate.spacing=simulationContext2D.h;candidate.solverDt=simulationContext2D.dt;
        candidate.externalPml=simulationContext2D.externalPml;
        candidate.npml=simulationContext2D.npml;candidate.formulation=simulationContext2D.flag_type;
        candidate.freeSurface=simulationContext2D.flag_type==3&&simulationContext2D.upFlag;
        candidate.sourceX=sourceX;candidate.sourceZ=sourceZ;
        candidate.sourceType=simulationContext2D.src_type;candidate.sourceAmplitude=simulationContext2D.src_amp;
        candidate.sourceAngle=simulationContext2D.src_angle;candidate.frequency=appliedSourceFrequency2D;candidate.delay=appliedSourceDelay2D;
        candidate.captureStartStep=currentIt2D;candidate.samplingStride=newStride;candidate.multipleSources=multiSourceMode2D;
        if(std::uint64_t(simulationContext2D.total_grid)*12>256ull*1024*1024)
            throw std::invalid_argument("2D frozen model exceeds the 256 MiB capture budget.");
        candidate.lambda=simulationContext2D.lambda;candidate.mu=simulationContext2D.mu;
        candidate.rho=simulationContext2D.rho;candidate.wavelet=simulationContext2D.wavelet;
        std::vector<std::vector<float>> newVx(numChannels,std::vector<float>(expectedPoints,0));
        std::vector<std::vector<float>> newVz(numChannels,std::vector<float>(expectedPoints,0));
        recorded_vx.swap(newVx);recorded_vz.swap(newVz);frozenModel=std::move(candidate);
        capturedReceivers=std::move(frozenReceivers);capturedSamples=0;captureStartStep=currentIt2D;
        capturedSourceX=sourceX;capturedSourceZ=sourceZ;capturedGridSpacing=simulationContext2D.h;
        samplingStepStride=newStride;actualSampleInterval=newInterval;

    }
};

inline AcquisitionState2D acquisition2D;

enum ShotSequenceState2D {
    AUTO_ACQ_IDLE = 0,
    AUTO_ACQ_RUNNING,
    AUTO_ACQ_FINISHED
};

struct ShotSequence2D {
    ShotSequenceState2D state = AUTO_ACQ_IDLE;
    int          startX = 100;
    int          shotDepthZ = 60;
    int          shotIntervalX = 60;
    int          totalShots = 10;
    int          currentShotIdx = 0;

    // Prepare the first shot and extend the run length to cover its requested recording duration.
    void start() {
        if (totalShots <= 0) return;
        currentShotIdx = 0;

        editSrcX2D = startX;
        editSrcZ2D = shotDepthZ;

        const double requested=std::ceil(double(acquisition2D.totalDurationSec)/simulationContext2D.dt);
        if(!std::isfinite(requested)||requested<1||requested>50000)
            throw std::invalid_argument("Automatic shots require 1-50000 solver steps; reduce duration or use a safe larger dt.");
        const int required_steps=int(requested);
        state = AUTO_ACQ_RUNNING;
        if (required_steps > 0) {
            simulationContext2D.nt = required_steps;
            tempNt2D = required_steps;

            generateRickerWavelet(simulationContext2D.wavelet, simulationContext2D.nt, simulationContext2D.dt, sourceFrequency2D, sourceDelay2D);
        }
    }
};

inline ShotSequence2D autoAcquisition2D;

typedef int nvmlReturn_t;
#define NVML_SUCCESS 0
#define NVML_TEMPERATURE_GPU 0

typedef struct {
    unsigned int gpu;
    unsigned int memory;
} nvmlUtilization_t;

typedef void* nvmlDevice_t;

typedef nvmlReturn_t(*pfn_nvmlInit)();
typedef nvmlReturn_t(*pfn_nvmlShutdown)();
typedef nvmlReturn_t(*pfn_nvmlDeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*);
typedef nvmlReturn_t(*pfn_nvmlDeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t*);
typedef nvmlReturn_t(*pfn_nvmlDeviceGetTemperature)(nvmlDevice_t, int, unsigned int*);
typedef nvmlReturn_t(*pfn_nvmlDeviceGetPowerUsage)(nvmlDevice_t, unsigned int*);

struct DeviceMonitor2D {
    bool loaded = false;
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif

    pfn_nvmlInit                     init = nullptr;
    pfn_nvmlShutdown                 shutdown = nullptr;
    pfn_nvmlDeviceGetHandleByIndex   getHandle = nullptr;
    pfn_nvmlDeviceGetUtilizationRates getUtil = nullptr;
    pfn_nvmlDeviceGetTemperature     getTemp = nullptr;
    pfn_nvmlDeviceGetPowerUsage      getPower = nullptr;

    nvmlDevice_t devHandle = nullptr;

    // Resolve optional telemetry once; an unavailable device must not leak library handles.
    void load() {
        if (handle) return;
#ifdef _WIN32
        handle = LoadLibraryA("nvml.dll");
#else
        handle = dlopen("libnvidia-ml.so", RTLD_LAZY);
#endif
        if (!handle) return;

#ifdef _WIN32
        init = (pfn_nvmlInit)GetProcAddress(handle, "nvmlInit");
        shutdown = (pfn_nvmlShutdown)GetProcAddress(handle, "nvmlShutdown");
        getHandle = (pfn_nvmlDeviceGetHandleByIndex)GetProcAddress(handle, "nvmlDeviceGetHandleByIndex");
        getUtil = (pfn_nvmlDeviceGetUtilizationRates)GetProcAddress(handle, "nvmlDeviceGetUtilizationRates");
        getTemp = (pfn_nvmlDeviceGetTemperature)GetProcAddress(handle, "nvmlDeviceGetTemperature");
        getPower = (pfn_nvmlDeviceGetPowerUsage)GetProcAddress(handle, "nvmlDeviceGetPowerUsage");
#else
        init = (pfn_nvmlInit)dlsym(handle, "nvmlInit");
        shutdown = (pfn_nvmlShutdown)dlsym(handle, "nvmlShutdown");
        getHandle = (pfn_nvmlDeviceGetHandleByIndex)dlsym(handle, "nvmlDeviceGetHandleByIndex");
        getUtil = (pfn_nvmlDeviceGetUtilizationRates)dlsym(handle, "nvmlDeviceGetUtilizationRates");
        getTemp = (pfn_nvmlDeviceGetTemperature)dlsym(handle, "nvmlDeviceGetTemperature");
        getPower = (pfn_nvmlDeviceGetPowerUsage)dlsym(handle, "nvmlDeviceGetPowerUsage");
#endif

        if (init && shutdown && getHandle && getUtil && getTemp && getPower) {
            if (init() == NVML_SUCCESS) {
                if (getHandle(0, &devHandle) == NVML_SUCCESS) {
                    loaded = true;
                }
                else {
                    shutdown();
                }
            }
        }
    }

    // Release the library even when symbol resolution or device initialization failed.
    void unload() {
        if (!handle) return;
        if (loaded && shutdown) shutdown();
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        loaded = false;
        handle = nullptr;devHandle = nullptr;
    }
};

inline DeviceMonitor2D deviceMonitor2D;

ViewerState2D recordViewer2D;

inline bool showBoundary2D=false;
inline int domainPad2D() {return simulationContext2D.externalPml?std::max(simulationContext2D.npml,kStencilRadius):0;}
inline int viewWidth2D() {return simulationContext2D.NX-1+(showBoundary2D?2*domainPad2D():0);}
inline bool freeTop2D() {return simulationContext2D.flag_type==3&&simulationContext2D.upFlag;}
inline int viewHeight2D() {return simulationContext2D.NZ-1+(showBoundary2D?(freeTop2D()?domainPad2D():2*domainPad2D()):0);}
inline int viewOriginZ2D() {return showBoundary2D&&!freeTop2D()?-domainPad2D():0;}
inline int viewOrigin2D() {return showBoundary2D?-domainPad2D():0;}
inline int selectedBackend2D=0; // Auto, CPU/OpenMP, CUDA.
inline int workerCount2D=std::max(1,std::min(8,cpuWorkerLimit2D()));
inline double stepMilliseconds2D=0,stepsPerSecond2D=0;
inline std::string backendStatus2D;
inline void initializeExternalSimulation2D(GPUSimData& gpu,SimulationContext& context) {
    context.externalPml=true;
    const bool available=getCudaDeviceInfo().success&&std::getenv("SEISWAVE_FORCE_CPU")==nullptr;
    const bool cpu=selectedBackend2D==1||!available;
    backendStatus2D=!available?"CUDA unavailable: CPU / OpenMP fallback":"";
    try {::initializeGpuSimulation(gpu,context,cpu);}
    catch(const std::exception& e) {
        if(cpu) throw;
        backendStatus2D=std::string("CUDA initialization failed; CPU fallback: ")+e.what();
        ::initializeGpuSimulation(gpu,context,true);
    }
    gpu.workers=workerCount2D;
}

inline bool showWave2D=true,showMaterial2D=true;
inline float waveOpacity2D=1.0f;

inline bool panActive2D=false,showModelMesh2D=true;
inline float meshOpacity2D=.15f;

inline double lastInjectTime2D=-1.0;

inline bool automaticWaveScale2D=true;
inline float currentWaveReference2D=1.0f;
