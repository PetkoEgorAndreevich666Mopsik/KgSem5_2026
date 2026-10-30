#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

void Check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Direct3D operation failed: " + std::to_string(result));
}

ComPtr<ID3DBlob> Compile(const std::filesystem::path& path, const char* entry, const char* target) {
    ComPtr<ID3DBlob> shader, errors;
    const HRESULT result = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS, 0, &shader, &errors);
    if (errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
    Check(result);
    return shader;
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected project directory");
        const std::filesystem::path root(argv[1]);
        Compile(root / "src/post_effects.hlsl", "PS_PostEffects", "ps_5_0");
        Compile(root / "src/lighting.hlsl", "PS_Lighting", "ps_5_0");
        auto code = Compile(root / "tools/test_atmosphere.hlsl", "CS_Test", "cs_5_0");

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context));
        ComPtr<ID3D11ComputeShader> shader;
        Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 6 * 4 * sizeof(float);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = 4 * sizeof(float);
        ComPtr<ID3D11Buffer> output;
        Check(device->CreateBuffer(&desc, nullptr, &output));
        ComPtr<ID3D11UnorderedAccessView> view;
        Check(device->CreateUnorderedAccessView(output.Get(), nullptr, &view));
        ID3D11UnorderedAccessView* views[] = {view.Get()};
        context->CSSetUnorderedAccessViews(0, 1, views, nullptr);
        context->CSSetShader(shader.Get(), nullptr, 0);
        context->Dispatch(1, 1, 1);

        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;
        ComPtr<ID3D11Buffer> readback;
        Check(device->CreateBuffer(&desc, nullptr, &readback));
        context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        std::array<float, 24> values;
        std::copy_n(static_cast<const float*>(mapped.pData), values.size(), values.begin());
        context->Unmap(readback.Get(), 0);
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i] != 1.0f) throw std::runtime_error("Atmosphere check failed: " + std::to_string(i));
        std::cout << "Shaders compiled; 24 atmosphere checks passed on WARP\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
