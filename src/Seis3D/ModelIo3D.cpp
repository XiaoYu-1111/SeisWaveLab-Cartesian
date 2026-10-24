#include "Internal3D.h"
#include "VtiModelIO.h"
#include "ResourcePaths.h"
#include "ModelImportTask3D.h"
#include <cstdlib>
#include <utility>
#include <stdexcept>
#include <vector>

namespace Seis3D {
ModelImportTask3D::~ModelImportTask3D() {
    cancel.store(true);
    if(worker.valid()) worker.wait();
    releaseGpu3D(gpu);
}
}
namespace Seis3D::Detail3D {
static SimulationContext3D vtiContext3D(const SeisCommon::VtiModelReader& reader,SimulationContext3D c) {
    const auto& g=reader.grid();
    for(int k=0;k<3;++k) if(g.origin[k]!=0||std::abs(g.spacing[k]-g.spacing[0])>g.spacing[0]*1e-7)
        throw std::runtime_error("VTI model requires origin (0,0,0) and equal XYZ spacing.");
    c.nx3D=g.nx;c.ny3D=g.ny;c.nz3D=g.nz;c.spacing3D=float(g.spacing[0]);c.maxVp3D=reader.maximumVp();c.modelKind3D=ModelKind3D::Imported;
    if(auto p=g.metadata.find("CPMLThickness");p!=g.metadata.end()) c.pml3D=p->second;
    if(auto p=g.metadata.find("BoundaryMode");p!=g.metadata.end()) {
        if(p->second>2) throw std::runtime_error("Invalid VTI boundary mode.");
        c.freeSurface3D=p->second==1;c.reflecting3D=p->second==2;
    }
    validateContext3D(c);
    if(estimateMemory3D(c).totalBytes3D>memoryLimit3D) throw std::runtime_error("Imported VTI exceeds the 4 GiB sandbox budget.");
    return c;
}

void beginModelImport3D(Seis3D_State& s) {
    if(s.modelImport3D) throw std::logic_error("A VTI model is already being imported.");
    if(s.recording3D||s.sequence3D) throw std::logic_error("Stop acquisition before importing a model.");
    auto task=std::make_unique<ModelImportTask3D>();
    task->context=s.editContext3D;task->backend=s.backend3D;task->workers=s.workers3D;
    task->path=uiPath3D(s.modelPath3D);if(task->path.is_relative()) task->path=getExecutableDirectory()/task->path;
    int devices=0;task->cudaAvailable=cudaGetDeviceCount(&devices)==cudaSuccess&&devices>0&&std::getenv("SEISWAVE_FORCE_CPU")==nullptr;
    if(task->cudaAvailable) requireCuda3D(cudaGetDevice(&task->device),"query import device");
    auto* job=task.get();
    job->worker=std::async(std::launch::async,[job] {
        SeisCommon::VtiModelReader reader(job->path,36000000,[job](float progress) {
            if(job->cancel.load()) throw std::runtime_error("Model import cancelled.");
            job->stage.store(progress<.8f?0:1);job->progress.store(.65f*progress);
        });
        job->context=vtiContext3D(reader,job->context);
        const auto& c=job->context;const auto plane=std::size_t(c.nx3D)*c.ny3D*3;
        job->materials.resize(plane*c.nz3D);
        for(int z=0;z<c.nz3D;++z) reader.readPlane(z,job->materials.data()+std::size_t(z)*plane);
    });
    s.running3D=false;s.singleStep3D=false;s.modelImport3D=std::move(task);
    s.message3D="Importing VTI model...";s.messageIsError3D=false;
}

static void prepareImportBackend3D(ModelImportTask3D& job) {
    job.phase=1;job.stage.store(2);job.progress.store(job.forceCpu?.97f:.68f);
    job.worker=std::async(std::launch::async,[&job] {
        if(job.cancel.load()) throw std::runtime_error("Model import cancelled.");
        bool cpu=job.forceCpu||job.backend==1||!job.cudaAvailable;
        if(!cpu) requireCuda3D(cudaSetDevice(job.device),"select import device");
        if(!job.cudaAvailable) job.status="CUDA unavailable: CPU / OpenMP fallback";
        job.gpu.workers3D=job.workers;
        try {initializeGpu3D(job.gpu,job.context,cpu);}
        catch(const std::exception& error) {
            if(cpu) throw;
            job.status=std::string("CUDA initialization failed; CPU fallback: ")+error.what();
            job.gpu.workers3D=job.workers;
            initializeGpu3D(job.gpu,job.context,true);
        }
        job.gpu.workers3D=job.workers;job.stage.store(3);job.progress.store(job.forceCpu?.97f:.75f);
        const auto& c=job.context;const auto plane=std::size_t(c.nx3D)*c.ny3D*3;
        for(int z=0;z<c.nz3D;++z) {
            if(job.cancel.load()) throw std::runtime_error("Model import cancelled.");
            uploadModelPlane3D(job.gpu,c,z,job.materials.data()+std::size_t(z)*plane);
            job.progress.store(job.forceCpu?.97f+.02f*float(z+1)/c.nz3D:.75f+.2f*float(z+1)/c.nz3D);
        }
        finishModelUpload3D(job.gpu,c);
    });
}

void cancelModelImport3D(Seis3D_State& s) {if(s.modelImport3D) s.modelImport3D->cancel.store(true);}
ModelImportView3D modelImportView3D(const Seis3D_State& s) {
    ModelImportView3D view;
    if(const auto* job=s.modelImport3D.get()) {
        view.progress=job->progress.load();view.stage=job->stage.load();view.cancelling=job->cancel.load();
        view.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-job->started).count();
        view.filename=pathToUtf8(job->path.filename());
    }
    return view;
}

