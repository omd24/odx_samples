#pragma once

#include "stdafx.h"
#include "camera.h"
#include "odx_helper.h"
#include "odx_multithreading.h"

using namespace DirectX;
using namespace Microsoft::WRL;

struct FrameResource {
private:
    ComPtr<ID3D12PipelineState> pso_;
    ComPtr<ID3D12PipelineState> pso_smap_;
    ComPtr<ID3D12Resource> shadow_tex_;
    D3D12_CPU_DESCRIPTOR_HANDLE shadow_depth_view_;
    ComPtr<ID3D12Resource> shadow_cbuffer_;
    ComPtr<ID3D12Resource> scene_cbuffer_;
    SceneCBuffer * shadow_cbuffer_write_only_ptr_;
    SceneCBuffer * scene_cbuffer_write_only_ptr_;
    // -- use a null srv for out of bounds behavior
    D3D12_GPU_DESCRIPTOR_HANDLE null_srv_handle_;
    D3D12_GPU_DESCRIPTOR_HANDLE shadow_depth_handle_;
    D3D12_GPU_DESCRIPTOR_HANDLE shadow_cbv_handle_;
    D3D12_GPU_DESCRIPTOR_HANDLE scene_cbv_handle_;
public:
    ID3D12CommandList * batch_submit_ [NumContexts * 2 + CmdlistCount];

    ComPtr<ID3D12CommandAllocator> cmdallocs_[CmdlistCount];
    ComPtr<ID3D12GraphicsCommandList> cmdlists_[CmdlistCount];

    ComPtr<ID3D12CommandAllocator> shadow_cmdallocs_[NumContexts];
    ComPtr<ID3D12GraphicsCommandList> shadow_cmdlists_[NumContexts];

    ComPtr<ID3D12CommandAllocator> scene_cmdallocs_[NumContexts];
    ComPtr<ID3D12GraphicsCommandList> scene_cmdlists_[NumContexts];

    UINT64 fence_value_;

    FrameResource (
        ID3D12Device * device,
        ID3D12PipelineState * pso,
        ID3D12PipelineState * shadow_pso,
        ID3D12DescriptorHeap * dsv_heap,
        ID3D12DescriptorHeap * cbv_srv_heap,
        D3D12_VIEWPORT * viewport,
        UINT frame_resource_index
    );
    ~FrameResource ();

    void Bind (
        ID3D12GraphicsCommandList * cmdlist,
        BOOL scene_pass,
        D3D12_CPU_DESCRIPTOR_HANDLE * rtv_handle,
        D3D12_CPU_DESCRIPTOR_HANDLE * dsv_handle
    );
    void Init ();
    void SwapBarriers ();
    void Finish ();
    void WriteCBuffers (
        D3D12_VIEWPORT * viewport,
        Camera * scene_camera,
        Camera light_cams [NumLights],
        LightState lights [NumLights]
    );
};

