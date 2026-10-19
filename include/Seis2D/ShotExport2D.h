#pragma once
#include "SeismicIO.h"
#include <filesystem>

namespace Seis2D {
// Freeze solver inputs with a shot; presentation controls never enter its physical metadata.
struct FrozenModel2D {
    int nx=0,nz=0,npml=0,formulation=0,sourceX=0,sourceZ=0,sourceType=0,captureStartStep=0,samplingStride=1;
    bool freeSurface=false,multipleSources=false,externalPml=false;
    float spacing=0,solverDt=0,frequency=0,delay=0,sourceAmplitude=0,sourceAngle=0;
    std::vector<float> lambda,mu,rho,wavelet;
};
struct ShotExportRequest2D {
    std::filesystem::path directory;
    std::string prefix="seis2d_shot",version,device;
    int shot=1,samples=0;
    float sampleInterval=0;
    bool includeCsv=true,includeModel=true;
};
// Reject unsafe basename components before creating directories or replacing files.
void validateExportPrefix2D(const std::string& prefix);
// Validate all inputs, write in an owned staging directory, and publish a new complete shot folder.
std::filesystem::path exportShotPackage2D(const ShotExportRequest2D& request,const FrozenModel2D& model,
    const std::vector<std::vector<float>>& vx,const std::vector<std::vector<float>>& vz,
    const std::vector<SeismicIO::TraceMetadata>& geometry);
}
