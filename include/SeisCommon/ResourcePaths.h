#pragma once

#include <filesystem>
#include <string>

// 返回当前可执行文件所在目录，不依赖启动程序时的工作目录。
std::filesystem::path getExecutableDirectory();

// 查找程序旁的资源；兼容旧构建将资源放在可执行文件父目录的布局。
std::filesystem::path resolveResourcePath(const std::filesystem::path& relativePath);

// 返回 Windows 实际系统目录中的字体目录。
std::filesystem::path getSystemFontDirectory();

// 将原生文件路径转换为支持 UTF-8 文件名的第三方接口所需字符串。
std::string pathToUtf8(const std::filesystem::path& path);
