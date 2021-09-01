#pragma once

#include "odx_helper.h"

struct OdxSample {
private:
    std::wstring asset_path_;   // -- root path for assets
    std::wstring title_;        // -- window title
protected:
    UINT width_;
    UINT height_;
    float aspect_ratio_;
    bool use_warp_;
    std::wstring GetAssetFullPath (LPCWSTR assetname);
    void GetHardwareAdapter (
        _In_ IDXGIFactory1 * factory,
        _Outptr_result_maybenull_ IDXGIAdapter1 ** adapter,
        bool request_high_performance_adapter = false
    );
    void SetCustomWindowText (LPCWSTR text, HWND wnd);
public:
    OdxSample (UINT width, UINT height, std::wstring name);
    virtual ~OdxSample();

    virtual void OnInit () = 0;
    virtual void OnUpdate () = 0;
    virtual void OnRender () = 0;
    virtual void OnDestroy () = 0;

    virtual void OnKeyDown (UINT8) {}
    virtual void OnKeyUp (UINT8) {}

    UINT GetWidth () const { return width_; }
    UINT GetHeight () const { return height_; }
    WCHAR const * GetTitle () const { return title_.c_str(); }

    void ParseCommandLineArgs (_In_reads_(argc) WCHAR * argv [], int argc);
};

