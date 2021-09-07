
#include "stdafx.h"
#include "odx_multithreading.h"

_Use_decl_annotations_ int WINAPI
WinMain (HINSTANCE instance, HINSTANCE, LPSTR, int cmdshow) {
    OdxMultithreading sample(1280, 720, L"O D3D12 Multithreading App");
    return Win32App::Run(&sample, instance, cmdshow);
}
