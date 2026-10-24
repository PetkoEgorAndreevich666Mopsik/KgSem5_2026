#include "../h/Window.h"

#include "DirectXApp.h"
#include "GameTimer.h"

Window::Window(HINSTANCE instance, int showCmd)
    : hInstance(instance), nCmdShow(showCmd) {
}

Window::~Window() {
    if (hWnd && IsWindow(hWnd)) {
        DestroyWindow(hWnd);
    }
    hWnd = nullptr;
    UnregisterClass(L"Lab3DX12WindowClass", hInstance);
}

bool Window::Initialize(const wchar_t* title, int clientWidth, int clientHeight) {
    width = clientWidth;
    height = clientHeight;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"Lab3DX12WindowClass";

    if (!RegisterClassEx(&wc)) {
        return false;
    }

    RECT windowRect = {0, 0, width, height};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    hWnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        this);

    if (!hWnd) {
        return false;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return true;
}

int Window::Run(DirectXApp& app, GameTimer& timer) {
    mDirectXApp = &app;
    timer.Reset();

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            if (!hWnd || !IsWindow(hWnd)) {
                break;
            }
            timer.Tick();
            app.Update(timer);
            app.Draw(timer);
        }
    }

    mDirectXApp = nullptr;
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Window* window = nullptr;

    if (message == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (message == WM_CLOSE) {
        DestroyWindow(hWnd);
        return 0;
    }

    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    if (message == WM_NCDESTROY) {
        if (window) {
            window->hWnd = nullptr;
        }
        SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    if (window && window->mDirectXApp) {
        return window->mDirectXApp->MsgProc(hWnd, message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
