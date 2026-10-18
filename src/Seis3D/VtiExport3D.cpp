#include "Internal3D.h"
#include "ResourcePaths.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>

namespace Seis3D::Detail3D {
namespace {

// Prefix each uncompressed VTK appended array with its explicit little-endian UInt64 byte count.
void writeVtiLength3D(std::ostream& output3D,std::uint64_t bytes3D) {
    unsigned char encoded3D[8]{};
    for(int i3D=0;i3D<8;++i3D) encoded3D[i3D]=static_cast<unsigned char>(bytes3D>>(8*i3D));
    output3D.write(reinterpret_cast<const char*>(encoded3D),sizeof(encoded3D));
}

// Emit portable float32 values without changing the buffers used by CUDA or the other components.
void writeVtiValues3D(std::ostream& output3D,const std::vector<float>& values3D) {
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
    if constexpr(std::endian::native==std::endian::little) {
        output3D.write(reinterpret_cast<const char*>(values3D.data()),std::streamsize(values3D.size()*4));
    } else {
        for(const float value3D:values3D) {
            std::uint32_t bits3D=0;std::memcpy(&bits3D,&value3D,4);
            const unsigned char encoded3D[4]={static_cast<unsigned char>(bits3D),static_cast<unsigned char>(bits3D>>8),
                static_cast<unsigned char>(bits3D>>16),static_cast<unsigned char>(bits3D>>24)};
            output3D.write(reinterpret_cast<const char*>(encoded3D),sizeof(encoded3D));
        }
    }
    if(!output3D) throw std::runtime_error("Cannot write VTI values; check disk space.");
}

// Keep samples within the same physical conditions as S3DM import, catching invalid GPU materials.
void validateVtiPlane3D(const std::vector<float>& plane3D) {
    for(std::size_t i3D=0;i3D<plane3D.size();i3D+=3) {
        const float vp3D=plane3D[i3D],vs3D=plane3D[i3D+1],rho3D=plane3D[i3D+2];
        if(!std::isfinite(vp3D)||!std::isfinite(vs3D)||!std::isfinite(rho3D)
            ||vp3D<=0||vp3D>30000||vs3D<0||vs3D>=vp3D
            ||vp3D*vp3D<=(4.0f/3.0f)*vs3D*vs3D||rho3D<=0||rho3D>50000)
            throw std::runtime_error("Cannot export VTI: the model contains invalid Vp/Vs/rho.");
    }
}

} // namespace

// Export original physical materials as three contiguous VTK point arrays using bounded XY buffers.
void exportVtiModel3D(Seis3D_State& s) {
    if(!s.ready3D||!s.gpu3D.stream3D) throw std::runtime_error("Initialize the 3D model before exporting VTI.");
    if(s.recording3D||s.sequence3D) throw std::runtime_error("Stop acquisition before exporting VTI.");
    const auto path3D=uiPath3D(s.vtiModelExportPath3D);
    auto extension3D=path3D.extension().string();
    std::transform(extension3D.begin(),extension3D.end(),extension3D.begin(),
        [](unsigned char c3D){return static_cast<char>(std::tolower(c3D));});
    if(extension3D!=".vti") throw std::runtime_error("Choose a .vti filename for VTK ImageData export.");
    if(std::filesystem::exists(path3D)) throw std::runtime_error("Choose a new model filename; the destination already exists.");

    const auto& c3D=s.context3D; // Export the live initialized model, not pending edits.
    if(c3D.nx3D<1||c3D.ny3D<1||c3D.nz3D<1||!std::isfinite(c3D.spacing3D)||c3D.spacing3D<=0)
        throw std::runtime_error("Cannot export VTI: invalid physical grid.");
    const auto planeCells3D=std::uint64_t(c3D.nx3D)*c3D.ny3D;
    if(planeCells3D>maxPhysicalNodes3D/std::uint64_t(c3D.nz3D))
        throw std::runtime_error("Cannot export VTI: physical node budget exceeded.");
    const std::uint64_t arrayBytes3D=planeCells3D*c3D.nz3D*4;
    const std::uint64_t blockBytes3D=sizeof(std::uint64_t)+arrayBytes3D;
    const std::array<std::uint64_t,3> offsets3D={0,blockBytes3D,2*blockBytes3D};
    const char* names3D[]={"Vp","Vs","Density"};
    if(!path3D.parent_path().empty()) std::filesystem::create_directories(path3D.parent_path());

    // Reserve a private temporary directory so an existing .part file can never be truncated.
    auto temporaryDirectory3D=path3D;temporaryDirectory3D+=std::filesystem::path(".part-"+sessionSuffix3D());
    if(!std::filesystem::create_directory(temporaryDirectory3D))
        throw std::runtime_error("Cannot reserve a temporary VTI output; choose another name or retry.");
    const auto temporaryPath3D=temporaryDirectory3D/"model.vti.part";
    try {
        std::ofstream output3D(temporaryPath3D,std::ios::binary|std::ios::trunc);
        if(!output3D) throw std::runtime_error("Cannot create VTI model file.");
        output3D.imbue(std::locale::classic());
        output3D<<std::setprecision(std::numeric_limits<float>::max_digits10)
            <<"<?xml version=\"1.0\"?>\n"
            <<"<VTKFile type=\"ImageData\" version=\"1.0\" byte_order=\"LittleEndian\" header_type=\"UInt64\">\n"
            <<"  <!-- SeisWave Lab physical model. Coordinates: m; Z is positive depth. Vp/Vs: m/s; Density: kg/m^3. -->\n"
            <<"  <ImageData WholeExtent=\"0 "<<c3D.nx3D-1<<" 0 "<<c3D.ny3D-1<<" 0 "<<c3D.nz3D-1
            <<"\" Origin=\"0 0 0\" Spacing=\""<<c3D.spacing3D<<' '<<c3D.spacing3D<<' '<<c3D.spacing3D<<"\">\n"
            <<"    <FieldData>\n"
            <<"      <DataArray type=\"Int32\" Name=\"BoundaryMode\" NumberOfTuples=\"1\" format=\"ascii\">"
            <<(c3D.reflecting3D?2:c3D.freeSurface3D?1:0)<<"</DataArray>\n"
            <<"      <DataArray type=\"Int32\" Name=\"CPMLThickness\" NumberOfTuples=\"1\" format=\"ascii\">"<<c3D.pml3D<<"</DataArray>\n"
            <<"      <DataArray type=\"Int32\" Name=\"DepthPositiveDown\" NumberOfTuples=\"1\" format=\"ascii\">1</DataArray>\n"
            <<"      <DataArray type=\"Float64\" Name=\"LengthUnitInMeters\" NumberOfTuples=\"1\" format=\"ascii\">1</DataArray>\n"
            <<"      <DataArray type=\"Float64\" Name=\"VelocityUnitInMetersPerSecond\" NumberOfTuples=\"1\" format=\"ascii\">1</DataArray>\n"
            <<"      <DataArray type=\"Float64\" Name=\"DensityUnitInKilogramsPerCubicMeter\" NumberOfTuples=\"1\" format=\"ascii\">1</DataArray>\n"
            <<"    </FieldData>\n"
            <<"    <Piece Extent=\"0 "<<c3D.nx3D-1<<" 0 "<<c3D.ny3D-1<<" 0 "<<c3D.nz3D-1<<"\">\n"
            <<"      <PointData Scalars=\"Vp\">\n";
        for(int a3D=0;a3D<3;++a3D)
            output3D<<"        <DataArray type=\"Float32\" Name=\""<<names3D[a3D]
                <<"\" NumberOfComponents=\"1\" format=\"appended\" offset=\""<<offsets3D[a3D]<<"\"/>\n";
        output3D<<"      </PointData>\n      <CellData/>\n    </Piece>\n  </ImageData>\n  <AppendedData encoding=\"raw\">\n_";
        const auto dataStart3D=output3D.tellp();
        if(!output3D||dataStart3D==std::streampos(-1)) throw std::runtime_error("Cannot write VTI file header.");
        for(int a3D=0;a3D<3;++a3D) {
            output3D.seekp(dataStart3D+std::streamoff(offsets3D[a3D]));
            writeVtiLength3D(output3D,arrayBytes3D);
        }

        std::vector<float> plane3D(std::size_t(planeCells3D)*3);
        std::vector<float> component3D(static_cast<std::size_t>(planeCells3D));
        for(int z3D=0;z3D<c3D.nz3D;++z3D) {
            downloadModelPlane3D(s.gpu3D,c3D,z3D,plane3D.data());
            validateVtiPlane3D(plane3D);
            for(int a3D=0;a3D<3;++a3D) {
                for(std::size_t i3D=0;i3D<component3D.size();++i3D) component3D[i3D]=plane3D[i3D*3+a3D];
                const auto planeOffset3D=offsets3D[a3D]+sizeof(std::uint64_t)+std::uint64_t(z3D)*planeCells3D*4;
                output3D.seekp(dataStart3D+std::streamoff(planeOffset3D));
                writeVtiValues3D(output3D,component3D);
            }
        }
        output3D.seekp(dataStart3D+std::streamoff(3*blockBytes3D));
        output3D<<"\n  </AppendedData>\n</VTKFile>\n";
        output3D.close();
        if(!output3D) throw std::runtime_error("Cannot finalize VTI model file; check disk space.");
        if(std::filesystem::exists(path3D)) throw std::runtime_error("VTI destination now exists; choose a new filename.");
        std::filesystem::rename(temporaryPath3D,path3D);
    } catch(...) {
        std::error_code ignored3D;
        std::filesystem::remove(temporaryPath3D,ignored3D);
        std::filesystem::remove(temporaryDirectory3D,ignored3D);
        throw;
    }
    std::error_code ignored3D;std::filesystem::remove(temporaryDirectory3D,ignored3D);
    s.message3D="VTK ImageData model exported: "+pathToUtf8(path3D);
    s.messageIsError3D=false;
}

// Export one native-resolution scalar snapshot without applying display gain, palette or volume sampling.
void exportVtiWavefield3D(Seis3D_State& s) {
    if(!s.ready3D||!s.gpu3D.stream3D) throw std::runtime_error("Initialize the 3D model before exporting a wavefield snapshot.");
    if(s.recording3D||s.sequence3D) throw std::runtime_error("Stop acquisition before exporting a wavefield snapshot.");
    if(s.component3D<0||s.component3D>4) throw std::runtime_error("Select a physical wavefield component before export.");
    const auto path3D=uiPath3D(s.vtiWavefieldExportPath3D);
    auto extension3D=path3D.extension().string();
    std::transform(extension3D.begin(),extension3D.end(),extension3D.begin(),
        [](unsigned char c3D){return static_cast<char>(std::tolower(c3D));});
    if(extension3D!=".vti") throw std::runtime_error("Choose a .vti filename for the wavefield snapshot.");
    if(std::filesystem::exists(path3D)) throw std::runtime_error("Choose a new snapshot filename; the destination already exists.");

    const auto& c3D=s.context3D;
    const auto planeCells3D=std::uint64_t(c3D.nx3D)*c3D.ny3D;
    if(c3D.nx3D<1||c3D.ny3D<1||c3D.nz3D<1||!std::isfinite(c3D.spacing3D)||c3D.spacing3D<=0
        ||planeCells3D>maxPhysicalNodes3D/std::uint64_t(c3D.nz3D))
        throw std::runtime_error("Cannot export wavefield: invalid physical grid.");
    const std::uint64_t arrayBytes3D=planeCells3D*c3D.nz3D*sizeof(float);
    const std::uint64_t blockBytes3D=sizeof(std::uint64_t)+arrayBytes3D;
    const char* componentNames3D[]={"Vz","Vx","Vy","VelocityMagnitude","Pressure"};
    const char* unitNames3D[]={"m/s","m/s","m/s","m/s","Pa"};
    const int component3D=s.component3D;
    const auto time3D=static_cast<double>(s.step3D)*c3D.dt3D;
    if(!std::isfinite(time3D)||!std::isfinite(c3D.dt3D)||c3D.dt3D<=0)
        throw std::runtime_error("Cannot export wavefield: invalid simulation time metadata.");
    if(!path3D.parent_path().empty()) std::filesystem::create_directories(path3D.parent_path());
    auto temporaryDirectory3D=path3D;temporaryDirectory3D+=std::filesystem::path(".part-"+sessionSuffix3D());
    if(!std::filesystem::create_directory(temporaryDirectory3D))
        throw std::runtime_error("Cannot reserve a temporary wavefield output; choose another name or retry.");
    const auto temporaryPath3D=temporaryDirectory3D/"wavefield.vti.part";
    try {
        std::ofstream output3D(temporaryPath3D,std::ios::binary|std::ios::trunc);
        if(!output3D) throw std::runtime_error("Cannot create VTI wavefield file.");
        output3D.imbue(std::locale::classic());
        output3D<<std::setprecision(std::numeric_limits<double>::max_digits10)
            <<"<?xml version=\"1.0\"?>\n"
            <<"<VTKFile type=\"ImageData\" version=\"1.0\" byte_order=\"LittleEndian\" header_type=\"UInt64\">\n"
            <<"  <!-- SeisWave Lab wavefield snapshot. Component: "<<componentNames3D[component3D]<<"; unit: "<<unitNames3D[component3D]<<"; Z is positive depth. -->\n"
            <<"  <ImageData WholeExtent=\"0 "<<c3D.nx3D-1<<" 0 "<<c3D.ny3D-1<<" 0 "<<c3D.nz3D-1
            <<"\" Origin=\"0 0 0\" Spacing=\""<<c3D.spacing3D<<' '<<c3D.spacing3D<<' '<<c3D.spacing3D<<"\">\n"
            <<"    <FieldData>\n"
            <<"      <DataArray type=\"UInt64\" Name=\"SimulationStep\" NumberOfTuples=\"1\" format=\"ascii\">"<<s.step3D<<"</DataArray>\n"
            <<"      <DataArray type=\"Float64\" Name=\"SimulationTimeSeconds\" NumberOfTuples=\"1\" format=\"ascii\">"<<time3D<<"</DataArray>\n"
            <<"      <DataArray type=\"Float64\" Name=\"TimeStepSeconds\" NumberOfTuples=\"1\" format=\"ascii\">"<<c3D.dt3D<<"</DataArray>\n"
            <<"      <DataArray type=\"Int32\" Name=\"WavefieldComponentId\" NumberOfTuples=\"1\" format=\"ascii\">"<<component3D<<"</DataArray>\n"
            <<"      <DataArray type=\"Int32\" Name=\"AmplitudeUnitCode\" NumberOfTuples=\"1\" format=\"ascii\">"<<(component3D==4?1:0)<<"</DataArray>\n"
            <<"      <DataArray type=\"Int32\" Name=\"DepthPositiveDown\" NumberOfTuples=\"1\" format=\"ascii\">1</DataArray>\n"
            <<"      <DataArray type=\"Float64\" Name=\"LengthUnitInMeters\" NumberOfTuples=\"1\" format=\"ascii\">1</DataArray>\n"
            <<"    </FieldData>\n"
            <<"    <Piece Extent=\"0 "<<c3D.nx3D-1<<" 0 "<<c3D.ny3D-1<<" 0 "<<c3D.nz3D-1<<"\">\n"
            <<"      <PointData Scalars=\""<<componentNames3D[component3D]<<"\">\n"
            <<"        <DataArray type=\"Float32\" Name=\""<<componentNames3D[component3D]
            <<"\" NumberOfComponents=\"1\" format=\"appended\" offset=\"0\"/>\n"
            <<"      </PointData>\n      <CellData/>\n    </Piece>\n  </ImageData>\n  <AppendedData encoding=\"raw\">\n_";
        const auto dataStart3D=output3D.tellp();
        if(!output3D||dataStart3D==std::streampos(-1)) throw std::runtime_error("Cannot write VTI wavefield header.");
        writeVtiLength3D(output3D,arrayBytes3D);
        std::vector<float> plane3D(static_cast<std::size_t>(planeCells3D));
        std::vector<float> scratch3D(static_cast<std::size_t>(planeCells3D)*6);
        for(int z3D=0;z3D<c3D.nz3D;++z3D) {
            downloadWavefieldPlane3D(s.gpu3D,c3D,z3D,component3D,plane3D.data(),scratch3D.data());
            if(!std::all_of(plane3D.begin(),plane3D.end(),[](float value3D){return std::isfinite(value3D);}))
                throw std::runtime_error("Cannot export VTI: the current wavefield contains non-finite values.");
            writeVtiValues3D(output3D,plane3D);
        }
        output3D.seekp(dataStart3D+std::streamoff(blockBytes3D));
        output3D<<"\n  </AppendedData>\n</VTKFile>\n";
        output3D.close();
        if(!output3D) throw std::runtime_error("Cannot finalize VTI wavefield file; check disk space.");
        if(std::filesystem::exists(path3D)) throw std::runtime_error("Wavefield destination now exists; choose a new filename.");
        std::filesystem::rename(temporaryPath3D,path3D);
    } catch(...) {
        std::error_code ignored3D;std::filesystem::remove(temporaryPath3D,ignored3D);std::filesystem::remove(temporaryDirectory3D,ignored3D);throw;
    }
    std::error_code ignored3D;std::filesystem::remove(temporaryDirectory3D,ignored3D);
    s.message3D=std::string("VTK wavefield snapshot exported: ")+componentNames3D[component3D]+" / "+unitNames3D[component3D]
        +" / t="+std::to_string(time3D)+" s / "+pathToUtf8(path3D);
    s.messageIsError3D=false;
}

} // namespace Seis3D::Detail3D
