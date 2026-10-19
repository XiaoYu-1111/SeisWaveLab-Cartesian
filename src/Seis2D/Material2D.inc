// Private 2D implementation, compiled only through Simulation2D.cpp.
// Scan material arrays once after edits to cache velocity ranges and interface hints.
inline void recalculateCachedModelMetrics() {
    if (simulationContext2D.total_grid <= 0) return;

    float max_p = -1e20f, min_p = 1e20f;
    float max_s = -1e20f, min_s = 1e20f;
    float max_r = -1e20f, min_r = 1e20f;

    for (int i = 0; i < simulationContext2D.total_grid; ++i) {
        float rho = simulationContext2D.rho[i];
        float vp = 0.0f;
        float vs = 0.0f;
        if (rho > 0.0f) {
            vp = sqrtf(simulationContext2D.lambda2mu[i] / rho);
            vs = sqrtf(simulationContext2D.mu[i] / rho);
        }

        if (vp > 350.0f) {
            if (vp > max_p) max_p = vp;
            if (vp < min_p) min_p = vp;
            if (vs > max_s) max_s = vs;
            if (vs < min_s) min_s = vs;
            if (rho > max_r) max_r = rho;
            if (rho < min_r) min_r = rho;
        }
    }

    if (max_p == -1e20f) {
        max_p = 2000.0f; min_p = 2000.0f;
        max_s = 1400.0f; min_s = 1400.0f;
        max_r = 2000.0f; min_r = 2000.0f;
    }

    cachedMaxVp2D = max_p;
    cachedMinVp2D = min_p;
    cachedMaxVs2D = max_s;
    cachedMinVs2D = min_s;
    cachedMaxRho2D = max_r;
    cachedMinRho2D = min_r;

    size_t estimated_bytes = size_t(simulationContext2D.NX+2*domainPad2D())*(simulationContext2D.NZ+2*domainPad2D())*sizeof(float)*28;
    cachedVramMb2D = static_cast<float>(estimated_bytes) / (1024.0f * 1024.0f);
}

// Rebuilds the visualization texture from the current model and graphics context.
inline void updateModelTexture() {
    auto expanded=extendedContext2D(::Seis2D::Legacy2D::simulationContext2D);
    auto& simulationContext2D=expanded;
    if (modelTex2D == 0) {
        glGenTextures(1, &modelTex2D);
    }

    std::vector<float> temp_data(simulationContext2D.total_grid * 4, 0.0f);

    for (int i = 0; i < simulationContext2D.total_grid; ++i) {
        float rho = simulationContext2D.rho[i];
        float vp = 0.0f;
        float vs = 0.0f;
        if (rho > 0.0f) {
            vp = sqrtf(simulationContext2D.lambda2mu[i] / rho);
            vs = sqrtf(simulationContext2D.mu[i] / rho);
        }

        temp_data[i * 4 + 0] = vp / 6000.0f;
        temp_data[i * 4 + 1] = vs / 3500.0f;
        temp_data[i * 4 + 2] = rho / 3000.0f;
        temp_data[i * 4 + 3] = 1.0f;
    }

    glBindTexture(GL_TEXTURE_2D, modelTex2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, simulationContext2D.NX, simulationContext2D.NZ, 0, GL_RGBA, GL_FLOAT, temp_data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Uploads current material fields to the simulation's device buffers.
inline void uploadMaterialToGpu() {
    auto expanded=extendedContext2D(::Seis2D::Legacy2D::simulationContext2D);
    auto& simulationContext2D=expanded;
    if (simulationContext2D.total_grid <= 0) return;

    size_t bytes = simulationContext2D.total_grid * sizeof(float);

    CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_rho, simulationContext2D.rho.data(), bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_mu, simulationContext2D.mu.data(), bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_lambda, simulationContext2D.lambda.data(), bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_lambda2mu, simulationContext2D.lambda2mu.data(), bytes, cudaMemcpyHostToDevice));

    if (gpuData2D.flag_type == 2) {
        std::vector<float> mu_x(simulationContext2D.total_grid, 0.0f), lambda_x(simulationContext2D.total_grid, 0.0f), l2m_x(simulationContext2D.total_grid, 0.0f);
        std::vector<float> mu_z(simulationContext2D.total_grid, 0.0f), rho_xz(simulationContext2D.total_grid, 0.0f), rho_orig(simulationContext2D.total_grid, 0.0f);
        for (int i = 0; i < simulationContext2D.NZ; ++i) {
            for (int j = 0; j < simulationContext2D.NX; ++j) {
                int k = i * simulationContext2D.NX + j;
                rho_orig[k] = simulationContext2D.rho[k];
                int k_next_x = (j < simulationContext2D.NX - 1) ? k + 1 : k;
                int k_next_z = (i < simulationContext2D.NZ - 1) ? k + simulationContext2D.NX : k;
                mu_x[k] = 0.5f * (simulationContext2D.mu[k] + simulationContext2D.mu[k_next_x]);
                lambda_x[k] = 0.5f * (simulationContext2D.lambda[k] + simulationContext2D.lambda[k_next_x]);
                l2m_x[k] = 0.5f * (simulationContext2D.lambda2mu[k] + simulationContext2D.lambda2mu[k_next_x]);
                mu_z[k] = 0.5f * (simulationContext2D.mu[k] + simulationContext2D.mu[k_next_z]);
                int k_dr = (i < simulationContext2D.NZ - 1 && j < simulationContext2D.NX - 1) ? k + simulationContext2D.NX + 1 : k;
                rho_xz[k] = 0.25f * (simulationContext2D.rho[k] + simulationContext2D.rho[k_next_x] + simulationContext2D.rho[k_next_z] + simulationContext2D.rho[k_dr]);
            }
        }
        CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_mu_x_flat, mu_x.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_lambda_x_flat, lambda_x.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_lambda2mu_x_flat, l2m_x.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_mu_z_flat, mu_z.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_rho_x_z_flat, rho_xz.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_rho_orig_flat, rho_orig.data(), bytes, cudaMemcpyHostToDevice));
    }

    if (gpuData2D.flag_type == 3) {
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
        CUDA_CHECK(solverCopy2D(gpuData2D,gpuData2D.d_dp_flat, simulationContext2D.dp_flat.data(), simulationContext2D.NX * sizeof(float), cudaMemcpyHostToDevice));
    }

    recalculateCachedModelMetrics();

    updateModelTexture();
}
