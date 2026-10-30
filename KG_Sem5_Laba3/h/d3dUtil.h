#pragma once

#include <d3dcompiler.h>
#include <wrl.h>
#include <stdexcept>
#include <string>

class DxException : public std::runtime_error {
public:
    explicit DxException(const std::string& message) : std::runtime_error(message) {}
};

inline void ThrowIfFailed(HRESULT hr, const char* message = "HRESULT failed") {
    if (FAILED(hr)) {
        throw DxException(message);
    }
}

class d3dUtil {
public:
    static unsigned int CalcConstantBufferByteSize(unsigned int byteSize);

    static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
        const std::wstring& filename,
        const D3D_SHADER_MACRO* defines,
        const std::string& entrypoint,
        const std::string& target);
};
