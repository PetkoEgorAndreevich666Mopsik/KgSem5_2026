#pragma once

#include <windows.h>

class DirectXApp;
class GameTimer;

class Window {
public:
    Window(HINSTANCE hInstance, int nCmdShow);
    ~Window();

    bool Initialize(const wchar_t* title, int width, int height);
    int Run(DirectXApp& app, GameTimer& timer);

    HWND GetHwnd() const { return hWnd; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    DirectXApp* GetDirectXApp() const { return mDirectXApp; }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HINSTANCE hInstance = nullptr;
    HWND hWnd = nullptr;
    int width = 1280;
    int height = 720;
    int nCmdShow = SW_SHOW;
    DirectXApp* mDirectXApp = nullptr;
};


