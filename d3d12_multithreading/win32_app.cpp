#include "stdafx.h"
#include "win32_app.h"

HWND Win32App::hwnd_ = nullptr;

LRESULT CALLBACK
Win32App::WindowProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // -- retrieve the OdxSample passed in
    OdxSample * dxsam = reinterpret_cast<OdxSample *>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA)
    );
    switch (msg) {
        case WM_CREATE: {
            // -- store the OdxSample passed in (to CreateWindow)
            LPCREATESTRUCT createstruct =
                reinterpret_cast<LPCREATESTRUCT>(lparam);
            SetWindowLongPtr(
                hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(createstruct->lpCreateParams)
            );
        } return 0;
        case WM_KEYDOWN:
            if (dxsam)
                dxsam->OnKeyDown(static_cast<UINT8>(wparam));
            return 0;
        case WM_KEYUP:
            if (dxsam)
                dxsam->OnKeyUp(static_cast<UINT8>(wparam));
            return 0;
        case WM_PAINT:
            if (dxsam) {
                dxsam->OnUpdate();
                dxsam->OnRender();
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}
int Win32App::Run (OdxSample * dxsam, HINSTANCE instance, int cmdshow) {
    // -- parse cmd params
    int argc;
    LPWSTR * argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    dxsam->ParseCommandLineArgs(argv, argc);
    LocalFree(argv);

    // -- init window class
    WNDCLASSEX window_class = {};
    window_class.cbSize = sizeof(WNDCLASSEX);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.lpszClassName = L"OdxSampleClass";
    RegisterClassEx(&window_class);

    RECT window_rect = {
        0, 0,
        static_cast<LONG>(dxsam->GetWidth()),
        static_cast<LONG>(dxsam->GetHeight())
    };

    // -- create window
    hwnd_ = CreateWindow(
        window_class.lpszClassName,
        dxsam->GetTitle(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        nullptr,    // -- no parent window
        nullptr,    // -- no menus
        instance,
        dxsam
    );

    // -- initialize the dxsam
    dxsam->OnInit();

    ShowWindow(hwnd_, cmdshow);

    // -- main loop
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    dxsam->OnDestroy();

    // -- return wparam of the message to Windows
    return static_cast<char>(msg.wParam);
}

