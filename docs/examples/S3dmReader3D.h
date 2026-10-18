#pragma once

// Standalone C++17 reader. No CUDA, OpenGL, ImGui or project headers are required.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Seis3D {

struct S3dmHeader3D {
    std::uint32_t version3D = 0, nx3D = 0, ny3D = 0, nz3D = 0;
    std::uint32_t pml3D = 0, boundary3D = 0;
    float spacing3D = 0;
    std::uint64_t cellCount3D = 0;
};

class S3dmReader3D {
public:
    // Validate the header and exact length; full physical validation occurs when planes are read.
    explicit S3dmReader3D(const std::filesystem::path& path3D, std::uint64_t maxNodes3D = 36000000)
        : input3D(path3D, std::ios::binary) {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "S3DM requires IEEE 754 float32");
        if (!input3D) throw std::runtime_error("Cannot open S3DM model");
        char magic3D[8]{};
        readExact3D(magic3D, 8);
        headerData3D.version3D = readU32Le3D();
        if (std::memcmp(magic3D, "S3DMOD1", 8) != 0
            || (headerData3D.version3D != 1 && headerData3D.version3D != 2))
            throw std::runtime_error("Unsupported S3DM magic or version");
        headerData3D.nx3D = readU32Le3D();
        headerData3D.ny3D = readU32Le3D();
        headerData3D.nz3D = readU32Le3D();
        headerData3D.pml3D = readU32Le3D();
        headerData3D.boundary3D = readU32Le3D();
        const auto spacingBits3D = readU32Le3D();
        std::memcpy(&headerData3D.spacing3D, &spacingBits3D, 4);
        const auto& h3D = headerData3D;
        if (!maxNodes3D || !h3D.nx3D || !h3D.ny3D || !h3D.nz3D
            || h3D.nx3D > maxNodes3D / h3D.ny3D
            || std::uint64_t(h3D.nx3D) * h3D.ny3D > maxNodes3D / h3D.nz3D)
            throw std::runtime_error("Invalid or over-limit S3DM dimensions");
        headerData3D.cellCount3D = std::uint64_t(h3D.nx3D) * h3D.ny3D * h3D.nz3D;
        if (h3D.boundary3D > (h3D.version3D == 1 ? 1u : 2u)
            || !std::isfinite(h3D.spacing3D) || h3D.spacing3D <= 0)
            throw std::runtime_error("Invalid S3DM boundary or spacing");
        const auto maximumOffset3D = std::uint64_t((std::numeric_limits<std::streamoff>::max)());
        if (h3D.cellCount3D > (maximumOffset3D - 36) / 12)
            throw std::runtime_error("S3DM payload exceeds stream offset capacity");
        input3D.seekg(0, std::ios::end);
        const auto fileBytes3D = input3D.tellg();
        if (!input3D || fileBytes3D != std::streampos(std::streamoff(36 + 12 * h3D.cellCount3D)))
            throw std::runtime_error("S3DM size does not match its dimensions");
        seekPayload3D(0);
    }

    // Expose immutable metadata without reading the complete material volume.
    const S3dmHeader3D& header3D() const noexcept { return headerData3D; }

    // Read one zero-based XYZ sample as [Vp, Vs, rho], using only twelve payload bytes.
    std::array<float, 3> readVoxel3D(std::uint32_t x3D, std::uint32_t y3D, std::uint32_t z3D,
                                   bool validate3D = true) {
        const auto& h3D = headerData3D;
        if (x3D >= h3D.nx3D || y3D >= h3D.ny3D || z3D >= h3D.nz3D)
            throw std::out_of_range("S3DM XYZ index is outside the physical grid");
        seekPayload3D((std::uint64_t(z3D) * h3D.ny3D + y3D) * h3D.nx3D + x3D);
        std::array<float, 3> values3D{};
        readFloats3D(values3D.data(), values3D.size());
        if (validate3D) validateMaterial3D(values3D.data());
        return values3D;
    }

    // Load one XY plane in [y][x][Vp,Vs,rho] order; never allocate the entire model.
    std::vector<float> readPlane3D(std::uint32_t z3D, bool validate3D = true) {
        const auto& h3D = headerData3D;
        if (z3D >= h3D.nz3D) throw std::out_of_range("S3DM Z index is outside the physical grid");
        const auto count3D = std::uint64_t(h3D.nx3D) * h3D.ny3D * 3;
        std::vector<float> values3D;
        if (count3D > values3D.max_size()
            || count3D > std::uint64_t((std::numeric_limits<std::streamsize>::max)()) / 4)
            throw std::runtime_error("S3DM plane exceeds buffer or stream capacity");
        values3D.resize(static_cast<std::size_t>(count3D));
        seekPayload3D(std::uint64_t(z3D) * h3D.nx3D * h3D.ny3D);
        readFloats3D(values3D.data(), values3D.size());
        if (validate3D)
            for (std::size_t i3D = 0; i3D < values3D.size(); i3D += 3)
                validateMaterial3D(values3D.data() + i3D);
        return values3D;
    }

private:
    std::ifstream input3D;
    S3dmHeader3D headerData3D;

    // Reject incomplete reads rather than returning partially initialized material arrays.
    void readExact3D(char* destination3D, std::streamsize bytes3D) {
        input3D.read(destination3D, bytes3D);
        if (!input3D) throw std::runtime_error("Truncated or unreadable S3DM input");
    }

    // Decode integers explicitly, avoiding compiler struct padding and native byte order.
    std::uint32_t readU32Le3D() {
        unsigned char b3D[4]{};
        readExact3D(reinterpret_cast<char*>(b3D), 4);
        return std::uint32_t(b3D[0]) | (std::uint32_t(b3D[1]) << 8)
            | (std::uint32_t(b3D[2]) << 16) | (std::uint32_t(b3D[3]) << 24);
    }

    // Seek by physical sample index; the constructor has checked the full byte-range capacity.
    void seekPayload3D(std::uint64_t index3D) {
        input3D.clear();
        input3D.seekg(std::streamoff(36 + index3D * 12), std::ios::beg);
        if (!input3D) throw std::runtime_error("Cannot seek within S3DM input");
    }

    // Convert the current Windows-exported little-endian float payload on big-endian readers too.
    void readFloats3D(float* values3D, std::size_t count3D) {
        readExact3D(reinterpret_cast<char*>(values3D), std::streamsize(count3D * 4));
        const std::uint32_t one3D = 1;
        if (*reinterpret_cast<const unsigned char*>(&one3D) == 1) return;
        auto* bytes3D = reinterpret_cast<unsigned char*>(values3D);
        for (std::size_t i3D = 0; i3D < count3D; ++i3D) {
            std::swap(bytes3D[i3D * 4], bytes3D[i3D * 4 + 3]);
            std::swap(bytes3D[i3D * 4 + 1], bytes3D[i3D * 4 + 2]);
        }
    }

    // Apply the current application's finite-value and isotropic-material conditions.
    static void validateMaterial3D(const float* values3D) {
        const float vp3D = values3D[0], vs3D = values3D[1], rho3D = values3D[2];
        if (!std::isfinite(vp3D) || !std::isfinite(vs3D) || !std::isfinite(rho3D)
            || vp3D <= 0 || vp3D > 30000 || vs3D < 0 || vs3D >= vp3D
            || vp3D * vp3D <= (4.0f / 3.0f) * vs3D * vs3D || rho3D <= 0 || rho3D > 50000)
            throw std::runtime_error("S3DM contains invalid Vp/Vs/rho");
    }
};

} // namespace Seis3D
