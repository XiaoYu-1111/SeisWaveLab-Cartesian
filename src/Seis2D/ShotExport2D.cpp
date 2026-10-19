#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif
#include "ShotExport2D.h"
#include "SolverNumerics.h"
#include <array>
#include <chrono>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace Seis2D {
namespace {
// Serialize UTF-8 text without allowing paths or device labels to break JSON syntax.
std::string jsonText(const std::string& value) {
    std::ostringstream out;out<<'"';
    for(unsigned char c:value) {
        if(c=='"'||c=='\\') out<<'\\'<<char(c);
        else if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;
        else out<<char(c);
    }
    out<<'"';return out.str();
}
// Retain Unicode names in the JSON file independently of the Windows active code page.
std::string utf8Path(const std::filesystem::path& path) {
    const auto text=path.u8string();return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
// Open bounded output streams with predictable decimal separators and visible write failures.
std::ofstream outputFile(const std::filesystem::path& path,bool binary=false) {
    std::ofstream out;out.exceptions(std::ios::failbit|std::ios::badbit);
    out.open(path,std::ios::out|(binary?std::ios::binary:std::ios::openmode(0)));
    out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<double>::max_digits10);return out;
}
// Write each model word in little endian so archived arrays have a documented portable layout.
void writeWord(std::ostream& out,std::uint32_t bits) {
    const unsigned char b[]={static_cast<unsigned char>(bits),static_cast<unsigned char>(bits>>8),
        static_cast<unsigned char>(bits>>16),static_cast<unsigned char>(bits>>24)};
    out.write(reinterpret_cast<const char*>(b),4);
}
// Preserve the exact float32 model/source values rather than rounded text values.
void writeFloats(std::ostream& out,const std::vector<float>& values) {
    for(float value:values) {std::uint32_t bits;std::memcpy(&bits,&value,4);writeWord(out,bits);}
}
// Hash the bytes that were actually closed on disk, including Unicode-path files.
std::string sha256File(const std::filesystem::path& path) {
#ifdef _WIN32
    struct HashResources {
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
        std::vector<unsigned char> object;
        ~HashResources(){if(hash) BCryptDestroyHash(hash);if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);}
    } resources;
    const auto check=[](NTSTATUS code){if(code<0) throw std::runtime_error("Cannot calculate package SHA-256.");};
    check(BCryptOpenAlgorithmProvider(&resources.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0));
    DWORD bytes=0,returned=0;check(BCryptGetProperty(resources.algorithm,BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&bytes),sizeof(bytes),&returned,0));
    resources.object.resize(bytes);check(BCryptCreateHash(resources.algorithm,&resources.hash,
        resources.object.data(),bytes,nullptr,0,0));
    std::ifstream input(path,std::ios::binary);if(!input) throw std::runtime_error("Cannot read package for SHA-256.");
    std::array<unsigned char,65536> buffer{};
    while(input) {input.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
        if(input.gcount()) check(BCryptHashData(resources.hash,buffer.data(),ULONG(input.gcount()),0));}
    if(!input.eof()) throw std::runtime_error("Package file changed or became unreadable while hashing.");
    std::array<unsigned char,32> digest{};check(BCryptFinishHash(resources.hash,digest.data(),ULONG(digest.size()),0));
    std::ostringstream out;for(auto byte:digest) out<<std::hex<<std::setw(2)<<std::setfill('0')<<int(byte);return out.str();
#else
    throw std::runtime_error("This build requires a SHA-256 provider for shot-package export.");
