#include "d3dUtil.h"

#include <Windows.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {
std::string Narrow(const std::wstring& ws) {
    return std::string(ws.begin(), ws.end());
}

std::wstring ResolveShaderPath(const std::wstring& inputPath) {
    fs::path input(inputPath);

    if (input.is_absolute() && fs::exists(input)) {
        return input.wstring();
    }

    std::vector<fs::path> candidates;

    candidates.push_back(fs::current_path() / input);

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const fs::path exeDir = fs::path(exePath).parent_path();

    candidates.push_back(exeDir / input);
    candidates.push_back(exeDir / ".." / input);
    candidates.push_back(exeDir / ".." / ".." / input);

    for (const auto& candidate : candidates) {
        std::error_code ec;
        const fs::path normalized = fs::weakly_canonical(candidate, ec);
        if (!ec && fs::exists(normalized)) {
            return normalized.wstring();
        }
        if (fs::exists(candidate)) {
            return candidate.wstring();
        }
    }

    return inputPath;
}
} // namespace

unsigned int d3dUtil::CalcConstantBufferByteSize(unsigned int byteSize) {
    return (byteSize + 255) & ~255;
}

ComPtr<ID3DBlob> d3dUtil::CompileShader(
    const std::wstring& filename,
    const D3D_SHADER_MACRO* defines,
    const std::string& entrypoint,
    const std::string& target) {
    const std::wstring resolvedPath = ResolveShaderPath(filename);

    UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> byteCode;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(
        resolvedPath.c_str(),
        defines,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entrypoint.c_str(),
        target.c_str(),
        compileFlags,
        0,
        &byteCode,
        &errors);

    if (errors) {
        OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    }

    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "Failed to compile shader\n"
            << "File: " << Narrow(resolvedPath) << "\n"
            << "Entry: " << entrypoint << "\n"
            << "Target: " << target << "\n"
            << "HRESULT: 0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);

        if (errors && errors->GetBufferPointer()) {
            oss << "\nCompiler output:\n" << static_cast<const char*>(errors->GetBufferPointer());
        }

        throw DxException(oss.str());
    }

    return byteCode;
}
