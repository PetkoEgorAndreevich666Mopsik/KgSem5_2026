#include "DirectXApp.h"
#include "d3dUtil.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include <filesystem>
#include <stdexcept>

void DirectXApp::InitializeSettingsUi() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mUiSrvHeap)), "Create UI heap failed");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    wchar_t windowsDirectory[MAX_PATH] = {};
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    const auto fontPath = std::filesystem::path(windowsDirectory) / "Fonts/segoeui.ttf";
    if (!io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 18.0f))
        throw std::runtime_error("Cannot load UI font");
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.97f);

    if (!ImGui_ImplWin32_Init(mHwnd)) throw std::runtime_error("Initialize Win32 UI failed");
    ImGui_ImplDX12_InitInfo info;
    info.Device = mDevice.Get();
    info.CommandQueue = mCommandQueue.Get();
    info.NumFramesInFlight = SwapChainBufferCount;
    info.RTVFormat = mBackBufferFormat;
    info.SrvDescriptorHeap = mUiSrvHeap.Get();
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        *cpu = init->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        *gpu = init->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};
    if (!ImGui_ImplDX12_Init(&info)) throw std::runtime_error("Initialize DirectX UI failed");
}

void DirectXApp::SetAtmospherePreset(bool haze) {
    mRayleighScale = 1.0f;
    mMieCoefficient = haze ? 0.0006f : 2e-6f;
}

void DirectXApp::ChangeScene(int scene) {
    if (scene == mCurrentScene) return;
    mCurrentScene = scene;
    mDebugViewMode = 1;
    BuildScene();
}

void DirectXApp::ResetCamera() {
    if (mCurrentScene == MeshPipelineScene) {
        mEyePos = XMFLOAT3(4.0f, 3.0f, -7.5f);
        mYaw = -0.49f;
        mPitch = -0.34f;
        return;
    }
    if (mCurrentScene == 3) {
        mEyePos = XMFLOAT3(9, 9, -20);
        mYaw = -0.45f;
        mPitch = -0.28f;
        return;
    }
    mEyePos = mCurrentScene == 0 ? XMFLOAT3(0, 90, -300)
        : (mCurrentScene == 1 ? XMFLOAT3(0, 3, -10) : XMFLOAT3(0, 0, 0));
    mYaw = mCurrentScene == 2 ? XM_PIDIV2 : 0.0f;
    mPitch = mCurrentScene == 0 ? -0.05f : (mCurrentScene == 1 ? -0.08f : 0.0f);
}