#endif
}
// Check the entire package before creating any temporary output directory.
void validatePackage(const ShotExportRequest2D& r,const FrozenModel2D& m,
    const std::vector<std::vector<float>>& vx,const std::vector<std::vector<float>>& vz,
    const std::vector<SeismicIO::TraceMetadata>& geometry) {
    validateExportPrefix2D(r.prefix);
    if(r.directory.empty()||r.shot<1||r.samples<1||r.samples>32767||!std::isfinite(r.sampleInterval)||
        r.sampleInterval<=0||r.sampleInterval*1e6<.5||r.sampleInterval*1e6>=32767.5)
        throw std::invalid_argument("Choose an output directory, valid shot ID and SEG-Y-compatible samples/interval.");
    if(vx.empty()||vx.size()!=vz.size()||vx.size()!=geometry.size()) throw std::invalid_argument("Shot components and geometry differ.");
    for(const auto* component:{&vx,&vz}) for(const auto& trace:*component) {
        if(trace.size()!=std::size_t(r.samples)) throw std::invalid_argument("Shot sample counts differ.");
        for(float value:trace) if(!std::isfinite(value)) throw std::invalid_argument("Shot contains invalid amplitudes.");
    }
    if(m.nx<1||m.nz<1||m.npml<0||m.formulation<1||m.formulation>3||m.samplingStride<1||m.captureStartStep<0||
        m.sourceType<0||m.sourceType>4||m.sourceX<0||m.sourceX>=m.nx||m.sourceZ<0||m.sourceZ>=m.nz||
        !std::isfinite(m.spacing)||m.spacing<=0||!std::isfinite(m.solverDt)||m.solverDt<=0||
        !std::isfinite(m.frequency)||m.frequency<=0||!std::isfinite(m.delay)||
        !std::isfinite(m.sourceAmplitude)||!std::isfinite(m.sourceAngle))
        throw std::invalid_argument("Frozen model parameters are invalid.");
    if(r.includeModel) {
        const auto count=std::uint64_t(m.nx)*m.nz;
        if(count*12>256ull*1024*1024||m.lambda.size()!=count||m.mu.size()!=count||m.rho.size()!=count||m.wavelet.empty())
            throw std::invalid_argument("Frozen model arrays are absent, inconsistent or exceed 256 MiB.");
        for(const auto* values:{&m.lambda,&m.mu,&m.rho,&m.wavelet}) for(float value:*values)
            if(!std::isfinite(value)) throw std::invalid_argument("Frozen model contains non-finite values.");
    }
}
}

// Validate UTF-8 basename text while rejecting separators, Windows devices and ambiguous endings.
void validateExportPrefix2D(const std::string& prefix) {
    if(prefix.empty()||prefix.size()>120||prefix=="."||prefix==".."||prefix.back()=='.'||prefix.back()==' ')
        throw std::invalid_argument("Shot prefix must be 1-120 UTF-8 bytes without a trailing dot or space.");
    for(unsigned char c:prefix) if(c<32||std::strchr("<>:\"/\\|?*",c))
        throw std::invalid_argument("Shot prefix must be a filename, without path separators or reserved characters.");
    std::string stem=prefix.substr(0,prefix.find('.'));for(char& c:stem) if(c>='a'&&c<='z') c-=32;
    if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||
        (stem.size()==4&&(stem.substr(0,3)=="COM"||stem.substr(0,3)=="LPT")&&stem[3]>='1'&&stem[3]<='9'))
        throw std::invalid_argument("Shot prefix is a reserved Windows filename.");
}

