#include "stdafx.h"
#include "odx_sample.h"

using namespace Microsoft::WRL;

OdxSample::OdxSample (
    UINT width, UINT height, std::wstring name
) : width_(width), height_(height), title_(name), use_warp_(false) {
    WCHAR assetpath[512];
    GetAssetsPath(assetpath, _countof(assetpath));
    asset_path_ = assetpath;

    aspect_ratio_ =
        static_cast<float>(width) / static_cast<float>(height);
}
OdxSample::~OdxSample () {}
//
// -- resolving to full path (append to root)
std::wstring OdxSample::GetAssetFullPath (LPCWSTR assetname) {
    return asset_path_ + assetname;
}
_Use_decl_annotations_
void OdxSample::GetHardwareAdapter (
    _In_ IDXGIFactory1 * factory,
    _Outptr_result_maybenull_ IDXGIAdapter1 ** adapter,
    bool request_high_performance_adapter
) {
    *adapter = nullptr;
    ComPtr<IDXGIAdapter1> adapter1;
    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
        for (
            UINT adapter_index = 0;
            SUCCEEDED(factory6->EnumAdapterByGpuPreference (
            adapter_index,
            true == request_high_performance_adapter ?
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE :
            DXGI_GPU_PREFERENCE_UNSPECIFIED,
            IID_PPV_ARGS(&adapter1)));
            ++adapter_index
        ) {
            DXGI_ADAPTER_DESC1 desc1;
            adapter1->GetDesc1(&desc1);
            if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;   // -- skip basic render driver adpater
            if (
                SUCCEEDED(D3D12CreateDevice(
                adapter1.Get(),
                D3D_FEATURE_LEVEL_11_0,
                /* just check support, don't create the device: */
                _uuidof(ID3D12Device), nullptr))
            ) {
                break;
            }
        } // -- end for
    } // -- end if
    if (nullptr == adapter1.Get()) {
        for (
            UINT adapter_index = 0;
            SUCCEEDED(factory->EnumAdapters1(adapter_index, &adapter1));
            ++adapter_index
        ) {
            DXGI_ADAPTER_DESC1 desc1;
            adapter1->GetDesc1(&desc1);
            if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;   // -- skip basic render driver adpater
            if (
                SUCCEEDED(D3D12CreateDevice(
                adapter1.Get(),
                D3D_FEATURE_LEVEL_11_0,
                /* just check support, don't create the device: */
                _uuidof(ID3D12Device), nullptr))
            ) {
                break;
            }
        } // -- end for
    } // -- end if

    *adapter = adapter1.Detach();
}
void OdxSample::SetCustomWindowText (LPCWSTR text, HWND wnd) {
    std::wstring window_text = title_ + L": " + text;
    SetWindowText(wnd, window_text.c_str());
}
void OdxSample::ParseCommandLineArgs (
    _In_reads_(argc) WCHAR * argv [], int argc
) {
    for (int i = 0; i < argc; ++i) {
        if (
            _wcsnicmp(argv[i], L"-warp", wcslen(argv[i])) == 0 ||
            _wcsnicmp(argv[i], L"/warp", wcslen(argv[i])) == 0
        ) {
            use_warp_ = true;
            title_ = title_ + L" (WARP)";
        }
    }
}

