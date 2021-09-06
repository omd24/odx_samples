#pragma once

#include "odx_sample.h"
#include "camera.h"
#include "timer.h"
#include "squid_room.h"

using namespace DirectX;

using Microsoft::WRL::ComPtr;

struct FrameResource;

struct LightState {
    XMFLOAT4 position;
    XMFLOAT4 direction;
    XMFLOAT4 color;
    XMFLOAT4 falloff;

    XMFLOAT4X4 view;
    XMFLOAT4X4 projection;
};

struct SceneCBuffer {
    XMFLOAT4X4 model;
    XMFLOAT4X4 view;
    XMFLOAT4X4 projection;
    XMFLOAT4 ambient_color;
    BOOL sample_smap;       // -- shadow map
    BOOL padding[3];        // -- align to be float4s
    LightState lights[NumLights];
};

struct OdxMultithreading : public OdxSample {
private:
    struct InputState {
        bool right_arrow_pressed;
        bool left_arrow_pressed;
        bool up_arrow_pressed;
        bool down_arrow_pressed;
        bool animate;
    };

    // -- pipeline objs
    CD3DX12_VIEWPORT viewport_;
    CD3DX12_RECT scissor_rect_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12Resource> render_targets_[FrameCount];
    ComPtr<ID3D12Resource> depth_stencil_;
    ComPtr<ID3D12CommandAllocator> cmdalloc_;
    ComPtr<ID3D12CommandQueue> cmdqueue_;
    ComPtr<ID3D12RootSignature> rootsig_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    ComPtr<ID3D12DescriptorHeap> cbv_srv_heap_;
    ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    ComPtr<ID3D12PipelineState> pso_;
    ComPtr<ID3D12PipelineState> pso_smap_;

    // -- app resources
    D3D12_VERTEX_BUFFER_VIEW vb_view_;
    D3D12_INDEX_BUFFER_VIEW ib_view_;
    ComPtr<ID3D12Resource> textures_[ArrayCount(SampleAssets::Textures)];
    ComPtr<ID3D12Resource> texture_uploads_[ArrayCount(SampleAssets::Textures)];
    ComPtr<ID3D12Resource> ib_;
    ComPtr<ID3D12Resource> ib_upload_;
    ComPtr<ID3D12Resource> vb_;
    ComPtr<ID3D12Resource> vb_upload_;
    UINT rtv_descriptor_size_;
    InputState keyboard_input_;
    LightState lights_[NumLights];
    Camera light_cameras_[NumLights];
    Camera camera_;
    Timer timer_;
    Timer cpu_timer_;
    int title_count_;
    double cpu_time_;

    // -- synchronization objects
    HANDLE worker_begin_render_frame_[NumContexts];
    HANDLE worker_finish_shadow_pass_[NumContexts];
    HANDLE worker_finished_render_frame_[NumContexts];
    HANDLE thread_handles_[NumContexts];
    UINT frame_index_;
    HANDLE fence_event_;
    ComPtr<ID3D12Fence> fence_;
    UINT64 fence_value_;

    // -- singleton object so that worker threads can share data members
    static OdxMultithreading * s_app;

    // -- frame resources
    FrameResource * frame_resources_[FrameCount];
    FrameResource * current_frame_resource_;
    int current_frame_resource_index_;

    struct ThreadParameter {
        int thread_index;
    };
    ThreadParameter thread_parameters_[NumContexts];

    void WorkerThread (int thread_index);
    void SetCommonPipelineState (ID3D12GraphicsCommandList * cmdlist);

    void LoadPipeLine (HWND hwnd);
    void LoadAssets ();
    void RestoreD3DResources ();
    void ReleaseD3DResources ();
    void WaitForGpu ();
    void LoadContexts ();
    void BeginFrame ();
    void MidFrame ();
    void EndFrame ();

public:
    OdxMultithreading (UINT width, UINT height, std::wstring name);
    virtual ~OdxMultithreading ();

    static OdxMultithreading * Get () { return s_app; }

    virtual void OnInit ();
    virtual void OnUpdate ();
    virtual void OnRender ();
    virtual void OnDestroy ();
    virtual void OnKeyDown (UINT8 key);
    virtual void OnKeyUp (UINT8 key);
};
