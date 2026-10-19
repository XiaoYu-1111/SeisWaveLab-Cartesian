// Private 2D implementation, compiled only through Simulation2D.cpp.
// Validate time sampling and generate the finite Ricker source series.
void generateRickerWavelet(std::vector<float>& wavelet, int nt, float dt, float f0, float t0) {
    if (nt <= 0 || !std::isfinite(dt) || dt <= 0.0f || !std::isfinite(f0) || f0 <= 0.0f || !std::isfinite(t0)) {
        throw std::invalid_argument("Invalid Ricker wavelet sampling or frequency");
    }
    wavelet.resize(nt);
    if(&wavelet==&simulationContext2D.wavelet) {appliedSourceFrequency2D=f0;appliedSourceDelay2D=t0;}
    for (int i = 0; i < nt; ++i) {
        float t = i * dt - t0;
        float pi2_f0 = M_PI * M_PI * f0 * f0;
        wavelet[i] = (1.0f - 2.0f * pi2_f0 * t * t) * expf(-pi2_f0 * t * t);
    }
}

/** Clamps the time step to the stability limit derived from the current grid and maximum velocity. */
inline void autoAlignTimeStep() {
    if (simulationContext2D.total_grid <= 0) return;

    float max_vp = 0.0f;
    for (int k = 0; k < simulationContext2D.total_grid; ++k) {
        float vp = sqrtf(simulationContext2D.lambda2mu[k] / simulationContext2D.rho[k]);
        if (vp > max_vp) max_vp = vp;
    }
    if (max_vp <= 350.0f) max_vp = 2000.0f;

    simulationContext2D.dt = kSafeCourant2d * simulationContext2D.h / max_vp;
    gpuData2D.dt = simulationContext2D.dt;
    gpuData2D.inv_dt = 1.0f / simulationContext2D.dt;
    tempDt2D = simulationContext2D.dt;

    generateRickerWavelet(simulationContext2D.wavelet, simulationContext2D.nt, simulationContext2D.dt, sourceFrequency2D, sourceDelay2D);
}

/** Rebuilds staggered-grid PML damping profiles from the current model and boundary settings. */
inline void updatePmlDampingArrays() {
    if(simulationContext2D.externalPml) {
        simulationContext2D.dx.assign(simulationContext2D.NX,0);simulationContext2D.dx_half=simulationContext2D.dx;
        simulationContext2D.dz.assign(simulationContext2D.NZ,0);simulationContext2D.dz_half=simulationContext2D.dz;return;
    }
    validateBoundaryGeometry(simulationContext2D.NX, simulationContext2D.NZ, simulationContext2D.npml, simulationContext2D.flag_type == 3 && simulationContext2D.upFlag);
    float max_vp = 0.0f;
    for (int k = 0; k < simulationContext2D.total_grid; ++k) {
        float vp = sqrtf(simulationContext2D.lambda2mu[k] / simulationContext2D.rho[k]);
        if (vp > max_vp) max_vp = vp;
    }
    if (max_vp <= 350.0f) max_vp = 2000.0f;

    simulationContext2D.dx.assign(simulationContext2D.NX, 0.0f);
    simulationContext2D.dx_half.assign(simulationContext2D.NX, 0.0f);
    simulationContext2D.dz.assign(simulationContext2D.NZ, 0.0f);
    simulationContext2D.dz_half.assign(simulationContext2D.NZ, 0.0f);

    if (simulationContext2D.npml > 0) {
        float L = simulationContext2D.npml * simulationContext2D.h;
        float d_max = (3.0f * max_vp * 16.12f * 3.5f) / (2.0f * L);
        float pml_power = 2.5f;

        for (int i = 0; i < simulationContext2D.npml; ++i) {
            float x_thick = (simulationContext2D.npml - i) / (float)simulationContext2D.npml;
            float x_thick_half = (simulationContext2D.npml - (i + 0.5f)) / (float)simulationContext2D.npml;
            if (x_thick_half < 0.0f) x_thick_half = 0.0f;

            float val = d_max * powf(x_thick, pml_power);
            float val_half = d_max * powf(x_thick_half, pml_power);

            simulationContext2D.dx[i] = val;
            simulationContext2D.dx[simulationContext2D.NX - 1 - i] = val;
            simulationContext2D.dx_half[i] = val_half;
            simulationContext2D.dx_half[simulationContext2D.NX - 1 - i] = val_half;
        }

        for (int i = 0; i < simulationContext2D.npml; ++i) {
            float z_thick = (simulationContext2D.npml - i) / (float)simulationContext2D.npml;
            float z_thick_half = (simulationContext2D.npml - (i + 0.5f)) / (float)simulationContext2D.npml;
            if (z_thick_half < 0.0f) z_thick_half = 0.0f;

            float val = d_max * powf(z_thick, pml_power);
            float val_half = d_max * powf(z_thick_half, pml_power);

            simulationContext2D.dz[i] = val;
            simulationContext2D.dz[simulationContext2D.NZ - 1 - i] = val;
            simulationContext2D.dz_half[i] = val_half;
            simulationContext2D.dz_half[simulationContext2D.NZ - 1 - i] = val_half;
        }
    }
}