// Publish one complete directory; never modify an existing destination or an unrelated staging folder.
std::filesystem::path exportShotPackage2D(const ShotExportRequest2D& r,const FrozenModel2D& m,
    const std::vector<std::vector<float>>& vx,const std::vector<std::vector<float>>& vz,
    const std::vector<SeismicIO::TraceMetadata>& geometry) {
    validatePackage(r,m,vx,vz,geometry);
    std::ostringstream name;name<<r.prefix<<'_'<<std::setw(4)<<std::setfill('0')<<r.shot;
    const auto base=std::filesystem::absolute(r.directory).lexically_normal();
    const auto final=base/std::filesystem::u8path(name.str());
    if(std::filesystem::exists(final)) throw std::runtime_error("Shot destination exists. Choose a new name or session.");
    std::filesystem::create_directories(base);
    const auto serial=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto staging=base/std::filesystem::u8path(name.str()+".part-"+std::to_string(serial));
    if(!std::filesystem::create_directory(staging)) throw std::runtime_error("Cannot create unique shot staging directory.");
    try {
        SeismicIO::writeSegyCaptureFile2D(vx,geometry,staging/"record_vx.sgy",r.sampleInterval);
        SeismicIO::writeSegyCaptureFile2D(vz,geometry,staging/"record_vz.sgy",r.sampleInterval);
        std::vector<std::string> files={"record_vx.sgy","record_vz.sgy","geometry.csv"};
        {
            auto out=outputFile(staging/"geometry.csv");out<<"trace,source_x_m,source_z_m,receiver_x_m,receiver_z_m\n";
            for(const auto& g:geometry) {
                const double scale=g.coordinateScalar<0?1.0/-g.coordinateScalar:g.coordinateScalar==0?1:g.coordinateScalar;
                out<<g.traceInRecord<<','<<g.sourceX*scale<<','<<g.sourceY*scale<<','<<g.groupX*scale<<','<<g.groupY*scale<<'\n';
            }
            out.close();
        }
        if(r.includeCsv) {
            auto out=outputFile(staging/"record_vz.csv");out<<"time_s";
            for(std::size_t c=0;c<vz.size();++c) out<<",trace_"<<c+1;out<<'\n';
            for(int i=0;i<r.samples;++i) {out<<double(i)*r.sampleInterval;for(const auto& trace:vz) out<<','<<trace[i];out<<'\n';}
            out.close();files.push_back("record_vz.csv");
        }
        if(r.includeModel) {
            auto out=outputFile(staging/"model.s2dm",true);out.write("S2DMOD1\0",8);writeWord(out,m.nx);writeWord(out,m.nz);
            std::uint32_t bits;std::memcpy(&bits,&m.spacing,4);writeWord(out,bits);
            writeFloats(out,m.lambda);writeFloats(out,m.mu);writeFloats(out,m.rho);out.close();
            auto wave=outputFile(staging/"source_wavelet.f32le",true);writeFloats(wave,m.wavelet);wave.close();
            files.push_back("model.s2dm");files.push_back("source_wavelet.f32le");
        }
        auto manifest=outputFile(staging/"manifest.json");
        const char* mechanisms[]={"vertical body force","horizontal body force","isotropic stress","shear stress","rotated body force"};
        manifest<<"{\n  \"schema\": \"SeisWaveShot2D-1\",\n  \"software\": \"SeisWave Lab\",\n  \"version\": "<<jsonText(r.version)
            <<",\n  \"gpu\": "<<jsonText(r.device)<<",\n  \"shot_id\": "<<r.shot
            <<",\n  \"created_unix_us\": "<<std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
            <<",\n  \"traces\": "<<vx.size()<<",\n  \"samples\": "<<r.samples<<",\n  \"sample_interval_s\": "<<r.sampleInterval
            <<",\n  \"segy_interval_us\": "<<std::llround(double(r.sampleInterval)*1e6)
            <<",\n  \"units\": \"m/s\",\n  \"amplitudes\": \"raw; no display gain or AGC\",\n"
            <<"  \"time_origin\": \"local acquisition start; SEG-Y delay is zero\",\n"
            <<"  \"coordinates\": \"X horizontal; Z down from model grid origin; legacy SEG-Y Y stores Z\",\n"
            <<"  \"model_layout\": "<<jsonText("S2DMOD1\\0 + uint32le nx,nz + float32le h; lambda,mu,rho planes; index=z*nx+x")<<",\n"
            <<"  \"model_included\": "<<(r.includeModel?"true":"false")
            <<",\n  \"grid\": {\"nx\": "<<m.nx<<", \"nz\": "<<m.nz<<", \"spacing_m\": "<<m.spacing<<", \"npml\": "<<m.npml<<", \"external_pml\": "<<(m.externalPml?"true":"false")
            <<", \"formulation\": "<<m.formulation<<", \"free_surface\": "<<(m.freeSurface?"true":"false")<<"},\n"
            <<"  \"solver\": {\"dt_s\": "<<m.solverDt<<", \"sampling_stride\": "<<m.samplingStride
            <<", \"capture_start_step\": "<<m.captureStartStep<<", \"nt\": "<<m.wavelet.size()<<", \"spatial_order\": 8, \"fast_math_build\": true},\n"
            <<"  \"source\": {\"x\": "<<m.sourceX<<", \"z\": "<<m.sourceZ<<", \"type\": "<<m.sourceType
            <<", \"mechanism\": "<<jsonText(mechanisms[m.sourceType])
            <<", \"numerical_injection_scale\": "<<kSourceInjectionScale2D
            <<", \"amplitude\": "<<m.sourceAmplitude<<", \"angle_deg\": "<<m.sourceAngle<<", \"f0_hz\": "<<m.frequency
            <<", \"delay_s\": "<<m.delay<<", \"multiple_sources\": "<<(m.multipleSources?"true":"false")<<"},\n"
            <<"  \"reproduction_note\": \"Continuation captures lack the initial dynamic wavefield; multi-source events are not archived. This package alone is not a replay checkpoint.\",\n"
            <<"  \"files\": [\n";
        for(std::size_t i=0;i<files.size();++i) manifest<<"    {\"path\": "<<jsonText(files[i])<<", \"bytes\": "
            <<std::filesystem::file_size(staging/files[i])<<", \"sha256\": "<<jsonText(sha256File(staging/files[i]))<<'}'<<(i+1<files.size()?",":"")<<'\n';
        manifest<<"  ]\n}\n";manifest.close();
        // Windows directory rename refuses an existing destination, including a racing writer.
        if(std::filesystem::exists(final)) throw std::runtime_error("Shot destination was created by another writer.");
        std::filesystem::rename(staging,final);return final;
    } catch(...) {
        // Only this call's successfully created staging directory is eligible for cleanup.
        std::error_code ignored;std::filesystem::remove_all(staging,ignored);throw;
    }
}
}
