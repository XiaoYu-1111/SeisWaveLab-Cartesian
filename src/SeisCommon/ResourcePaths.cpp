#include "ResourcePaths.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <system_error>
#include <vector>

// 使用宽字符 Windows API 获取模块路径，自动扩容以支持较长的安装路径。
std::filesystem::path getExecutableDirectory() {
    std::vector<wchar_t> pathBuffer(MAX_PATH);
    for (;;) {
        const DWORD pathLength = GetModuleFileNameW(
            nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
        if (pathLength == 0) {
            throw std::system_error(GetLastError(), std::system_category(),
                "Cannot locate the executable");
        }
        if (pathLength < pathBuffer.size()) {
            return std::filesystem::path(
                std::wstring(pathBuffer.data(), pathLength)).parent_path();
        }
        pathBuffer.resize(pathBuffer.size() * 2);
    }
}

// 优先使用随可执行文件部署的资源，缺失时返回明确路径供调用方报错。
std::filesystem::path resolveResourcePath(const std::filesystem::path& relativePath) {
    static const std::filesystem::path executableDirectory = getExecutableDirectory();
    const std::filesystem::path resourcePath =
        executableDirectory / "resource_CUDA_V1" / relativePath;
    std::error_code error;
    if (std::filesystem::is_regular_file(resourcePath, error)) {
        return resourcePath;
    }

    const std::filesystem::path legacyPath =
        executableDirectory.parent_path() / "resource_CUDA_V1" / relativePath;
    error.clear();
    if (std::filesystem::is_regular_file(legacyPath, error)) {
        return legacyPath;
    }
    return resourcePath;
}

// 查询真实系统目录，避免将系统盘盘符写死为 C:。
std::filesystem::path getSystemFontDirectory() {
    std::vector<wchar_t> pathBuffer(MAX_PATH);
    UINT pathLength = GetWindowsDirectoryW(pathBuffer.data(), static_cast<UINT>(pathBuffer.size()));
    if (pathLength >= pathBuffer.size()) {
        pathBuffer.resize(static_cast<size_t>(pathLength) + 1);
        pathLength = GetWindowsDirectoryW(pathBuffer.data(), static_cast<UINT>(pathBuffer.size()));
    }
    if (pathLength == 0 || pathLength >= pathBuffer.size()) {
        throw std::system_error(GetLastError(), std::system_category(),
            "Cannot locate the Windows font directory");
    }
    return std::filesystem::path(std::wstring(pathBuffer.data(), pathLength)) / "Fonts";
}

// 兼容 C++17/C++20 中 u8string 字符类型的差异。
std::string pathToUtf8(const std::filesystem::path& path) {
    const auto encodedPath = path.u8string();
    return std::string(encodedPath.begin(), encodedPath.end());
}