// Construct material, field, source and damping arrays after validating grid allocation limits.
void setupTestContext(SimulationContext& simulationContext2D, const Parameters& parameters2D) {
    simulationContext2D.externalPml=true;
    simulationContext2D.NX = parameters2D.model.xnum;
    simulationContext2D.NZ = parameters2D.model.znum;
    simulationContext2D.total_grid = static_cast<int>(checkedGridElementCount(simulationContext2D.NX, simulationContext2D.NZ));
    simulationContext2D.dt = parameters2D.FDM.dt;
    simulationContext2D.nt = static_cast<int>(parameters2D.FDM.nt);
    simulationContext2D.npml = static_cast<int>(parameters2D.FDM.npml);

    if (simulationContext2D.flag_type == 0) {
        simulationContext2D.flag_type = 3;
    }

    simulationContext2D.upFlag = (parameters2D.FDM.upFlag > 0.5f);

    float h = parameters2D.model.dx;
    if (h <= 0.0f) h = 1.0f;
    simulationContext2D.h = h;

    tempDx2D = h;
    tempDt2D = simulationContext2D.dt;
    tempNt2D = simulationContext2D.nt;
    tempPml2D = simulationContext2D.npml;

    simulationContext2D.c1_h = static_cast<float>(kStaggeredDerivativeWeights[0]) / h;
    simulationContext2D.c2_h = static_cast<float>(kStaggeredDerivativeWeights[1]) / h;
    simulationContext2D.c3_h = static_cast<float>(kStaggeredDerivativeWeights[2]) / h;
    simulationContext2D.c4_h = static_cast<float>(kStaggeredDerivativeWeights[3]) / h;

    float mu_val = editDensity2D * editVs2D * editVs2D;
    float lambda2mu_val = editDensity2D * editVp2D * editVp2D;
    float lambda_val = lambda2mu_val - 2.0f * mu_val;

    simulationContext2D.rho.assign(simulationContext2D.total_grid, editDensity2D);
    simulationContext2D.mu.assign(simulationContext2D.total_grid, mu_val);
    simulationContext2D.lambda.assign(simulationContext2D.total_grid, lambda_val);
    simulationContext2D.lambda2mu.assign(simulationContext2D.total_grid, lambda2mu_val);

    updatePmlDampingArrays();

    {
        simulationContext2D.dp_flat.assign(simulationContext2D.NX, 0.0f);
        int fs_idx = simulationContext2D.npml;
        for (int j = 0; j < simulationContext2D.NX; ++j) {
            int k = fs_idx * simulationContext2D.NX + j;
            if (k < simulationContext2D.total_grid && simulationContext2D.lambda2mu[k] > 0.0f) {
                float l2m = simulationContext2D.lambda2mu[k];
                float lam = simulationContext2D.lambda[k];
                simulationContext2D.dp_flat[j] = (l2m * l2m - lam * lam) / l2m;
            }
        }
    }

    simulationContext2D.src_z_idx = simulationContext2D.NZ / 4;
    simulationContext2D.src_idx = simulationContext2D.src_z_idx * simulationContext2D.NX + (simulationContext2D.NX / 2);
    simulationContext2D.src_angle = parameters2D.FDM.angle;
    simulationContext2D.src_type = editSourceType2D;
    simulationContext2D.src_amp = editAmp2D;
    generateRickerWavelet(simulationContext2D.wavelet, simulationContext2D.nt, simulationContext2D.dt, parameters2D.FDM.f0, parameters2D.FDM.t0);

    simulationContext2D.num_rcv = std::min(100, simulationContext2D.NX - 2 * kStencilRadius);
    simulationContext2D.rcv_grid_idx.resize(simulationContext2D.num_rcv);
    int rcv_z = std::min(simulationContext2D.NZ / 2 + 30, simulationContext2D.NZ - kStencilRadius - 1);
    int step = std::max(1, (simulationContext2D.NX - 2 * kStencilRadius) / simulationContext2D.num_rcv);
    for (int r = 0; r < simulationContext2D.num_rcv; ++r) {
        simulationContext2D.rcv_grid_idx[r] = rcv_z * simulationContext2D.NX + kStencilRadius + (r * step);
    }

    simulationContext2D.record_vx.assign(static_cast<size_t>(simulationContext2D.num_rcv) * simulationContext2D.nt, 0.0f);
    simulationContext2D.record_vz.assign(static_cast<size_t>(simulationContext2D.num_rcv) * simulationContext2D.nt, 0.0f);

    recalculateCachedModelMetrics();
}

// Apply material at a top-origin grid cell, matching canvas picking and solver indexing.
inline void setGridMaterial2D(int x, int z, float vp, float vs, float rho) {
    if(x<0||x>=simulationContext2D.NX||z<0||z>=simulationContext2D.NZ) return;
    const int k=z*simulationContext2D.NX+x;
    simulationContext2D.rho[k] = rho;
    simulationContext2D.mu[k] = rho * vs * vs;
    simulationContext2D.lambda2mu[k] = rho * vp * vp;
    simulationContext2D.lambda[k] = simulationContext2D.lambda2mu[k] - 2.0f * simulationContext2D.mu[k];
}

// Preserve the bottom-origin convention of legacy synthetic model generators.
inline void setMaterialAt(int x,int y,float vp,float vs,float rho) {
    setGridMaterial2D(x,simulationContext2D.NZ-1-y,vp,vs,rho);
}

/** Fills the current 2D material grid with a selected built-in geological test scenario. */