void pollModelImport3D(Seis3D_State& s) {
    if(!s.modelImport3D) return;
    auto& job=*s.modelImport3D;
    try {
        if(job.phase<2) {
            if(job.worker.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
            job.worker.get();
            if(job.cancel.load()) throw std::runtime_error("Model import cancelled.");
            if(job.phase==0) {
                // Release the old compute buffers only after full validation; avoid doubling VRAM use.
                releaseSandbox3D(s);prepareImportBackend3D(job);return;
            }
            job.phase=2;job.stage.store(4);job.progress.store(job.forceCpu?.995f:.97f);return;
        }
        s.context3D=job.context;s.editContext3D=job.context;
        s.gpu3D=std::exchange(job.gpu,GPUSimData3D{});s.backendStatus3D=job.status;
        try {initializeGraphics3D(s);}
        catch(const std::exception& error) {
            const bool fallback=!s.gpu3D.cpu3D&&std::string(error.what()).find("register Seis3D staging")!=std::string::npos;
            releaseSandbox3D(s);
            if(!fallback) throw;
            job.forceCpu=true;job.status="CUDA/OpenGL interop unavailable: CPU / OpenMP fallback";
            prepareImportBackend3D(job);return;
        }
        try {completeSession3D(s,job.context);}
        catch(...) {releaseSandbox3D(s);throw;}
        s.message3D="VTI model imported successfully.";s.messageIsError3D=false;
        s.modelImport3D.reset();
    } catch(const std::exception& error) {
        const bool cancelled=job.cancel.load();
        s.message3D=cancelled?"Model import cancelled.":error.what();s.messageIsError3D=!cancelled;
        s.modelImport3D.reset();
    }
}
// Validate every coordinate and physical material before replacing the live session.
SimulationContext3D inspectModel3D(const std::filesystem::path& path,SimulationContext3D c) {
    return vtiContext3D(SeisCommon::VtiModelReader(path),c);
}

// Upload only physical cells; the solver rebuilds its own CPML/ghost storage.
void importModel3D(Seis3D_State& s,const std::filesystem::path& path) {
    SeisCommon::VtiModelReader reader(path);
    const auto c=vtiContext3D(reader,s.context3D);
    const auto& live=s.context3D;
    if(c.nx3D!=live.nx3D||c.ny3D!=live.ny3D||c.nz3D!=live.nz3D||c.spacing3D!=live.spacing3D
        ||c.maxVp3D!=live.maxVp3D||c.pml3D!=live.pml3D||c.freeSurface3D!=live.freeSurface3D||c.reflecting3D!=live.reflecting3D)
        throw std::runtime_error("VTI model changed during import; retry with a stable file.");
    std::vector<float> plane(std::size_t(c.nx3D)*c.ny3D*3);
    for(int z=0;z<c.nz3D;++z) {reader.readPlane(z,plane.data());uploadModelPlane3D(s.gpu3D,live,z,plane.data());}
    finishModelUpload3D(s.gpu3D,live);
}

void exportModel3D(Seis3D_State& s) {
    if(!s.ready3D) throw std::runtime_error("Initialize the 3D model before exporting VTI.");
    if(s.recording3D||s.sequence3D) throw std::runtime_error("Stop acquisition before exporting VTI.");
    const auto path=uiPath3D(s.modelExportPath3D);
    const auto& c=s.context3D;SeisCommon::ModelGrid grid;
    grid.nx=c.nx3D;grid.ny=c.ny3D;grid.nz=c.nz3D;grid.spacing={c.spacing3D,c.spacing3D,c.spacing3D};
    grid.metadata={{"Dimension",3},{"BoundaryMode",c.reflecting3D?2:c.freeSurface3D?1:0},{"CPMLThickness",c.pml3D}};
    SeisCommon::writeVtiModel(path,grid,[&](int z,float* plane){downloadModelPlane3D(s.gpu3D,s.context3D,z,plane);});
    s.message3D="VTI 3D model exported: "+pathToUtf8(path);
}
}
