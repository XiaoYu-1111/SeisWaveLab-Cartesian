#include "Internal3D.h"
#include "Seis3D_Sim.h"
#include "Cuda_Seis3D.cuh"
#include "ResourcePaths.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Seis3D::Detail3D {
// Build a bounded surface receiver mesh in physical grid coordinates, checking 64-bit extents first.
void configureReceivers3D(Seis3D_State& s) {
    const auto& q=s.survey3D; const auto& c=s.context3D;
    if(q.receiverCountX3D<1||q.receiverCountY3D<1||q.receiverCountX3D>maxReceivers3D||q.receiverCountY3D>maxReceivers3D
        ||std::int64_t(q.receiverCountX3D)*q.receiverCountY3D>maxReceivers3D||q.receiverSpacing3D[0]<1||q.receiverSpacing3D[1]<1)
        throw std::runtime_error("Receiver mesh must have 1..100 total channels (X times Y) and positive spacing.");
    const auto lastX=std::int64_t(q.receiverOrigin3D[0])+std::int64_t(q.receiverCountX3D-1)*q.receiverSpacing3D[0];
    const auto lastY=std::int64_t(q.receiverOrigin3D[1])+std::int64_t(q.receiverCountY3D-1)*q.receiverSpacing3D[1];
    if(q.receiverOrigin3D[0]<0||q.receiverOrigin3D[1]<0||lastX>=c.nx3D||lastY>=c.ny3D
        ||q.receiverOrigin3D[2]<0||q.receiverOrigin3D[2]>=c.nz3D) throw std::runtime_error("Receiver mesh exceeds the model.");
    std::vector<Receiver3D> receivers;
    for(int y=0;y<q.receiverCountY3D;++y) for(int x=0;x<q.receiverCountX3D;++x)
        receivers.push_back({q.receiverOrigin3D[0]+x*q.receiverSpacing3D[0],q.receiverOrigin3D[1]+y*q.receiverSpacing3D[1],q.receiverOrigin3D[2]});
    LiveTrace3D monitor;monitor.receiverCount3D=int(receivers.size());
    monitor.samples3D.resize(std::size_t(LiveTrace3D::capacity3D)*receivers.size()*3);
    setReceivers3D(s.gpu3D,c,receivers.data(),int(receivers.size()));s.receivers3D=std::move(receivers);s.liveTrace3D=std::move(monitor);
    s.message3D="3D receiver mesh deployed.";
}

// Set a signed or unsigned 16-bit SEG-Y field in network byte order.
void putU16Be3D(unsigned char* output,int offset,std::uint16_t value) { output[offset]=static_cast<unsigned char>(value>>8); output[offset+1]=static_cast<unsigned char>(value); }

// Set a signed 32-bit SEG-Y field in network byte order.
void putI32Be3D(unsigned char* output,int offset,std::int32_t value) {
    const auto bits=std::uint32_t(value);
    for(int a=0;a<4;++a) output[offset+a]=static_cast<unsigned char>(bits>>(24-8*a));
}

