#pragma once
#include "Common.h"
#include "SolverNumerics.h"
#include <algorithm>
#include <cmath>

// Host/model/export coordinates stay physical. Only GPU allocation uses this extended copy.
inline SimulationContext extendedContext2D(const SimulationContext& physical) {
    if(!physical.externalPml) return physical;
    const auto count=checkedGridElementCount(physical.NX,physical.NZ);
    if(physical.total_grid!=count||physical.rho.size()!=count||physical.mu.size()!=count||physical.lambda.size()!=count||physical.lambda2mu.size()!=count||physical.npml<0||physical.npml>10000)
        throw std::invalid_argument("Invalid physical model arrays or external boundary thickness");
    if(physical.src_idx<0||physical.src_idx>=physical.total_grid||physical.src_z_idx<0||physical.src_z_idx>=physical.NZ)
        throw std::invalid_argument("Source outside physical model");
    for(int i:physical.rcv_grid_idx) if(i<0||i>=physical.total_grid) throw std::invalid_argument("Receiver outside physical model");
    if(physical.NX>std::numeric_limits<int>::max()-2*std::max(physical.npml,kStencilRadius)||physical.NZ>std::numeric_limits<int>::max()-2*std::max(physical.npml,kStencilRadius))
        throw std::overflow_error("Extended grid dimensions overflow");
    SimulationContext c=physical;c.externalPml=false;
    const int pad=std::max(physical.npml,kStencilRadius);
    c.NX=physical.NX+2*pad;c.NZ=physical.NZ+2*pad;
    c.total_grid=int(checkedGridElementCount(c.NX,c.NZ));
    const auto extend=[&](const std::vector<float>& field) {
        std::vector<float> result(c.total_grid);
        for(int z=0;z<c.NZ;++z) for(int x=0;x<c.NX;++x)
            result[z*c.NX+x]=field[std::clamp(z-pad,0,physical.NZ-1)*physical.NX+std::clamp(x-pad,0,physical.NX-1)];
        return result;
    };
    c.rho=extend(physical.rho);c.mu=extend(physical.mu);
    c.lambda=extend(physical.lambda);c.lambda2mu=extend(physical.lambda2mu);
    c.dx.assign(c.NX,0);c.dx_half.assign(c.NX,0);c.dz.assign(c.NZ,0);c.dz_half.assign(c.NZ,0);
    float maxVp=0;for(int i=0;i<physical.total_grid;++i) maxVp=std::max(maxVp,std::sqrt(physical.lambda2mu[i]/physical.rho[i]));
    if(c.npml>0) {
        const float maximum=3*maxVp*16.12f*3.5f/(2*c.npml*c.h);
        for(int i=0;i<c.npml;++i) {
            const float full=maximum*std::pow(float(c.npml-i)/c.npml,2.5f);
            const float half=maximum*std::pow(float(c.npml-i-.5f)/c.npml,2.5f);
            c.dx[i]=c.dx[c.NX-1-i]=full;c.dx_half[i]=c.dx_half[c.NX-1-i]=half;
            c.dz[c.NZ-1-i]=full;c.dz_half[c.NZ-1-i]=half;
            if(!(c.flag_type==3&&c.upFlag)) {c.dz[i]=full;c.dz_half[i]=half;}
        }
    }
    c.dp_flat.resize(c.NX);
    for(int x=0;x<c.NX;++x) {const int k=pad*c.NX+x;const float a=c.lambda2mu[k],b=c.lambda[k];c.dp_flat[x]=(a*a-b*b)/a;}
    const auto index=[&](int i){return (i/physical.NX+pad)*c.NX+i%physical.NX+pad;};
    c.src_idx=index(physical.src_idx);c.src_z_idx=physical.src_z_idx+pad;
    for(int& i:c.rcv_grid_idx) i=index(i);
    return c;
}
