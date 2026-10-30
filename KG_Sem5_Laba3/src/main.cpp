#include "DirectXApp.h"
#include "GameTimer.h"
#include "../h/Window.h"

#include <Windows.h>
#include <string>
#include <stdexcept>
#include <memory>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, int nCmdShow) {
    Window window(hInstance, nCmdShow);
    if (!window.Initialize(L"DirectX 12 Framework", 1280, 720)) {
        MessageBox(nullptr, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    auto app = std::make_unique<DirectXApp>();

    try {
        if (!app->Initialize(window.GetHwnd(), static_cast<unsigned int>(window.GetWidth()), static_cast<unsigned int>(window.GetHeight()))) {
            MessageBox(window.GetHwnd(), L"Initialization failed.", L"Error", MB_OK | MB_ICONERROR);
            return 0;
        }
    } catch (const std::exception& ex) {
        std::string msg = "Initialization exception: ";
        msg += ex.what();
        MessageBoxA(window.GetHwnd(), msg.c_str(), "Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    GameTimer timer;
    return window.Run(*app, timer);
}


