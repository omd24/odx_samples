#pragma once

#include "odx_sample.h"

struct OdxSample;
struct Win32App {
private:
    static HWND hwnd_;
    static LRESULT CALLBACK
    WindowProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

public:
    static int
    Run (OdxSample * dxsam, HINSTANCE instance, int cmdshow);
    static HWND
    GetHwnd () {return hwnd_;}

};