void DirectXApp::UpdateSettingsUi() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(510, 450), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(440, 300), ImVec2(900, 900));
    if (ImGui::Begin("Settings")) {
        ImGui::TextDisabled("%.0f FPS", mCurrentFps);
        if (ImGui::BeginTabBar("SettingsTabs")) {
            if (ImGui::BeginTabItem("Shadows")) {
                if (!mRtShadows.Supported()) ImGui::TextWrapped("DXR 1.1 unavailable. Using existing shadow maps.");
                ImGui::BeginDisabled(!mRtShadows.Supported());
                const char* modes[] = {"No shadows", "Hard RT", "Soft RT"};
                ImGui::Combo("Mode", &mShadowMode, modes, 3);
                ImGui::SliderAngle("Sun radius", &mSunRadius, 0.0f, 8.0f, "%.2f deg");
                ImGui::SliderInt("Rays per pixel", &mShadowSamples, 1, 32);
                ImGui::Checkbox("Spatial filter", &mShadowFilter);
                const char* masks[] = {"Lighting", "Hard visibility", "Raw soft visibility", "Filtered visibility", "Blocker distance"};
                ImGui::Combo("Display", &mShadowView, masks, 5);
                ImGui::SliderFloat("Ray offset (m)", &mShadowBias, 0.0001f, 0.03f, "%.4f", ImGuiSliderFlags_Logarithmic);
                ImGui::EndDisabled();
                if (ImGui::Button("Shadow test scene")) ChangeScene(3);
                ImGui::BeginDisabled(mCurrentScene != 3);
                ImGui::SliderFloat("Caster height", &mCasterHeight, 0.0f, 6.0f, "%.2f m");
                ImGui::SliderFloat("Caster position X", &mCasterX, -10.0f, 10.0f, "%.2f m");
                ImGui::Checkbox("Move casters", &mAnimateCaster);
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Atmosphere")) {
                if (ImGui::Button("Clear")) SetAtmospherePreset(false);
                ImGui::SameLine();
                if (ImGui::Button("Haze")) SetAtmospherePreset(true);
                ImGui::SeparatorText("Scattering");
                ImGui::SliderFloat("Rayleigh", &mRayleighScale, 0.0f, 5.0f, "%.2f");
                float mie = mMieCoefficient * 1000.0f;
                if (ImGui::SliderFloat("Mie (1/km)", &mie, 0.0f, 2.0f, "%.3f")) mMieCoefficient = mie / 1000.0f;
                ImGui::Checkbox("Aerial perspective", &mAtmosphereOnObjects);
                ImGui::SeparatorText("Sun");
                ImGui::SliderAngle("Azimuth", &mSunAzimuth, 0.0f, 360.0f, "%.1f°");
                ImGui::SliderAngle("Elevation", &mSunElevation, 0.0f, 360.0f, "%.1f°");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Scene")) {
                const char* scenes[] = {"Terrain", "Earth", "Sponza", "RT Shadows", "Mesh Pipeline"};
                int scene = mCurrentScene;
                if (ImGui::Combo("Scene", &scene, scenes, _countof(scenes))) ChangeScene(scene);
                if (mCurrentScene == MeshPipelineScene) {
                    ImGui::Text("Vertices: %zu   Triangles: %zu   Meshlets: %u",
                                mSceneMesh.vertices.size(), mSceneMesh.indices.size() / 3, mMeshletCount);
                    if (mStorageError.empty()) ImGui::TextUnformatted("Assets loaded via DirectStorage");
                    else ImGui::TextWrapped("DirectStorage fallback: %s", mStorageError.c_str());
                    const bool supported = mRenderingSystem->GetMeshPSO(false) != nullptr;
                    if (!supported) ImGui::TextWrapped("Mesh Shader Tier 1 and Shader Model 6.5 required. Using indexed rendering.");
                    ImGui::BeginDisabled(!supported);
                    ImGui::Checkbox("Color meshlets", &mColorMeshlets);
                    ImGui::Checkbox("Single meshlet", &mShowSingleMeshlet);
                    ImGui::BeginDisabled(!mShowSingleMeshlet);
                    ImGui::SliderInt("Meshlet", &mSelectedMeshlet, 0, static_cast<int>(mMeshletCount) - 1);
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    ImGui::SliderAngle("Model rotation", &mMeshYaw, -180.0f, 180.0f);
                    if (ImGui::Button("Reset camera")) ResetCamera();
                }
                const char* views[] = {"Material", "Normals", "Wireframe and LOD"};
                int view = mDebugViewMode - 1;
                if (ImGui::Combo("View", &view, views, 3)) mDebugViewMode = view + 1;
                ImGui::Checkbox("Frustum culling", &mFrustumCullingEnabled);
                ImGui::BeginDisabled(mCurrentScene != 0);
                ImGui::Checkbox("Terrain LOD", &mTerrainLodEnabled);
                ImGui::EndDisabled();
                ImGui::SeparatorText("Statistics");
                ImGui::Text("Visible patches: %u", mLastVisibleObjectCount);
                ImGui::Text("Draw calls: %u", mLastDrawCallCount);
                if (mCurrentScene == 0) ImGui::Text("Culled nodes: %u", mTerrain.Culled());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Brush")) {
                ImGui::BeginDisabled(mCurrentScene != 0);
                if (ImGui::Checkbox("Edit terrain", &mBrushEnabled))
                    mBrushVisible = false;
                ImGui::SliderFloat("Radius (m)", &mBrushRadius, 4.0f, 48.0f, "%.1f");
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Camera")) {
                ImGui::InputFloat3("Position", &mEyePos.x, "%.1f");
                ImGui::SliderAngle("Yaw", &mYaw, -180.0f, 180.0f, "%.1f°");
                ImGui::SliderAngle("Pitch", &mPitch, XMConvertToDegrees(-1.45f), XMConvertToDegrees(1.45f), "%.1f°");
                if (ImGui::Button("Reset camera")) ResetCamera();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (ImGui::GetIO().WantCaptureMouse) mBrushVisible = false;
    ImGui::Render();
}

void DirectXApp::DrawSettingsUi() {
    ID3D12DescriptorHeap* heaps[] = {mUiSrvHeap.Get()};
    mCommandList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
}
