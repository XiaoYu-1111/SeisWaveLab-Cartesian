#pragma once
#include "Cuda_Check.cuh"
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif
#ifdef __CUDACC__
#define SEIS_CELL __host__ __device__ inline
#else
#define SEIS_CELL inline
#endif
namespace Seis2DCells {
SEIS_CELL void updateType1Stress(GPUSimData g,int i,int j) {

    if (i >= 4 && i < g.NZ - 4 && j >= 4 && j < g.NX - 4) {
        int k = i * g.NX + j;
        float inv_dt = g.inv_dt;

        float d_z = g.d_dz[i];
        float d_z_h = g.d_dz_half[i];
        float d_x = g.d_dx[j];
        float d_x_h = g.d_dx_half[j];

        float dvx_dx = g.c1_h * (g.d_vx_1[k + 1] + g.d_vx_2[k + 1] - g.d_vx_1[k] - g.d_vx_2[k])
            + g.c2_h * (g.d_vx_1[k + 2] + g.d_vx_2[k + 2] - g.d_vx_1[k - 1] - g.d_vx_2[k - 1])
            + g.c3_h * (g.d_vx_1[k + 3] + g.d_vx_2[k + 3] - g.d_vx_1[k - 2] - g.d_vx_2[k - 2])
            + g.c4_h * (g.d_vx_1[k + 4] + g.d_vx_2[k + 4] - g.d_vx_1[k - 3] - g.d_vx_2[k - 3]);

        float dvz_dz = g.c1_h * (g.d_vz_1[k] + g.d_vz_2[k] - g.d_vz_1[k - g.NX] - g.d_vz_2[k - g.NX])
            + g.c2_h * (g.d_vz_1[k + g.NX] + g.d_vz_2[k + g.NX] - g.d_vz_1[k - 2 * g.NX] - g.d_vz_2[k - 2 * g.NX])
            + g.c3_h * (g.d_vz_1[k + 2 * g.NX] + g.d_vz_2[k + 2 * g.NX] - g.d_vz_1[k - 3 * g.NX] - g.d_vz_2[k - 3 * g.NX])
            + g.c4_h * (g.d_vz_1[k + 3 * g.NX] + g.d_vz_2[k + 3 * g.NX] - g.d_vz_1[k - 4 * g.NX] - g.d_vz_2[k - 4 * g.NX]);

        g.d_sxx_1[k] = (g.d_sxx_1[k] * (inv_dt - d_x_h) + 0.5f * (g.d_lambda2mu[k] + g.d_lambda2mu[k + 1]) * dvx_dx) / (inv_dt + d_x_h);
        g.d_sxx_2[k] = (g.d_sxx_2[k] * (inv_dt - d_z) + 0.5f * (g.d_lambda[k] + g.d_lambda[k + 1]) * dvz_dz) / (inv_dt + d_z);
        g.d_szz_1[k] = (g.d_szz_1[k] * (inv_dt - d_x_h) + 0.5f * (g.d_lambda[k] + g.d_lambda[k + 1]) * dvx_dx) / (inv_dt + d_x_h);
        g.d_szz_2[k] = (g.d_szz_2[k] * (inv_dt - d_z) + 0.5f * (g.d_lambda2mu[k] + g.d_lambda2mu[k + 1]) * dvz_dz) / (inv_dt + d_z);

        float dvz_dx = g.c1_h * (g.d_vz_1[k] + g.d_vz_2[k] - g.d_vz_1[k - 1] - g.d_vz_2[k - 1])
            + g.c2_h * (g.d_vz_1[k + 1] + g.d_vz_2[k + 1] - g.d_vz_1[k - 2] - g.d_vz_2[k - 2])
            + g.c3_h * (g.d_vz_1[k + 2] + g.d_vz_2[k + 2] - g.d_vz_1[k - 3] - g.d_vz_2[k - 3])
            + g.c4_h * (g.d_vz_1[k + 3] + g.d_vz_2[k + 3] - g.d_vz_1[k - 4] - g.d_vz_2[k - 4]);

        float dvx_dz = g.c1_h * (g.d_vx_1[k + g.NX] + g.d_vx_2[k + g.NX] - g.d_vx_1[k] - g.d_vx_2[k])
            + g.c2_h * (g.d_vx_1[k + 2 * g.NX] + g.d_vx_2[k + 2 * g.NX] - g.d_vx_1[k - g.NX] - g.d_vx_2[k - g.NX])
            + g.c3_h * (g.d_vx_1[k + 3 * g.NX] + g.d_vx_2[k + 3 * g.NX] - g.d_vx_1[k - 2 * g.NX] - g.d_vx_2[k - 2 * g.NX])
            + g.c4_h * (g.d_vx_1[k + 4 * g.NX] + g.d_vx_2[k + 4 * g.NX] - g.d_vx_1[k - 3 * g.NX] - g.d_vx_2[k - 3 * g.NX]);

        g.d_sxz_1[k] = (g.d_sxz_1[k] * (inv_dt - d_x) + 0.5f * (g.d_mu[k] + g.d_mu[k + g.NX]) * dvz_dx) / (inv_dt + d_x);
        g.d_sxz_2[k] = (g.d_sxz_2[k] * (inv_dt - d_z_h) + 0.5f * (g.d_mu[k] + g.d_mu[k + g.NX]) * dvx_dz) / (inv_dt + d_z_h);
    }
}
SEIS_CELL void updateType1Velocity(GPUSimData g,int i,int j) {

    if (i >= 4 && i < g.NZ - 4 && j >= 4 && j < g.NX - 4) {
        int k = i * g.NX + j;
        float inv_dt = g.inv_dt;

        float d_z = g.d_dz[i];
        float d_z_h = g.d_dz_half[i]; // 已成功修复：重新引入被删除的 d_z_h
        float d_x = g.d_dx[j];
        float d_x_h = g.d_dx_half[j];
        float cur_rho = g.d_rho[k];

        float dsigxx_dx = g.c1_h * (g.d_sxx_1[k] + g.d_sxx_2[k] - g.d_sxx_1[k - 1] - g.d_sxx_2[k - 1])
            + g.c2_h * (g.d_sxx_1[k + 1] + g.d_sxx_2[k + 1] - g.d_sxx_1[k - 2] - g.d_sxx_2[k - 2])
            + g.c3_h * (g.d_sxx_1[k + 2] + g.d_sxx_2[k + 2] - g.d_sxx_1[k - 3] - g.d_sxx_2[k - 3])
            + g.c4_h * (g.d_sxx_1[k + 3] + g.d_sxx_2[k + 3] - g.d_sxx_1[k - 4] - g.d_sxx_2[k - 4]);

        float dsigxz_dz = g.c1_h * (g.d_sxz_1[k] + g.d_sxz_2[k] - g.d_sxz_1[k - g.NX] - g.d_sxz_2[k - g.NX])
            + g.c2_h * (g.d_sxz_1[k + g.NX] + g.d_sxz_2[k + g.NX] - g.d_sxz_1[k - 2 * g.NX] - g.d_sxz_2[k - 2 * g.NX])
            + g.c3_h * (g.d_sxz_1[k + 2 * g.NX] + g.d_sxz_2[k + 2 * g.NX] - g.d_sxz_1[k - 3 * g.NX] - g.d_sxz_2[k - 3 * g.NX])
            + g.c4_h * (g.d_sxz_1[k + 3 * g.NX] + g.d_sxz_2[k + 3 * g.NX] - g.d_sxz_1[k - 4 * g.NX] - g.d_sxz_2[k - 4 * g.NX]);

        g.d_vx_1[k] = (g.d_vx_1[k] * (inv_dt - d_x) + dsigxx_dx / cur_rho) / (inv_dt + d_x);
        g.d_vx_2[k] = (g.d_vx_2[k] * (inv_dt - d_z) + dsigxz_dz / cur_rho) / (inv_dt + d_z);

        float dsigxz_dx = g.c1_h * (g.d_sxz_1[k + 1] + g.d_sxz_2[k + 1] - g.d_sxz_1[k] - g.d_sxz_2[k])
            + g.c2_h * (g.d_sxz_1[k + 2] + g.d_sxz_2[k + 2] - g.d_sxz_1[k - 1] - g.d_sxz_2[k - 1])
            + g.c3_h * (g.d_sxz_1[k + 3] + g.d_sxz_2[k + 3] - g.d_sxz_1[k - 2] - g.d_sxz_2[k - 2])
            + g.c4_h * (g.d_sxz_1[k + 4] + g.d_sxz_2[k + 4] - g.d_sxz_1[k - 3] - g.d_sxz_2[k - 3]);

        float dsigzz_dz = g.c1_h * (g.d_szz_1[k + g.NX] + g.d_szz_2[k + g.NX] - g.d_szz_1[k] - g.d_szz_2[k])
            + g.c2_h * (g.d_szz_1[k + 2 * g.NX] + g.d_szz_2[k + 2 * g.NX] - g.d_szz_1[k - g.NX] - g.d_szz_2[k - g.NX])
            + g.c3_h * (g.d_szz_1[k + 3 * g.NX] + g.d_szz_2[k + 3 * g.NX] - g.d_szz_1[k - 2 * g.NX] - g.d_szz_2[k - 2 * g.NX])
            + g.c4_h * (g.d_szz_1[k + 4 * g.NX] + g.d_szz_2[k + 4 * g.NX] - g.d_szz_1[k - 3 * g.NX] - g.d_szz_2[k - 3 * g.NX]);

        float rho_avg = 0.25f * (g.d_rho[k] + g.d_rho[k + 1] + g.d_rho[k + g.NX] + g.d_rho[k + g.NX + 1]);
        g.d_vz_1[k] = (g.d_vz_1[k] * (inv_dt - d_x_h) + dsigxz_dx / rho_avg) / (inv_dt + d_x_h);
        g.d_vz_2[k] = (g.d_vz_2[k] * (inv_dt - d_z_h) + dsigzz_dz / rho_avg) / (inv_dt + d_z_h);
    }
}
SEIS_CELL void updateType2Stress(GPUSimData g,int i,int j) {

    if (i >= 4 && i < g.NZ - 4 && j >= 4 && j < g.NX - 4) {
        int k = i * g.NX + j;
        int row_dn1 = (i + 1) * g.NX; int row_dn2 = (i + 2) * g.NX; int row_dn3 = (i + 3) * g.NX; int row_dn4 = (i + 4) * g.NX;
        int row_up1 = (i - 1) * g.NX; int row_up2 = (i - 2) * g.NX; int row_up3 = (i - 3) * g.NX; int row_up4 = (i - 4) * g.NX;

        float dvz_dx = g.c1_h * (g.d_vz[k] - g.d_vz[k - 1]) + g.c2_h * (g.d_vz[k + 1] - g.d_vz[k - 2])
            + g.c3_h * (g.d_vz[k + 2] - g.d_vz[k - 3]) + g.c4_h * (g.d_vz[k + 3] - g.d_vz[k - 4]);

        float dvx_dz = g.c1_h * (g.d_vx[row_dn1 + j] - g.d_vx[k]) + g.c2_h * (g.d_vx[row_dn2 + j] - g.d_vx[row_up1 + j])
            + g.c3_h * (g.d_vx[row_dn3 + j] - g.d_vx[row_up2 + j]) + g.c4_h * (g.d_vx[row_dn4 + j] - g.d_vx[row_up3 + j]);

        float dvx_dx = g.c1_h * (g.d_vx[k + 1] - g.d_vx[k]) + g.c2_h * (g.d_vx[k + 2] - g.d_vx[k - 1])
            + g.c3_h * (g.d_vx[k + 3] - g.d_vx[k - 2]) + g.c4_h * (g.d_vx[k + 4] - g.d_vx[k - 3]);

        float dvz_dz = g.c1_h * (g.d_vz[k] - g.d_vz[row_up1 + j]) + g.c2_h * (g.d_vz[row_dn1 + j] - g.d_vz[row_up2 + j])
            + g.c3_h * (g.d_vz[row_dn2 + j] - g.d_vz[row_up3 + j]) + g.c4_h * (g.d_vz[row_dn3 + j] - g.d_vz[row_up4 + j]);

        bool is_pml = (i < g.npml || i >= g.NZ - g.npml || j < g.npml || j >= g.NX - g.npml);

        if (!is_pml) {
            g.d_sigmaxz[k] += g.dt * g.d_mu_z_flat[k] * (dvz_dx + dvx_dz);
            g.d_sigmaxx[k] += g.dt * (g.d_lambda2mu_x_flat[k] * dvx_dx + g.d_lambda_x_flat[k] * dvz_dz);
            g.d_sigmazz[k] += g.dt * (g.d_lambda_x_flat[k] * dvx_dx + g.d_lambda2mu_x_flat[k] * dvz_dz);
        }
        else {
            float inv_dt = g.inv_dt;
            float d_z_val = g.d_dz[i];
            float d_z_half_val = g.d_dz_half[i];
            float d_x_val = g.d_dx[j];
            float d_x_half_val = g.d_dx_half[j];

            g.d_sxz_1[k] = (g.d_sxz_1[k] * (inv_dt - d_x_val) + g.d_mu_z_flat[k] * dvz_dx) / (inv_dt + d_x_val);
            g.d_sxz_2[k] = (g.d_sxz_2[k] * (inv_dt - d_z_half_val) + g.d_mu_z_flat[k] * dvx_dz) / (inv_dt + d_z_half_val);

            g.d_sxx_1[k] = (g.d_sxx_1[k] * (inv_dt - d_x_half_val) + g.d_lambda2mu_x_flat[k] * dvx_dx) / (inv_dt + d_x_half_val);
            g.d_szz_1[k] = (g.d_szz_1[k] * (inv_dt - d_x_half_val) + g.d_lambda_x_flat[k] * dvx_dx) / (inv_dt + d_x_half_val);

            g.d_sxx_2[k] = (g.d_sxx_2[k] * (inv_dt - d_z_val) + g.d_lambda_x_flat[k] * dvz_dz) / (inv_dt + d_z_val);
            g.d_szz_2[k] = (g.d_szz_2[k] * (inv_dt - d_z_val) + g.d_lambda2mu_x_flat[k] * dvz_dz) / (inv_dt + d_z_val);

            g.d_sigmaxz[k] = g.d_sxz_1[k] + g.d_sxz_2[k];
            g.d_sigmaxx[k] = g.d_sxx_1[k] + g.d_sxx_2[k];
            g.d_sigmazz[k] = g.d_szz_1[k] + g.d_szz_2[k];
        }
    }
}
SEIS_CELL void updateType2Velocity(GPUSimData g,int i,int j) {

    if (i >= 4 && i < g.NZ - 4 && j >= 4 && j < g.NX - 4) {
        int k = i * g.NX + j;
        int row_dn1 = (i + 1) * g.NX; int row_dn2 = (i + 2) * g.NX; int row_dn3 = (i + 3) * g.NX; int row_dn4 = (i + 4) * g.NX;
        int row_up1 = (i - 1) * g.NX; int row_up2 = (i - 2) * g.NX; int row_up3 = (i - 3) * g.NX; int row_up4 = (i - 4) * g.NX;

        float dsigxz_dx = g.c1_h * (g.d_sigmaxz[k + 1] - g.d_sigmaxz[k]) + g.c2_h * (g.d_sigmaxz[k + 2] - g.d_sigmaxz[k - 1])
            + g.c3_h * (g.d_sigmaxz[k + 3] - g.d_sigmaxz[k - 2]) + g.c4_h * (g.d_sigmaxz[k + 4] - g.d_sigmaxz[k - 3]);

        float dsigzz_dz = g.c1_h * (g.d_sigmazz[row_dn1 + j] - g.d_sigmazz[k]) + g.c2_h * (g.d_sigmazz[row_dn2 + j] - g.d_sigmazz[row_up1 + j])
            + g.c3_h * (g.d_sigmazz[row_dn3 + j] - g.d_sigmazz[row_up2 + j]) + g.c4_h * (g.d_sigmazz[row_dn4 + j] - g.d_sigmazz[row_up3 + j]);

        float dsigxx_dx = g.c1_h * (g.d_sigmaxx[k] - g.d_sigmaxx[k - 1]) + g.c2_h * (g.d_sigmaxx[k + 1] - g.d_sigmaxx[k - 2])
            + g.c3_h * (g.d_sigmaxx[k + 2] - g.d_sigmaxx[k - 3]) + g.c4_h * (g.d_sigmaxx[k + 3] - g.d_sigmaxx[k - 4]);

        float dsigxz_dz = g.c1_h * (g.d_sigmaxz[k] - g.d_sigmaxz[row_up1 + j]) + g.c2_h * (g.d_sigmaxz[row_dn1 + j] - g.d_sigmaxz[row_up2 + j])
            + g.c3_h * (g.d_sigmaxz[row_dn2 + j] - g.d_sigmaxz[row_up3 + j]) + g.c4_h * (g.d_sigmaxz[row_dn3 + j] - g.d_sigmaxz[row_up4 + j]);

        bool is_pml = (i < g.npml || i >= g.NZ - g.npml || j < g.npml || j >= g.NX - g.npml);

        if (!is_pml) {
            g.d_vz[k] += g.dt * (dsigxz_dx + dsigzz_dz) / g.d_rho_x_z_flat[k];
            g.d_vx[k] += g.dt * (dsigxx_dx + dsigxz_dz) / g.d_rho_orig_flat[k];
        }
        else {
            float inv_dt = g.inv_dt;
            float d_z_val = g.d_dz[i];
            float d_z_half_val = g.d_dz_half[i];
            float d_x_val = g.d_dx[j];
            float d_x_half_val = g.d_dx_half[j];

            g.d_vz_1[k] = (g.d_vz_1[k] * (inv_dt - d_x_half_val) + dsigxz_dx / g.d_rho_x_z_flat[k]) / (inv_dt + d_x_half_val);
            g.d_vz_2[k] = (g.d_vz_2[k] * (inv_dt - d_z_half_val) + dsigzz_dz / g.d_rho_x_z_flat[k]) / (inv_dt + d_z_half_val);

            g.d_vx_1[k] = (g.d_vx_1[k] * (inv_dt - d_x_val) + dsigxx_dx / g.d_rho_orig_flat[k]) / (inv_dt + d_x_val);
            g.d_vx_2[k] = (g.d_vx_2[k] * (inv_dt - d_z_val) + dsigxz_dz / g.d_rho_orig_flat[k]) / (inv_dt + d_z_val);

            g.d_vx[k] = g.d_vx_1[k] + g.d_vx_2[k];
            g.d_vz[k] = g.d_vz_1[k] + g.d_vz_2[k];
        }
    }
}
SEIS_CELL void updateType3Stress(GPUSimData g,int i,int j) {

    if (i >= g.fs_idx && i < g.NZ - 4 && j >= 4 && j < g.NX - 4) {
        int k = i * g.NX + j;
        int row_dn1 = (i + 1) * g.NX; int row_dn2 = (i + 2) * g.NX;
        int row_dn3 = (i + 3) * g.NX; int row_dn4 = (i + 4) * g.NX;
        int row_up1 = (i - 1) * g.NX; int row_up2 = (i - 2) * g.NX; int row_up3 = (i - 3) * g.NX; int row_up4 = (i - 4) * g.NX;

        float d_z_val = (i <= g.fs_idx + 5 && g.upFlag) ? 0.0f : g.d_dz[i];
        float d_z_half_val = (i <= g.fs_idx + 5 && g.upFlag) ? 0.0f : g.d_dz_half[i];
        float d_x_val = g.d_dx[j];
        float d_x_half_val = g.d_dx_half[j];
        float inv_dt = g.inv_dt;

        if (i == g.fs_idx) {
            g.d_szz_1[k] = 0.0f; g.d_szz_2[k] = 0.0f;

            float dvx_dx = g.c1_h * (g.d_vx_1[k + 1] + g.d_vx_2[k + 1] - g.d_vx_1[k] - g.d_vx_2[k])
                + g.c2_h * (g.d_vx_1[k + 2] + g.d_vx_2[k + 2] - g.d_vx_1[k - 1] - g.d_vx_2[k - 1])
                + g.c3_h * (g.d_vx_1[k + 3] + g.d_vx_2[k + 3] - g.d_vx_1[k - 2] - g.d_vx_2[k - 2])
                + g.c4_h * (g.d_vx_1[k + 4] + g.d_vx_2[k + 4] - g.d_vx_1[k - 3] - g.d_vx_2[k - 3]);

            // The free-surface row still lies inside the left/right PML. Keep
            // the x-split damping here, using the traction-free effective
            // modulus, instead of bypassing the side absorber at the corner.
            g.d_sxx_1[k] = (g.d_sxx_1[k] * (inv_dt - d_x_half_val) + g.d_dp_flat[j] * dvx_dx)
                / (inv_dt + d_x_half_val);
            g.d_sxx_2[k] = 0.0f;

            float dvz_dx = g.c1_h * (g.d_vz_1[k] + g.d_vz_2[k] - g.d_vz_1[k - 1] - g.d_vz_2[k - 1])
                + g.c2_h * (g.d_vz_1[k + 1] + g.d_vz_2[k + 1] - g.d_vz_1[k - 2] - g.d_vz_2[k - 2]);

            float dvx_dz = g.c1_h * (g.d_vx_1[row_dn1 + j] + g.d_vx_2[row_dn1 + j] - g.d_vx_1[k] - g.d_vx_2[k])
                + g.c2_h * (g.d_vx_1[row_dn2 + j] + g.d_vx_2[row_dn2 + j] - g.d_vx_1[row_up1 + j] - g.d_vx_2[row_up1 + j]);

            g.d_sxz_1[k] = (g.d_sxz_1[k] * (inv_dt - d_x_val) + 0.5f * (g.d_mu[k] + g.d_mu[row_dn1 + j]) * dvz_dx) / (inv_dt + d_x_val);
            g.d_sxz_2[k] = (g.d_sxz_2[k] * (inv_dt - d_z_half_val) + 0.5f * (g.d_mu[k] + g.d_mu[row_dn1 + j]) * dvx_dz) / (inv_dt + d_z_half_val);
        }
        else {
            float dvx_dx = g.c1_h * (g.d_vx_1[k + 1] + g.d_vx_2[k + 1] - g.d_vx_1[k] - g.d_vx_2[k])
                + g.c2_h * (g.d_vx_1[k + 2] + g.d_vx_2[k + 2] - g.d_vx_1[k - 1] - g.d_vx_2[k - 1])
                + g.c3_h * (g.d_vx_1[k + 3] + g.d_vx_2[k + 3] - g.d_vx_1[k - 2] - g.d_vx_2[k - 2])
                + g.c4_h * (g.d_vx_1[k + 4] + g.d_vx_2[k + 4] - g.d_vx_1[k - 3] - g.d_vx_2[k - 3]);

            float dvz_dz = g.c1_h * (g.d_vz_1[k] + g.d_vz_2[k] - g.d_vz_1[row_up1 + j] - g.d_vz_2[row_up1 + j])
                + g.c2_h * (g.d_vz_1[row_dn1 + j] + g.d_vz_2[row_dn1 + j] - g.d_vz_1[row_up2 + j] - g.d_vz_2[row_up2 + j])
                + g.c3_h * (g.d_vz_1[row_dn2 + j] + g.d_vz_2[row_dn2 + j] - g.d_vz_1[row_up3 + j] - g.d_vz_2[row_up3 + j])
                + g.c4_h * (g.d_vz_1[row_dn3 + j] + g.d_vz_2[row_dn3 + j] - g.d_vz_1[row_up4 + j] - g.d_vz_2[row_up4 + j]);

            g.d_sxx_1[k] = (g.d_sxx_1[k] * (inv_dt - d_x_half_val) + 0.5f * (g.d_lambda2mu[k] + g.d_lambda2mu[k + 1]) * dvx_dx) / (inv_dt + d_x_half_val);
            g.d_sxx_2[k] = (g.d_sxx_2[k] * (inv_dt - d_z_val) + 0.5f * (g.d_lambda[k] + g.d_lambda[k + 1]) * dvz_dz) / (inv_dt + d_z_val);

            g.d_szz_1[k] = (g.d_szz_1[k] * (inv_dt - d_x_half_val) + 0.5f * (g.d_lambda[k] + g.d_lambda[k + 1]) * dvx_dx) / (inv_dt + d_x_half_val);
            g.d_szz_2[k] = (g.d_szz_2[k] * (inv_dt - d_z_val) + 0.5f * (g.d_lambda2mu[k] + g.d_lambda2mu[k + 1]) * dvz_dz) / (inv_dt + d_z_val);

            float dvz_dx = g.c1_h * (g.d_vz_1[k] + g.d_vz_2[k] - g.d_vz_1[k - 1] - g.d_vz_2[k - 1])
                + g.c2_h * (g.d_vz_1[k + 1] + g.d_vz_2[k + 1] - g.d_vz_1[k - 2] - g.d_vz_2[k - 2])
                + g.c3_h * (g.d_vz_1[k + 2] + g.d_vz_2[k + 2] - g.d_vz_1[k - 3] - g.d_vz_2[k - 3])
                + g.c4_h * (g.d_vz_1[k + 3] + g.d_vz_2[k + 3] - g.d_vz_1[k - 4] - g.d_vz_2[k - 4]);

            float dvx_dz = g.c1_h * (g.d_vx_1[row_dn1 + j] + g.d_vx_2[row_dn1 + j] - g.d_vx_1[k] - g.d_vx_2[k])
                + g.c2_h * (g.d_vx_1[row_dn2 + j] + g.d_vx_2[row_dn2 + j] - g.d_vx_1[row_up1 + j] - g.d_vx_2[row_up1 + j])
                + g.c3_h * (g.d_vx_1[row_dn3 + j] + g.d_vx_2[row_dn3 + j] - g.d_vx_1[row_up2 + j] - g.d_vx_2[row_up2 + j])
                + g.c4_h * (g.d_vx_1[row_dn4 + j] + g.d_vx_2[row_dn4 + j] - g.d_vx_1[row_up3 + j] - g.d_vx_2[row_up3 + j]);

            g.d_sxz_1[k] = (g.d_sxz_1[k] * (inv_dt - d_x_val) + 0.5f * (g.d_mu[k] + g.d_mu[row_dn1 + j]) * dvz_dx) / (inv_dt + d_x_val);
            g.d_sxz_2[k] = (g.d_sxz_2[k] * (inv_dt - d_z_half_val) + 0.5f * (g.d_mu[k] + g.d_mu[row_dn1 + j]) * dvx_dz) / (inv_dt + d_z_half_val);
        }
    }
}
SEIS_CELL void mirrorType3Stress(GPUSimData g,int j) {
    if (j >= 4 && j < g.NX - 4) {
        for (int l = 1; l <= 4; ++l) {
            int idx_above = (g.fs_idx - l) * g.NX + j;
            int idx_below = (g.fs_idx + l) * g.NX + j;

            if (idx_above >= 0 && idx_below < g.total_grid) {
                g.d_szz_1[idx_above] = -g.d_szz_1[idx_below];
                g.d_szz_2[idx_above] = -g.d_szz_2[idx_below];

                int idx_below_shear = (g.fs_idx + l - 1) * g.NX + j;
                g.d_sxz_1[idx_above] = -g.d_sxz_1[idx_below_shear];
                g.d_sxz_2[idx_above] = -g.d_sxz_2[idx_below_shear];
            }
        }
    }
}
SEIS_CELL void updateType3Velocity(GPUSimData g,int i,int j) {

    if (i >= 4 && i < g.NZ - 4 && j >= 4 && j < g.NX - 4) {
        int k = i * g.NX + j;

        if (i < g.fs_idx) {
            g.d_vx_1[k] = 0.0f; g.d_vx_2[k] = 0.0f;
            g.d_vz_1[k] = 0.0f; g.d_vz_2[k] = 0.0f;
        }
        else {
            int row_dn1 = (i + 1) * g.NX; int row_dn2 = (i + 2) * g.NX;
            int row_dn3 = (i + 3) * g.NX; int row_dn4 = (i + 4) * g.NX;
            int row_up1 = (i - 1) * g.NX; int row_up2 = (i - 2) * g.NX; int row_up3 = (i - 3) * g.NX; int row_up4 = (i - 4) * g.NX;

            float d_z_val = (i <= g.fs_idx + 5 && g.upFlag) ? 0.0f : g.d_dz[i];
            float d_z_half_val = (i <= g.fs_idx + 5 && g.upFlag) ? 0.0f : g.d_dz_half[i];
            float d_x_val = g.d_dx[j];
            float d_x_half_val = g.d_dx_half[j];
            float cur_rho = g.d_rho[k];
            float inv_dt = g.inv_dt;

            float dsigxx_dx = g.c1_h * (g.d_sxx_1[k] + g.d_sxx_2[k] - g.d_sxx_1[k - 1] - g.d_sxx_2[k - 1])
                + g.c2_h * (g.d_sxx_1[k + 1] + g.d_sxx_2[k + 1] - g.d_sxx_1[k - 2] - g.d_sxx_2[k - 2])
                + g.c3_h * (g.d_sxx_1[k + 2] + g.d_sxx_2[k + 2] - g.d_sxx_1[k - 3] - g.d_sxx_2[k - 3])
                + g.c4_h * (g.d_sxx_1[k + 3] + g.d_sxx_2[k + 3] - g.d_sxx_1[k - 4] - g.d_sxx_2[k - 4]);

            float dsigxz_dz = g.c1_h * (g.d_sxz_1[k] + g.d_sxz_2[k] - g.d_sxz_1[row_up1 + j] - g.d_sxz_2[row_up1 + j])
                + g.c2_h * (g.d_sxz_1[row_dn1 + j] + g.d_sxz_2[row_dn1 + j] - g.d_sxz_1[row_up2 + j] - g.d_sxz_2[row_up2 + j])
                + g.c3_h * (g.d_sxz_1[row_dn1 + g.NX + j] + g.d_sxz_2[row_dn1 + g.NX + j] - g.d_sxz_1[row_up3 + j] - g.d_sxz_2[row_up3 + j])
                + g.c4_h * (g.d_sxz_1[row_dn1 + 2 * g.NX + j] + g.d_sxz_2[row_dn1 + 2 * g.NX + j] - g.d_sxz_1[row_up4 + j] - g.d_sxz_2[row_up4 + j]);

            g.d_vx_1[k] = (g.d_vx_1[k] * (inv_dt - d_x_val) + dsigxx_dx / cur_rho) / (inv_dt + d_x_val);
            g.d_vx_2[k] = (g.d_vx_2[k] * (inv_dt - d_z_val) + dsigxz_dz / cur_rho) / (inv_dt + d_z_val);

            float dsigxz_dx = g.c1_h * (g.d_sxz_1[k + 1] + g.d_sxz_2[k + 1] - g.d_sxz_1[k] - g.d_sxz_2[k])
                + g.c2_h * (g.d_sxz_1[k + 2] + g.d_sxz_2[k + 2] - g.d_sxz_1[k - 1] - g.d_sxz_2[k - 1])
                + g.c3_h * (g.d_sxz_1[k + 3] + g.d_sxz_2[k + 3] - g.d_sxz_1[k - 2] - g.d_sxz_2[k - 2])
                + g.c4_h * (g.d_sxz_1[k + 4] + g.d_sxz_2[k + 4] - g.d_sxz_1[k - 3] - g.d_sxz_2[k - 3]);

            float dsigzz_dz = g.c1_h * (g.d_szz_1[row_dn1 + j] + g.d_szz_2[row_dn1 + j] - g.d_szz_1[k] - g.d_szz_2[k])
                + g.c2_h * (g.d_szz_1[row_dn2 + j] + g.d_szz_2[row_dn2 + j] - g.d_szz_1[row_up1 + j] - g.d_szz_2[row_up1 + j])
                + g.c3_h * (g.d_szz_1[row_dn3 + j] + g.d_szz_2[row_dn3 + j] - g.d_szz_1[row_up2 + j] - g.d_szz_2[row_up2 + j])
                + g.c4_h * (g.d_szz_1[row_dn4 + j] + g.d_szz_2[row_dn4 + j] - g.d_szz_1[row_up3 + j] - g.d_szz_2[row_up3 + j]);

            float rho_avg = 0.25f * (g.d_rho[k] + g.d_rho[k + 1] + g.d_rho[row_dn1 + j] + g.d_rho[row_dn1 + j + 1]);
            g.d_vz_1[k] = (g.d_vz_1[k] * (inv_dt - d_x_half_val) + dsigxz_dx / rho_avg) / (inv_dt + d_x_half_val);
            g.d_vz_2[k] = (g.d_vz_2[k] * (inv_dt - d_z_half_val) + dsigzz_dz / rho_avg) / (inv_dt + d_z_half_val);
        }
    }
}
SEIS_CELL void zeroBoundary(GPUSimData g,int idx) {
    if (idx < g.NX) {
        int top = idx;
        int btm = (g.NZ - 1) * g.NX + idx;
        if (g.flag_type == 1 || g.flag_type == 3) {
            g.d_vx_1[top] = 0; g.d_vx_2[top] = 0; g.d_vz_1[top] = 0; g.d_vz_2[top] = 0;
            g.d_vx_1[btm] = 0; g.d_vx_2[btm] = 0; g.d_vz_1[btm] = 0; g.d_vz_2[btm] = 0;
        }
        else {
            g.d_vx[top] = 0; g.d_vz[top] = 0; g.d_vx[btm] = 0; g.d_vz[btm] = 0;
        }
    }
    if (idx < g.NZ) {
        int left = idx * g.NX;
        int right = idx * g.NX + (g.NX - 1);
        if (g.flag_type == 1 || g.flag_type == 3) {
            g.d_vx_1[left] = 0; g.d_vx_2[left] = 0; g.d_vz_1[left] = 0; g.d_vz_2[left] = 0;
            g.d_vx_1[right] = 0; g.d_vx_2[right] = 0; g.d_vz_1[right] = 0; g.d_vz_2[right] = 0;
        }
        else {
            g.d_vx[left] = 0; g.d_vz[left] = 0; g.d_vx[right] = 0; g.d_vz[right] = 0;
        }
    }
}
SEIS_CELL void add(float* p,float v) {
#ifdef __CUDA_ARCH__
 atomicAdd(p,v);
#else
 *p+=v;
#endif
}
SEIS_CELL void applyPointSource(GPUSimData g,float wavelet_val,int src_idx) {
    if (src_idx < 0 || src_idx >= g.total_grid || !isfinite(wavelet_val)) return;
    const int sourceX = src_idx % g.NX;
    const int sourceZ = src_idx / g.NX;
    if (sourceX < kStencilRadius || sourceX >= g.NX - kStencilRadius ||
        sourceZ < kStencilRadius || sourceZ >= g.NZ - kStencilRadius ||
        (g.flag_type == 3 && g.upFlag && sourceZ <= g.fs_idx)) return;
    // g.src_type:
    // 0: Vertical Force (Tzz) - 垂直方向集中力，直接作用于 Vz 分量
    // 1: Horizontal Force (Txx) - 水平方向集中力，直接作用于 Vx 分量
    // 2: Explosive (Txx + Tzz) - 各向同性膨胀压力源 (纯 P 波)，直接注入正应力张量
    // 3: Shear (Txz) - 纯剪切源 (生成强 S 波)，直接注入剪应力张量
    // 4: Rotated Force - 倾斜方向力源，根据 g.src_angle 分解并作用于 Vx 和 Vz

    if (g.src_type == 0) { // 垂直集中力
        float force_z = wavelet_val;
        if (g.flag_type == 1 || g.flag_type == 3) {
            float rho_avg = 0.25f * (g.d_rho[src_idx] + g.d_rho[src_idx + 1] + g.d_rho[src_idx + g.NX] + g.d_rho[src_idx + g.NX + 1]);
            add(&g.d_vz_1[src_idx], force_z * g.dt / rho_avg);
        }
        else if (g.flag_type == 2) {
            add(&g.d_vz[src_idx], force_z * g.dt / g.d_rho_x_z_flat[src_idx]);
        }
    }
    else if (g.src_type == 1) { // 水平集中力
        float force_x = wavelet_val;
        if (g.flag_type == 1 || g.flag_type == 3) {
            add(&g.d_vx_1[src_idx], force_x * g.dt / g.d_rho[src_idx]);
        }
        else if (g.flag_type == 2) {
            add(&g.d_vx[src_idx], force_x * g.dt / g.d_rho_orig_flat[src_idx]);
        }
    }
    else if (g.src_type == 2) { // 膨胀震源 (纯压缩 P 波，无横波分量) -> 作用于对角应力张量 [2]
        if (g.flag_type == 1 || g.flag_type == 3) {
            add(&g.d_sxx_1[src_idx], wavelet_val * 0.5f);
            add(&g.d_sxx_2[src_idx], wavelet_val * 0.5f);
            add(&g.d_szz_1[src_idx], wavelet_val * 0.5f);
            add(&g.d_szz_2[src_idx], wavelet_val * 0.5f);
        }
        else if (g.flag_type == 2) {
            add(&g.d_sigmaxx[src_idx], wavelet_val);
            add(&g.d_sigmazz[src_idx], wavelet_val);
        }
    }
    else if (g.src_type == 3) { // 剪切震源 (剪切波) -> 作用于离角剪切应力张量 Sxz
        if (g.flag_type == 1 || g.flag_type == 3) {
            add(&g.d_sxz_1[src_idx], wavelet_val * 0.5f);
            add(&g.d_sxz_2[src_idx], wavelet_val * 0.5f);
        }
        else if (g.flag_type == 2) {
            add(&g.d_sigmaxz[src_idx], wavelet_val);
        }
    }
    else if (g.src_type == 4) { // 任意倾斜力源 (Rotated Force)
        float force_x = sinf(g.src_angle * M_PI / 180.0f) * wavelet_val;
        float force_z = cosf(g.src_angle * M_PI / 180.0f) * wavelet_val;
        if (g.flag_type == 1 || g.flag_type == 3) {
            add(&g.d_vx_1[src_idx], force_x * g.dt / g.d_rho[src_idx]);
            float rho_avg = 0.25f * (g.d_rho[src_idx] + g.d_rho[src_idx + 1] + g.d_rho[src_idx + g.NX] + g.d_rho[src_idx + g.NX + 1]);
            add(&g.d_vz_1[src_idx], force_z * g.dt / rho_avg);
        }
        else if (g.flag_type == 2) {
            add(&g.d_vx[src_idx], force_x * g.dt / g.d_rho_orig_flat[src_idx]);
            add(&g.d_vz[src_idx], force_z * g.dt / g.d_rho_x_z_flat[src_idx]);
        }
    }
}
SEIS_CELL float get_vx(const GPUSimData& g, int i, int j) {
    // 替代 max(0, min(i, g.NZ - 1))
    int r = (i < 0) ? 0 : ((i > g.NZ - 1) ? g.NZ - 1 : i);
    // 替代 max(0, min(j, g.NX - 1))
    int c = (j < 0) ? 0 : ((j > g.NX - 1) ? g.NX - 1 : j);

    int k = r * g.NX + c;
    return (g.flag_type == 1 || g.flag_type == 3) ? (g.d_vx_1[k] + g.d_vx_2[k]) : g.d_vx[k];
}

SEIS_CELL float get_vz(const GPUSimData& g, int i, int j) {
    // 替代 max(0, min(i, g.NZ - 1))
    int r = (i < 0) ? 0 : ((i > g.NZ - 1) ? g.NZ - 1 : i);
    // 替代 max(0, min(j, g.NX - 1))
    int c = (j < 0) ? 0 : ((j > g.NX - 1) ? g.NX - 1 : j);

    int k = r * g.NX + c;
    return (g.flag_type == 1 || g.flag_type == 3) ? (g.d_vz_1[k] + g.d_vz_2[k]) : g.d_vz[k];
}
SEIS_CELL float component(GPUSimData g,int i,int j,int component,float& component_balancer) {
 int k=i*g.NX+j;
        float val = 0.0f;
        component_balancer = 1.0f; // 针对不同物理分量的自适应增益平衡因子

        if (component == 0) {
            // 1. Velocity Mag
            float vx = get_vx(g, i, j);
            float vz = get_vz(g, i, j);
            val = sqrtf(vx * vx + vz * vz);
            component_balancer = 1.0f;
        }
        else if (component == 1) {
            // 2. Velocity X
            val = get_vx(g, i, j);
            component_balancer = 1.0f;
        }
        else if (component == 2) {
            // 3. Velocity Y (Z)
            val = get_vz(g, i, j);
            component_balancer = 1.0f;
        }
        else if (component == 3) {
            // 4. Stress Normal (Sxx + Szz)
            float stress = 0.0f;
            if (g.flag_type == 1 || g.flag_type == 3) {
                stress = g.d_sxx_1[k] + g.d_sxx_2[k] + g.d_szz_1[k] + g.d_szz_2[k];
            }
            else if (g.flag_type == 2) {
                stress = g.d_sigmaxx[k] + g.d_sigmazz[k];
            }

            // 【物理归一化】：应力数值极大（GPa量级），除以本地纵波拉梅常数转换为应变尺度（10^-6 ~ 10^-3）
            float lam2mu = g.d_lambda2mu[k];
            val = (lam2mu > 1000.0f) ? (stress / lam2mu) : stress;
            val *= 1000;//处理后变小，需要继续增大1000左右
            component_balancer = 2.0f; // 归一化后的微调增益
        }
        else if (component == 4) {
            // 5. Stress Shear (Sxz)
            float shear = 0.0f;
            if (g.flag_type == 1 || g.flag_type == 3) {
                shear = g.d_sxz_1[k] + g.d_sxz_2[k];
            }
            else if (g.flag_type == 2) {
                shear = g.d_sigmaxz[k];
            }

            // 【物理归一化】：剪切应力除以剪切模量 mu
            float mu = g.d_mu[k];
            val = (mu > 1000.0f) ? (shear / mu) : shear;
            val *= 1000;
            component_balancer = 3.0f; // 归一化后的微调增益
        }
        else if (component == 5) {
            // 6. P-Wave (Div): 散度 (微商数值偏小，乘以 8.0 倍补偿) [2]
            float dvx_dx = get_vx(g, i, j) - get_vx(g, i, j - 1);
            float dvz_dz = get_vz(g, i, j) - get_vz(g, i - 1, j);
            val = dvx_dx + dvz_dz;
            component_balancer = 8.0f;
        }
        else if (component == 6) {
            // 7. S-Wave (Curl): 旋度 (微商数值偏小，乘以 8.0 倍补偿) [2]
            float dvz_dx = get_vz(g, i, j) - get_vz(g, i, j - 1);
            float dvx_dz = get_vx(g, i, j) - get_vx(g, i - 1, j);
            val = dvz_dx - dvx_dz;
            component_balancer = 8.0f;
        }

return val;
}
}
#undef SEIS_CELL