// Convert metres to millimetre-scaled SEG-Y coordinates, rejecting overflow rather than clipping.
std::int32_t coordinateMm3D(double metres) {
    const double millimetres=std::round(metres*1000);
    if(!std::isfinite(millimetres)||millimetres<std::numeric_limits<std::int32_t>::min()||millimetres>std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("SEG-Y coordinate exceeds the signed 32-bit millimetre range.");
    return std::int32_t(millimetres);
}

// Encode world XY with the gather's largest safe decimal precision, supporting large northings.
std::int32_t coordinateValue3D(double metres,int scalar) {
    const double value=std::round(metres*(scalar<0 ? -scalar : 1.0/scalar));
    if(!std::isfinite(value)||value<std::numeric_limits<std::int32_t>::min()||value>std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("SEG-Y world coordinate exceeds the supported integer range.");
    return std::int32_t(value);
}

// Compute one-based CMP inline/crossline bins from source-receiver physical midpoints.
std::pair<int,int> cmpBin3D(const Gather3D& gather,const Receiver3D& receiver) {
    const double x=0.5*(gather.source3D.x3D+receiver.x3D)*gather.context3D.spacing3D;
    const double y=0.5*(gather.source3D.y3D+receiver.y3D)*gather.context3D.spacing3D;
    const double inlineBin=std::floor(y/gather.survey3D.cmpBinSize3D[1])+1; // X-Z inline plane is indexed by Y.
    const double crosslineBin=std::floor(x/gather.survey3D.cmpBinSize3D[0])+1;
    if(!std::isfinite(inlineBin)||!std::isfinite(crosslineBin)||inlineBin>2147483646.0||crosslineBin>2147483646.0)
        throw std::runtime_error("CMP bin index overflow.");
    return {int(inlineBin),int(crosslineBin)};
}

// Write one SEG-Y rev1 IEEE-float component, freezing all geometry and sample metadata in the gather.
void writeSegyComponent3D(const Gather3D& gather,const std::filesystem::path& path,int component) {
    if(gather.sampleCount3D<1||gather.sampleCount3D>maxSegySamples3D||gather.sampleIntervalUs3D<1||gather.sampleIntervalUs3D>maxSegyIntervalUs3D)
        throw std::runtime_error("SEG-Y rev1 sample count/interval exceeds its positive signed 16-bit range (32767).");
    const auto temporary=std::filesystem::path(path.wstring()+L".part");
    try {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        if(!output) throw std::runtime_error("Cannot create 3D SEG-Y file.");
        std::array<char,3200> text; text.fill(' ');
        const char* names[3]={"VX","VY","VZ"};
        for(int line=0;line<40;++line) { char label[5]{}; std::snprintf(label,sizeof(label),"C%02d ",line+1); std::memcpy(text.data()+line*80,label,4); }
        const std::string lines[]={"SEIS3D ISOTROPIC ELASTIC COMMON-SHOT GATHER / SEG-Y REV1 / ASCII",
            std::string("COMPONENT ")+names[component]+"; VELOCITY IN M/S; IEEE 32-BIT BIG-ENDIAN SAMPLES",
            "METRES; XY SCALAR "+std::to_string(gather.coordinateScalar3D)+"; ELEVATION SCALAR -1000",
            "TRACE BYTES 189-192 CMP INLINE; 193-196 CMP CROSSLINE; 181-188 MIDPOINT XY",
            "SURFACE RECEIVER GRID; INLINE/CROSSLINE ARE ONE-BASED CMP BIN INDICES",
            "FIRST VELOCITY SAMPLE TIME (S): "+std::to_string(gather.firstSampleTime3D),
            "LEAPFROG HALF-TIME VELOCITIES; DELAY USES TIME SCALAR AT BYTES 215-216",
            "SHOTPOINT 197-200=SOURCE ID; INLINE BINS Y / CROSSLINE BINS X; Z DOWN",
            "GEOMETRY CSV PRESERVES EXACT COORDINATES AND FIRST SAMPLE TIME",
            "GRID SPACING M: "+std::to_string(gather.context3D.spacing3D)+"; SOURCE F0 HZ: "+std::to_string(gather.context3D.frequency3D),
            "SOURCE AMPLITUDE: "+std::to_string(gather.source3D.amplitude3D)+(gather.source3D.kind3D==SourceKind3D::Explosion?" PA/S EXPLOSION":" N/M3 BODY FORCE")};
        for(std::size_t i=0;i<std::size(lines);++i) std::memcpy(text.data()+i*80+4,lines[i].data(),(std::min)(std::size_t(76),lines[i].size()));
        output.write(text.data(),text.size());
        std::array<unsigned char,400> binary{};
        putI32Be3D(binary.data(),0,1); putI32Be3D(binary.data(),4,1);
        putU16Be3D(binary.data(),12,std::uint16_t(gather.receivers3D.size()));
        putU16Be3D(binary.data(),16,std::uint16_t(gather.sampleIntervalUs3D)); putU16Be3D(binary.data(),18,std::uint16_t(gather.sampleIntervalUs3D));
        putU16Be3D(binary.data(),20,std::uint16_t(gather.sampleCount3D)); putU16Be3D(binary.data(),22,std::uint16_t(gather.sampleCount3D));
        putU16Be3D(binary.data(),24,5); putU16Be3D(binary.data(),28,5); // IEEE float / common-source sorting.
        putU16Be3D(binary.data(),54,1); putU16Be3D(binary.data(),300,0x0100); putU16Be3D(binary.data(),302,1);
        output.write(reinterpret_cast<const char*>(binary.data()),binary.size());
        const double spacing=gather.context3D.spacing3D;
        const double sx=gather.survey3D.worldOrigin3D[0]+gather.source3D.x3D*spacing;
        const double sy=gather.survey3D.worldOrigin3D[1]+gather.source3D.y3D*spacing;
        int timeFactor=10000;
        while(timeFactor>1&&std::round(gather.firstSampleTime3D*1000*timeFactor)>32767) timeFactor/=10;
        std::vector<unsigned char> traceSamples(std::size_t(gather.sampleCount3D)*4);
        for(std::size_t r=0;r<gather.receivers3D.size();++r) {
            const auto receiver=gather.receivers3D[r];
            const double gx=gather.survey3D.worldOrigin3D[0]+receiver.x3D*spacing;
            const double gy=gather.survey3D.worldOrigin3D[1]+receiver.y3D*spacing;
            const auto bin=cmpBin3D(gather,receiver);
            std::array<unsigned char,240> trace{};
            putI32Be3D(trace.data(),0,int(r+1)); putI32Be3D(trace.data(),4,int(r+1)); putI32Be3D(trace.data(),8,gather.shotId3D);
            putI32Be3D(trace.data(),12,int(r+1)); putI32Be3D(trace.data(),16,gather.shotId3D);
            putI32Be3D(trace.data(),20,gather.shotId3D); putI32Be3D(trace.data(),24,int(r+1));
            putU16Be3D(trace.data(),28,component==0?14:component==1?13:12); // Inline X / crossline Y / vertical Z.
            putU16Be3D(trace.data(),30,1); putU16Be3D(trace.data(),32,1);
            putI32Be3D(trace.data(),36,std::int32_t(std::round(std::hypot(gx-sx,gy-sy))));
            putI32Be3D(trace.data(),40,coordinateMm3D(-receiver.z3D*spacing));
            putI32Be3D(trace.data(),44,0); // Surface elevation at the source, distinct from buried source depth.
            putI32Be3D(trace.data(),48,coordinateMm3D(gather.source3D.z3D*spacing));
            putU16Be3D(trace.data(),68,std::uint16_t(std::int16_t(-1000)));
            putU16Be3D(trace.data(),70,std::uint16_t(std::int16_t(gather.coordinateScalar3D)));
            putI32Be3D(trace.data(),72,coordinateValue3D(sx,gather.coordinateScalar3D)); putI32Be3D(trace.data(),76,coordinateValue3D(sy,gather.coordinateScalar3D));
            putI32Be3D(trace.data(),80,coordinateValue3D(gx,gather.coordinateScalar3D)); putI32Be3D(trace.data(),84,coordinateValue3D(gy,gather.coordinateScalar3D));
            putU16Be3D(trace.data(),88,1);
            putU16Be3D(trace.data(),108,std::uint16_t(std::round(gather.firstSampleTime3D*1000*timeFactor)));
            putU16Be3D(trace.data(),114,std::uint16_t(gather.sampleCount3D)); putU16Be3D(trace.data(),116,std::uint16_t(gather.sampleIntervalUs3D));
            putI32Be3D(trace.data(),180,coordinateValue3D(0.5*(sx+gx),gather.coordinateScalar3D));
            putI32Be3D(trace.data(),184,coordinateValue3D(0.5*(sy+gy),gather.coordinateScalar3D));
            putI32Be3D(trace.data(),188,bin.first); putI32Be3D(trace.data(),192,bin.second); putI32Be3D(trace.data(),196,gather.shotId3D);
            putU16Be3D(trace.data(),200,1); putU16Be3D(trace.data(),202,6); // Shotpoint scalar / trace values in m/s.
            putI32Be3D(trace.data(),204,1); putU16Be3D(trace.data(),210,6);
            putU16Be3D(trace.data(),212,std::uint16_t(r+1)); putU16Be3D(trace.data(),214,std::uint16_t(std::int16_t(-timeFactor)));
            for(int sample=0;sample<gather.sampleCount3D;++sample) {
                const float value=gather.samples3D[(std::size_t(sample)*gather.receivers3D.size()+r)*3+component];
                if(!std::isfinite(value)) throw std::runtime_error("Refusing to export non-finite receiver samples.");
                std::uint32_t bits=0; std::memcpy(&bits,&value,4); putI32Be3D(traceSamples.data(),sample*4,std::int32_t(bits));
            }
            output.write(reinterpret_cast<const char*>(trace.data()),trace.size());
            output.write(reinterpret_cast<const char*>(traceSamples.data()),std::streamsize(traceSamples.size()));
            if(!output) throw std::runtime_error("3D SEG-Y write failed; check disk space.");
        }
        output.close(); if(!output) throw std::runtime_error("Cannot finalize 3D SEG-Y file.");
        if(std::filesystem::exists(path)) throw std::runtime_error("SEG-Y destination already exists.");
        std::filesystem::rename(temporary,path);
    } catch(...) { std::error_code ignored; std::filesystem::remove(temporary,ignored); throw; }
}

// Export vx/vy/vz and an explicit geometry/CMP manifest; success is reported only after all writes finish.
void exportGather3D(Seis3D_State& s,const std::filesystem::path& directory) {
    auto& gather=s.gather3D;
    if(gather.sampleCount3D<1) throw std::runtime_error("No 3D receiver samples to export.");
    std::ostringstream shotName; shotName<<validatedExportPrefix3D(s.exportPrefix3D)<<'_'<<std::setw(4)<<std::setfill('0')<<gather.shotId3D;
    const auto base=shotName.str();
    const char* suffix[3]={"_vx.sgy","_vy.sgy","_vz.sgy"};
    const char* outputs[4]={"_vx.sgy","_vy.sgy","_vz.sgy","_geometry.csv"};
    // Reject all name collisions before writing a component, including stale temporary files.
    for(const auto ending:outputs) {
        const auto destination=directory/uiPath3D((base+ending).c_str());
        if(std::filesystem::exists(destination)||std::filesystem::exists(std::filesystem::path(destination.wstring()+L".part")))
            throw std::runtime_error("Export files already exist; choose another prefix or enable a session subfolder.");
    }
    std::filesystem::create_directories(directory);
    for(int component=0;component<3;++component) writeSegyComponent3D(gather,directory/uiPath3D((base+suffix[component]).c_str()),component);
    std::ofstream csv(directory/uiPath3D((base+"_geometry.csv").c_str()));
    if(!csv) throw std::runtime_error("Cannot write geometry manifest.");
    csv<<"shot,receiver,sx_m,sy_m,sz_depth_m,gx_m,gy_m,gz_depth_m,cmp_inline,cmp_crossline,first_sample_s,sample_interval_s,samples\n"<<std::setprecision(12);
    const double h=gather.context3D.spacing3D;
    for(std::size_t r=0;r<gather.receivers3D.size();++r) {
        const auto p=gather.receivers3D[r]; const auto bin=cmpBin3D(gather,p);
        csv<<gather.shotId3D<<','<<r+1<<','<<gather.survey3D.worldOrigin3D[0]+gather.source3D.x3D*h<<','
            <<gather.survey3D.worldOrigin3D[1]+gather.source3D.y3D*h<<','<<gather.source3D.z3D*h<<','
            <<gather.survey3D.worldOrigin3D[0]+p.x3D*h<<','<<gather.survey3D.worldOrigin3D[1]+p.y3D*h<<','<<p.z3D*h<<','
            <<bin.first<<','<<bin.second<<','<<gather.firstSampleTime3D<<','<<gather.sampleIntervalUs3D*1e-6<<','<<gather.sampleCount3D<<'\n';
    }
    csv.close(); if(!csv) throw std::runtime_error("Cannot finalize geometry manifest.");
    s.message3D="3D gather exported: "+pathToUtf8(directory);
}

// Freeze observation geometry and allocate a bounded shot gather before resetting the wavefield.
// Millisecond mode has its own output clock; stride mode retains accepted-step samples.
int receiverSamplingStride3D(const Seis3D_State& s,double dt) {
    if(!s.sampleIntervalInMs3D) return s.samplingStride3D;
    receiverSamplingIntervalUs3D(s,dt);
    if(!std::isfinite(dt)||dt<=0) throw std::invalid_argument("Invalid solver time step.");
    return int((std::clamp)(std::round(s.requestedSampleIntervalMs3D/(dt*1000.0)),1.0,10000.0));
}
int receiverSamplingIntervalUs3D(const Seis3D_State& s,double dt) {
    const double us=s.sampleIntervalInMs3D?s.requestedSampleIntervalMs3D*1000.0:dt*1e6*s.samplingStride3D;
    if(!std::isfinite(us)||us<1||us>maxSegyIntervalUs3D)
        throw std::invalid_argument("Sampling interval must be 0.001 .. 32.767 ms.");
    return int(std::llround(us));
}
// Velocity states are staggered by half a solver step. Interpolate on the physical time axis.
void appendGatherSamples3D(Seis3D_State& s) {
    auto& g=s.gather3D;
    if(!g.uniformSampling3D&&s.step3D%std::uint64_t(g.stride3D)!=0) return;
    const auto values=sampleReceivers3D(s.gpu3D,s.context3D);
    const std::size_t count=g.receivers3D.size()*3;
    if(!g.uniformSampling3D) {
        std::copy_n(values,count,g.samples3D.data()+std::size_t(g.sampleCount3D++)*count);
        return;
    }
    const double now=(double(s.step3D)-0.5)*s.context3D.dt3D;
    while(g.sampleCount3D<g.targetSamples3D) {
        const double target=g.firstSampleTime3D+double(g.sampleCount3D)*g.sampleIntervalUs3D*1e-6;
        if(target>now+1e-12) break;
        const double alpha=(std::clamp)((target-g.previousSampleTime3D)/(now-g.previousSampleTime3D),0.0,1.0);
        float* output=g.samples3D.data()+std::size_t(g.sampleCount3D)*count;
        for(std::size_t i=0;i<count;++i) output[i]=float(g.previousReceiverSamples3D[i]*(1-alpha)+values[i]*alpha);
        ++g.sampleCount3D;
    }
    g.previousReceiverSamples3D.assign(values,values+count);
    g.previousSampleTime3D=now;
}

void startGather3D(Seis3D_State& s,int shotId) {
    const auto& c=s.context3D; const auto& source=s.source3D;
    if(source.x3D<1||source.x3D>=c.nx3D-1||source.y3D<1||source.y3D>=c.ny3D-1
        ||source.z3D<2||source.z3D>=c.nz3D-1||!std::isfinite(source.amplitude3D))
        throw std::runtime_error("Place the source inside the model, at depth >=2, before capture.");
    for(float direction:source.direction3D) if(!std::isfinite(direction)) throw std::runtime_error("Invalid source direction.");
    configureReceivers3D(s);
    s.samplingStride3D=receiverSamplingStride3D(s,s.context3D.dt3D);
    if(s.targetSamples3D<1||s.targetSamples3D>maxSegySamples3D||s.samplingStride3D<1||s.samplingStride3D>10000)
        throw std::runtime_error("Gather sample count must be 1..32767 with stride 1..10000.");
    for(int a=0;a<2;++a) {
        if(!std::isfinite(s.survey3D.cmpBinSize3D[a])||s.survey3D.cmpBinSize3D[a]<=0
            ||!std::isfinite(s.survey3D.worldOrigin3D[a])) throw std::runtime_error("Invalid CMP bin size or world origin.");
    }
    const auto interval=receiverSamplingIntervalUs3D(s,s.context3D.dt3D);
    if(interval>maxSegyIntervalUs3D) throw std::runtime_error("SEG-Y rev1 sample interval must not exceed 32767 us.");
    const std::size_t values=std::size_t(s.targetSamples3D)*s.receivers3D.size()*3;
    if(values*sizeof(float)>std::size_t(512)*1024*1024) throw std::runtime_error("Shot gather exceeds the 512 MiB CPU budget.");
    applySourceFrequency3D(s);
    Gather3D gather; gather.context3D=s.context3D; gather.source3D=s.source3D; gather.survey3D=s.survey3D;
    gather.receivers3D=s.receivers3D; gather.targetSamples3D=s.targetSamples3D; gather.stride3D=s.samplingStride3D;
    gather.shotId3D=shotId; gather.sampleIntervalUs3D=int(interval);
    gather.uniformSampling3D=s.sampleIntervalInMs3D;
    gather.firstSampleTime3D=gather.uniformSampling3D?0.0:(gather.stride3D-0.5)*s.context3D.dt3D;
    gather.previousSampleTime3D=-0.5*s.context3D.dt3D;
    if(gather.uniformSampling3D) gather.previousReceiverSamples3D.assign(gather.receivers3D.size()*3,0.0f);
    double maximumCoordinate=0;
    const double h=gather.context3D.spacing3D;
    const double sx=gather.survey3D.worldOrigin3D[0]+gather.source3D.x3D*h;
    const double sy=gather.survey3D.worldOrigin3D[1]+gather.source3D.y3D*h;
    maximumCoordinate=(std::max)(std::abs(sx),std::abs(sy));
    coordinateMm3D(gather.source3D.z3D*h);
    for(const auto& p:gather.receivers3D) {
        maximumCoordinate=(std::max)({maximumCoordinate,std::abs(gather.survey3D.worldOrigin3D[0]+p.x3D*h),
            std::abs(gather.survey3D.worldOrigin3D[1]+p.y3D*h)});
        coordinateMm3D(-p.z3D*h); cmpBin3D(gather,p);
    }
    int coordinateFactor=1000;
    while(coordinateFactor>1&&maximumCoordinate*coordinateFactor>2147483646.0) coordinateFactor/=10;
    if(!std::isfinite(maximumCoordinate)||maximumCoordinate*coordinateFactor>2147483646.0)
        throw std::runtime_error("World coordinates exceed the SEG-Y integer range.");
    gather.coordinateScalar3D=coordinateFactor==1?1:-coordinateFactor;
    gather.samples3D.resize(values);
    restartSimulation3D(s,false);s.gather3D=std::move(gather);
    s.tracePreviewCache3D.entries3D.clear();
    s.recording3D=true; s.running3D=true; s.dirty3D=true; s.message3D="Capturing frozen-geometry 3D shot gather.";
}

// Position a shot in a rectangular multi-line source spread after validating the complete spread.
void positionSequenceShot3D(Seis3D_State& s) {
    const auto& q=s.survey3D; const auto& c=s.context3D;
    if(q.shotCountX3D<1||q.shotCountY3D<1||q.shotCountX3D>100||q.shotCountY3D>100||q.shotSpacing3D[0]<1||q.shotSpacing3D[1]<1)
        throw std::runtime_error("Invalid 3D source spread.");
    const auto lastX=std::int64_t(q.shotOrigin3D[0])+std::int64_t(q.shotCountX3D-1)*q.shotSpacing3D[0];
    const auto lastY=std::int64_t(q.shotOrigin3D[1])+std::int64_t(q.shotCountY3D-1)*q.shotSpacing3D[1];
    if(q.shotOrigin3D[0]<1||q.shotOrigin3D[1]<1||lastX>=c.nx3D-1||lastY>=c.ny3D-1
        ||q.shotOrigin3D[2]<2||q.shotOrigin3D[2]>=c.nz3D-1) throw std::runtime_error("Source spread exceeds the 3D model.");
    s.source3D.x3D=q.shotOrigin3D[0]+(s.sequenceIndex3D%q.shotCountX3D)*q.shotSpacing3D[0];
    s.source3D.y3D=q.shotOrigin3D[1]+(s.sequenceIndex3D/q.shotCountX3D)*q.shotSpacing3D[1]; s.source3D.z3D=q.shotOrigin3D[2];
}


} // namespace Seis3D::Detail3D
