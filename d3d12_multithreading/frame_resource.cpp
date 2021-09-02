#include "stdafx.h"
#include "frame_resource.h"
#include "squid_room.h"

FrameResource::FrameResource (
    ID3D12Device * device,
    ID3D12PipelineState * pso,
    ID3D12PipelineState * shadow_pso,
    ID3D12DescriptorHeap * dsv_heap,
    ID3D12DescriptorHeap * cbv_srv_heap,
    D3D12_VIEWPORT * viewport,
    UINT frame_resource_index
) : fence_value_(0), pso_(pso), pso_smap_(shadow_pso) {
    for (int i = 0; i < CmdlistCount; ++i) {
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&cmdallocs_[i])
        ));
        ThrowIfFailed(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            cmdallocs_[i].Get(),
            pso_.Get(),
            IID_PPV_ARGS(&cmdlists_[i])
        ));
        NAME_D3D12_OBJECT_INDEXED(cmdlists_, i);
        ThrowIfFailed(cmdlists_[i]->Close());
    }
    for (int i = 0; i < NumContexts; ++i) {
        // -- worker cmd allocators
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&scene_cmdallocs_[i])
        ));
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&shadow_cmdallocs_[i])
        ));
        // -- worker cmd lists
        ThrowIfFailed(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            scene_cmdallocs_[i].Get(),
            pso_.Get(),
            IID_PPV_ARGS(&scene_cmdlists_[i])
        ));
        ThrowIfFailed(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            shadow_cmdallocs_[i].Get(),
            pso_smap_.Get(),
            IID_PPV_ARGS(&shadow_cmdlists_[i])
        ));

        NAME_D3D12_OBJECT_INDEXED(scene_cmdlists_, i);
        NAME_D3D12_OBJECT_INDEXED(shadow_cmdlists_, i);

        ThrowIfFailed(scene_cmdlists_[i]->Close());
        ThrowIfFailed(shadow_cmdlists_[i]->Close());
    }
    // -- describe and create smap tex
    CD3DX12_RESOURCE_DESC shadow_tex_desc (
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        0,
        static_cast<UINT>(viewport->Width),
        static_cast<UINT>(viewport->Height),
        1, 1,
        DXGI_FORMAT_R32_TYPELESS,
        1, 0,
        D3D12_TEXTURE_LAYOUT_UNKNOWN,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    );
    // NOTE(omid): performance tip: in the runtime,
    // specify the desired clear value at resource creation time
    D3D12_CLEAR_VALUE clear_value;
    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 1.0f;
    clear_value.DepthStencil.Stencil = 0;
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &shadow_tex_desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear_value,
        IID_PPV_ARGS(&shadow_tex_)
    ));

    NAME_D3D12_OBJECT(shadow_tex_);

    // -- get a handle to the start of the descriptor heap
    // -- and offest it based on frame resource index
    UINT const dsv_descriptor_size =
        device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV
    );
    CD3DX12_CPU_DESCRIPTOR_HANDLE depth_handle(
        dsv_heap->GetCPUDescriptorHandleForHeapStart(),
        1 + frame_resource_index,   // +1 for the smap
        dsv_descriptor_size
    );

    // -- describe and create the shadow dep view
    D3D12_DEPTH_STENCIL_VIEW_DESC depstncl_view_desc = {};
    depstncl_view_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depstncl_view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depstncl_view_desc.Texture2D.MipSlice = 0;
    device->CreateDepthStencilView(
        shadow_tex_.Get(),
        &depstncl_view_desc,
        depth_handle
    );
    // -- cache the cpu descriptor handle
    shadow_depth_view_ = depth_handle;

    // -- get a handle to the start of the descriptor heap
    // -- and offet it based on existing textures and frameresource index
    // NOTE(omid): Each frame has 1 SRV (smap) and 2 CBVs
    UINT const null_srv_count = 2;  // null descriptors at the start of the heap
    // -- diffuse + normal textures near the start of the heap:
    UINT const texture_count = ArrayCount(SampleAssets::Textures);
    UINT const cbv_srv_descriptor_size =
        device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );
    CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_srv_cpu_handle(
        cbv_srv_heap->GetCPUDescriptorHandleForHeapStart()
    );
    CD3DX12_GPU_DESCRIPTOR_HANDLE cbv_srv_gpu_handle(
        cbv_srv_heap->GetGPUDescriptorHandleForHeapStart()
    );
    null_srv_handle_ = cbv_srv_gpu_handle;
    cbv_srv_cpu_handle.Offset(
        null_srv_count + texture_count +
        (frame_resource_index * FrameCount),
        cbv_srv_descriptor_size
    );
    cbv_srv_gpu_handle.Offset(
        null_srv_count + texture_count +
        (frame_resource_index * FrameCount),
        cbv_srv_descriptor_size
    );

    // -- describe and create a srv for shadow depth tex
    // NOTE(omid): this srv is for sampling smap from our shader
    // it uses same tex that we use as depstncl during shadow pass
    D3D12_SHADER_RESOURCE_VIEW_DESC shadow_srv_desc = {};
    shadow_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    shadow_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shadow_srv_desc.Texture2D.MipLevels = 1;
    shadow_srv_desc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device->CreateShaderResourceView(
        shadow_tex_.Get(),
        &shadow_srv_desc,
        cbv_srv_cpu_handle
    );
    // -- cache the gpu descriptro handle
    shadow_depth_handle_ = cbv_srv_gpu_handle;

    // -- increment descriptor handles
    cbv_srv_cpu_handle.Offset(cbv_srv_descriptor_size);
    cbv_srv_gpu_handle.Offset(cbv_srv_descriptor_size);

    // -- create cbuffers
    UINT const cbufsize =
        CalculateCBufferByteSize(sizeof(SceneCBuffer));
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(cbufsize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&shadow_cbuffer_)
    ));
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(cbufsize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&scene_cbuffer_)
    ));

    // -- map the cbuffers and cache their heap pointers
    CD3DX12_RANGE read_range(0, 0); // i.e., we don't intend to read from this resource on cpu
    ThrowIfFailed(scene_cbuffer_->Map(
        0, &read_range,
        reinterpret_cast<void **>(&scene_cbuffer_write_only_ptr_)
    ));
    ThrowIfFailed(shadow_cbuffer_->Map(
        0, &read_range,
        reinterpret_cast<void **>(&shadow_cbuffer_write_only_ptr_)
    ));

    // -- create cbuffer views (CBVs):
    // -- one for shadow pass, one for scene pass
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
    cbv_desc.SizeInBytes = cbufsize;

    // -- describe and create shadow CBV
    cbv_desc.BufferLocation = shadow_cbuffer_->GetGPUVirtualAddress();
    device->CreateConstantBufferView(&cbv_desc, cbv_srv_cpu_handle);

    // -- cache the gpu descriptor handle
    shadow_cbv_handle_ = cbv_srv_gpu_handle;

    // -- increment descriptor handles
    cbv_srv_cpu_handle.Offset(cbv_srv_descriptor_size);
    cbv_srv_gpu_handle.Offset(cbv_srv_descriptor_size);

    // -- describe and create scene CBV
    cbv_desc.BufferLocation = scene_cbuffer_->GetGPUVirtualAddress();
    device->CreateConstantBufferView(&cbv_desc, cbv_srv_cpu_handle);

    // -- cache the gpu descriptor handle
    scene_cbv_handle_ = cbv_srv_gpu_handle;

    // -- batch up cmd lists for execution later
    UINT const batch_size =
        ArrayCount(scene_cmdlists_) +
        ArrayCount(shadow_cmdlists_) +
        3;  // -- Cmdlist Pre, Mid and Post
    batch_submit_[0] = cmdlists_[CmdlistPre].Get();
    memcpy(
        batch_submit_ + 1,
        shadow_cmdlists_,
        ArrayCount(shadow_cmdlists_) * sizeof(ID3D12CommandList *)
    );
    batch_submit_[ArrayCount(shadow_cmdlists_) + 1] =
        cmdlists_[CmdlistMid].Get();
    memcpy(
        batch_submit_ + 2 + ArrayCount(shadow_cmdlists_),
        scene_cmdlists_,
        ArrayCount(scene_cmdlists_) * sizeof(ID3D12CommandList *)
    );
    batch_submit_[batch_size - 1] =
        cmdlists_[CmdlistPost].Get();
}
FrameResource::~FrameResource () {
    for (int i = 0; i < CmdlistCount; ++i) {
        cmdallocs_[i] = nullptr;
        cmdlists_[i] = nullptr;
    }

    shadow_cbuffer_ = nullptr;
    scene_cbuffer_ = nullptr;

    for (int i = 0; i < NumContexts; ++i) {
        shadow_cmdallocs_[i] = nullptr;
        shadow_cmdlists_[i] = nullptr;

        scene_cmdallocs_[i] = nullptr;
        scene_cmdlists_[i] = nullptr;
    }

    shadow_tex_ = nullptr;
}
//
// -- set up the descriptor tables for the worker cmdlist
// -- to use resources (provided by the frame resource)
void FrameResource::Bind (
    ID3D12GraphicsCommandList * cmdlist,
    BOOL scene_pass,
    D3D12_CPU_DESCRIPTOR_HANDLE * rtv_handle,
    D3D12_CPU_DESCRIPTOR_HANDLE * dsv_handle
) {
    if (scene_pass) {
        // -- for scene pass we use cbuf#2 and dep_stncl#2
        cmdlist->SetGraphicsRootDescriptorTable(2, shadow_depth_handle_);
        cmdlist->SetGraphicsRootDescriptorTable(1, scene_cbv_handle_);

        assert(rtv_handle != nullptr);
        assert(dsv_handle != nullptr);

        cmdlist->OMSetRenderTargets(1, rtv_handle, FALSE, dsv_handle);
    } else {    // -- shadow pass
        // -- set a null srv for the shadow texture
        // -- (for out of bounds behavior)
        cmdlist->SetGraphicsRootDescriptorTable(2, null_srv_handle_);
        // -- use cbuf#1 for shadow pass
        cmdlist->SetGraphicsRootDescriptorTable(1, shadow_cbv_handle_);
        // -- disable rendering to render-target and use dep_stncl#1
        cmdlist->OMSetRenderTargets(0, nullptr, FALSE, &shadow_depth_view_);

    }
}
void FrameResource::Init () {
    // -- reset cmdallocs and lists for the main thread
    for (int i = 0; i < CmdlistCount; ++i) {
        ThrowIfFailed(cmdallocs_[i]->Reset());
        ThrowIfFailed(cmdlists_[i]->Reset(
            cmdallocs_[i].Get(),
            pso_.Get()
        ));
    }
    // -- clear dep stncl buf (prepare for rendering smap)
    cmdlists_[CmdlistPre]->ClearDepthStencilView(
        shadow_depth_view_,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f,
        0, 0, nullptr
    );
    // -- reset worker cmdallocs and lists
    for (int i = 0; i < NumContexts; ++i) {
        ThrowIfFailed(shadow_cmdallocs_[i]->Reset());
        ThrowIfFailed(shadow_cmdlists_[i]->Reset(
            shadow_cmdallocs_[i].Get(),
            pso_smap_.Get()
        ));
        ThrowIfFailed(scene_cmdallocs_[i]->Reset());
        ThrowIfFailed(scene_cmdlists_[i]->Reset(
            scene_cmdallocs_[i].Get(),
            pso_.Get()
        ));
    }
}
void FrameResource::SwapBarriers () {
    // -- transition of smap from writable to readable
    auto rsc_bar = CD3DX12_RESOURCE_BARRIER::Transition(
        shadow_tex_.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cmdlists_[CmdlistMid]->ResourceBarrier(
        1, &rsc_bar
    );
}
void FrameResource::Finish () {
    auto rsc_bar = CD3DX12_RESOURCE_BARRIER::Transition(
        shadow_tex_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );
    cmdlists_[CmdlistPost]->ResourceBarrier(
        1, &rsc_bar
    );
}
//
// -- build and write cbufs from scratch to the proper slots
// -- for this frame resource
void FrameResource::WriteCBuffers (
    D3D12_VIEWPORT * viewport,
    Camera * scene_camera,
    Camera light_cams[NumLights],
    LightState lights[NumLights]
) {
    SceneCBuffer scene_cbuf = {};
    SceneCBuffer shadow_cbuf = {};

    // -- scale down the world a bit
    XMStoreFloat4x4(
        &scene_cbuf.model,
        XMMatrixScaling(0.1f, 0.1f, 0.1f)
    );
    XMStoreFloat4x4(
        &shadow_cbuf.model,
        XMMatrixScaling(0.1f, 0.1f, 0.1f)
    );

    // -- scene pass is drawn from camera pov
    scene_camera->Get3DViewProjMatrices(
        &scene_cbuf.view, &scene_cbuf.projection,
        90.0f, viewport->Width, viewport->Height
    );
    // -- shadow pass is drawn from first light pov
    light_cams[0].Get3DViewProjMatrices(
        &shadow_cbuf.view, &shadow_cbuf.projection,
        90.0f, viewport->Width, viewport->Height
    );

    for (int i = 0; i < NumLights; ++i) {
        memcpy(&scene_cbuf.lights[i], &lights[i], sizeof(LightState));
        memcpy(&shadow_cbuf.lights[i], &lights[i], sizeof(LightState));
    }

    // -- shadow pass won't sample the smap but rather write to it
    shadow_cbuf.sample_smap = FALSE;

    // -- On the contrary, scene pass samples the smap
    scene_cbuf.sample_smap = TRUE;

    shadow_cbuf.ambient_color = scene_cbuf.ambient_color = {
        0.1f, 0.2f, 0.3f, 1.0f
    };

    memcpy(
        scene_cbuffer_write_only_ptr_,
        &scene_cbuf,
        sizeof(SceneCBuffer)
    );
    memcpy(
        shadow_cbuffer_write_only_ptr_,
        &shadow_cbuf,
        sizeof(SceneCBuffer)
    );
}
