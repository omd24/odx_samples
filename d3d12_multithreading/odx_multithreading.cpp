#include "stdafx.h"
#include "odx_multithreading.h"
#include "frame_resource.h"

// -- body of a worker thread:
// -- index is an int from 0 to NumContexts
void OdxMultithreading::WorkerThread (int thread_index) {
    assert(thread_index >= 0);
    assert(thread_index < NumContexts);

#if !SINGLETHREADED
    while (thread_index >= 0 && thread_index < NumContexts) {
        // -- wait for the main thread to tell us to draw
        WaitForSingleObject(worker_begin_render_frame_[thread_index], INFINITE);
#endif // !SINGLETHREADED

        ID3D12GraphicsCommandList * shadow_cmdlist =
            current_frame_resource_->shadow_cmdlists_[thread_index].Get();
        ID3D12GraphicsCommandList * scene_cmdlist =
            current_frame_resource_->scene_cmdlists_[thread_index].Get();

        //
        // -- shadow pass
        //

        // -- populate cmdlist
        SetCommonPipelineState(shadow_cmdlist);
        current_frame_resource_->Bind(shadow_cmdlist, FALSE, nullptr, nullptr);

        // -- set null SRVs for diffuse/normal textures
        shadow_cmdlist->SetGraphicsRootDescriptorTable(
            0, cbv_srv_heap_->GetGPUDescriptorHandleForHeapStart()
        );

        // -- distribute objects over threads
        // -- by drawing only 1/NumContexts objs per worker
        // -- i.e., every obj such that obj_num % NumContexts == thread_index

        PIXBeginEvent(shadow_cmdlist, 0, L"worker thread drawing shadow pass...");
        for (
            int j = thread_index;
            j < ArrayCount(SampleAssets::Draws);
            j += NumContexts
        ) {
            SampleAssets::DrawParameters draw_args = SampleAssets::Draws[j];
            shadow_cmdlist->DrawIndexedInstanced(
                draw_args.IndexCount,
                1,
                draw_args.IndexStart,
                draw_args.VertexBase,
                0
            );
        }
        PIXEndEvent(shadow_cmdlist);
        ThrowIfFailed(shadow_cmdlist->Close());

#if !SINGLETHREADED
        // -- submit shadow pass
        SetEvent(worker_finish_shadow_pass_[thread_index]);
#endif // !SINGLETHREADED

        //
        // -- scene pass
        //

        // -- populate the cmdlist.
        // -- to be send only after shadow pass has been submitted
        SetCommonPipelineState(scene_cmdlist);
        CD3DX12_CPU_DESCRIPTOR_HANDLE hrtv(
            rtv_heap_->GetCPUDescriptorHandleForHeapStart(),
            frame_index_,
            rtv_descriptor_size_
        );
        CD3DX12_CPU_DESCRIPTOR_HANDLE hdsv(
            dsv_heap_->GetCPUDescriptorHandleForHeapStart()
        );
        current_frame_resource_->Bind(scene_cmdlist, TRUE, &hrtv, &hdsv);

        PIXBeginEvent(scene_cmdlist, 0, L"worker thread drawing scene pass...");
        D3D12_GPU_DESCRIPTOR_HANDLE cbv_srv_heap_start =
            cbv_srv_heap_->GetGPUDescriptorHandleForHeapStart();
        UINT const cbv_srv_descriptor_size =
            device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        UINT const null_srv_count = 2;
        for (
            int j = thread_index;
            j < ArrayCount(SampleAssets::Draws);
            j += NumContexts
        ) {
            SampleAssets::DrawParameters draw_args = SampleAssets::Draws[j];
            // -- set diffuse and normal maps for current obj
            CD3DX12_GPU_DESCRIPTOR_HANDLE cbv_srv_handle (
                cbv_srv_heap_start,
                null_srv_count + draw_args.DiffuseTextureIndex,
                cbv_srv_descriptor_size
            );
            scene_cmdlist->SetGraphicsRootDescriptorTable(
                0, cbv_srv_handle
            );
            scene_cmdlist->DrawIndexedInstanced(
                draw_args.IndexCount,
                1,
                draw_args.IndexStart,
                draw_args.VertexBase,
                0
            );
        }
        PIXEndEvent(scene_cmdlist);
        ThrowIfFailed(scene_cmdlist->Close());

#if !SINGLETHREADED
        // -- tell main thread, we are done
        SetEvent(worker_finished_render_frame_[thread_index]);
    }
#endif // !SINGLETHREADED
}
void OdxMultithreading::SetCommonPipelineState (
    ID3D12GraphicsCommandList * cmdlist
) {
    cmdlist->SetGraphicsRootSignature(rootsig_.Get());

    ID3D12DescriptorHeap * heaps [] = {
        cbv_srv_heap_.Get(), sampler_heap_.Get()
    };
    cmdlist->SetDescriptorHeaps(ArrayCount(heaps), heaps);

    cmdlist->RSSetViewports(1, &viewport_);
    cmdlist->RSSetScissorRects(1, &scissor_rect_);
    cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdlist->IASetVertexBuffers(0, 1, &vb_view_);
    cmdlist->IASetIndexBuffer(&ib_view_);
    cmdlist->SetGraphicsRootDescriptorTable(
        3, sampler_heap_->GetGPUDescriptorHandleForHeapStart()
    );
    cmdlist->OMSetStencilRef(0);

    // -- render targets and depstncl are set elsewhere
    // -- bc depstncl depends on the frame resource being used

    // -- cbuffers are set elsewhere
    // -- bc they depends on the frame resource being used

    // -- SRVs are set elsewhere bc they change based on obj being drawn
}
//
// -- load rendering pipeline dependencies
void OdxMultithreading::LoadPipeLine (HWND hwnd) {
    UINT dxgi_factory_flags = 0;
#if defined(_DEBUG)
    // NOTE(omid): Enabling debug layer after device creation
    // invalidates the active device
    {
        ComPtr<ID3D12Debug> debug_controller;
        if (
            SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))
        ) {
            debug_controller->EnableDebugLayer();
            // -- enable additional debug layers
            dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(
        dxgi_factory_flags,
        IID_PPV_ARGS(&factory)
    ));
    if (use_warp_) {
        ComPtr<IDXGIAdapter> warp_adapter;
        ThrowIfFailed(
            factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter))
        );
        ThrowIfFailed(D3D12CreateDevice(
            warp_adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device_)
        ));
    } else {
        ComPtr<IDXGIAdapter1> hardware_adapter;
        GetHardwareAdapter(factory.Get(), &hardware_adapter, true);
        ThrowIfFailed(D3D12CreateDevice(
            hardware_adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device_)
        ));
    }

    // -- describe and create command queue
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(device_->CreateCommandQueue(
        &queue_desc,
        IID_PPV_ARGS(&cmdqueue_)
    ));
    NAME_D3D12_OBJECT(cmdqueue_);

    // -- describe and create swapchain
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
    swapchain_desc.BufferCount = FrameCount;
    swapchain_desc.Width = width_;
    swapchain_desc.Height = height_;
    swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchain_desc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swap_chain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        cmdqueue_.Get(),    // swapchain needs the queue to force a flush
        hwnd,
        &swapchain_desc,
        nullptr, nullptr,
        &swap_chain
    ));
    // -- this sample does not support fullscreen transitions
    ThrowIfFailed(factory->MakeWindowAssociation(
        hwnd,
        DXGI_MWA_NO_ALT_ENTER
    ));

    ThrowIfFailed(swap_chain.As(&swapchain_));
    frame_index_ = swapchain_->GetCurrentBackBufferIndex();

    // -- create descriptor heaps
    {
        // -- describe and create a RTV descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
        rtv_heap_desc.NumDescriptors = FrameCount;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(device_->CreateDescriptorHeap(
            &rtv_heap_desc,
            IID_PPV_ARGS(&rtv_heap_)
        ));
        // -- describe and create a DSV descriptor heap
        // -- each frame has its own depstncls (to write shadows onto)
        // -- and then there is one for the scene itself
        D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
        dsv_heap_desc.NumDescriptors = 1 + FrameCount * 1;
        dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(device_->CreateDescriptorHeap(
            &dsv_heap_desc,
            IID_PPV_ARGS(&dsv_heap_)
        ));
        // -- descrube and create a SRV and CBV descriptor heap
        /*
            Heap layout:
                null views,
                object diffuse + normal textures views,
                frame 1's shadow buffer,
                frame 1's 2x cbuffers,
                frame 2's shadow buffer,
                frame 2's 2x cbuffers,
                frame 3's shadow buffer,
                frame 3's 2x cbuffers,
        */
        UINT const null_srv_count = 2;  // null descriptors needed for out of bounds behaviour reads
        UINT const cbv_count = FrameCount * 2;
        UINT const srv_count =
            ArrayCount(SampleAssets::Textures) + (FrameCount * 1);
        D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_heap_desc = {};
        cbv_srv_heap_desc.NumDescriptors =
            null_srv_count + cbv_count + srv_count;
        cbv_srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbv_srv_heap_desc.Flags =
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device_->CreateDescriptorHeap(
            &cbv_srv_heap_desc,
            IID_PPV_ARGS(&cbv_srv_heap_)
        ));
        NAME_D3D12_OBJECT(cbv_srv_heap_);

        // -- descrube and create a sampler descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
        sampler_heap_desc.NumDescriptors = 2;   // one clamp and one wrap sampler
        sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device_->CreateDescriptorHeap(
            &sampler_heap_desc,
            IID_PPV_ARGS(&sampler_heap_)
        ));
        NAME_D3D12_OBJECT(sampler_heap_);

        rtv_descriptor_size_ =
            device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }
    ThrowIfFailed(device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmdalloc_)
    ));
}
//
// -- load sample assets
void OdxMultithreading::LoadAssets () {
    //
    // -- create root rignature
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {};
        // -- highest supported version
        // -- CheckFeatureSupport succeeds, highest version wouldn't be higher
        feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (FAILED(device_->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE,
            &feature_data,
            sizeof(feature_data)
            ))) {
            feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }
        // NOTE(omid): Performance tip: order from most frequent used 
        CD3DX12_DESCRIPTOR_RANGE1 ranges[4];
        // -- two frequenctly changed diffuse + normal maps
        // -- using register t1 and t2
        ranges[0].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            2 /* num of descriptors */, 1 /* t1, t2 */, 0 /* space0 */,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC
        );
        // -- one frequently changed cbuffer
        ranges[1].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
            1 /* num of descriptors */, 0 /* b0 */, 0 /* space0 */,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC
        );
        // -- one infrequenctly changed shadow texture
        // -- using register t0
        ranges[2].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            1 /* num of descriptors */, 0 /* t0 */
        );
        ranges[3].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
            2 /* num of descriptors */, 0 /* s0 */
        );

        CD3DX12_ROOT_PARAMETER1 root_params[4];
        root_params[0].InitAsDescriptorTable(
            1 /* num of ranges */,
            &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL
        );
        root_params[1].InitAsDescriptorTable(
            1 /* num of ranges */,
            &ranges[1], D3D12_SHADER_VISIBILITY_ALL
        );
        root_params[2].InitAsDescriptorTable(
            1 /* num of ranges */,
            &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL
        );
        root_params[3].InitAsDescriptorTable(
            1 /* num of ranges */,
            &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL
        );

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc;
        root_sig_desc.Init_1_1(
            ArrayCount(root_params),
            root_params,
            0 /* num of static sampler */,
            nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        );

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
            &root_sig_desc,
            feature_data.HighestVersion,
            &signature,
            &error
        ));
        ThrowIfFailed(device_->CreateRootSignature(
            0 /* node mask */,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(&rootsig_)
        ));
        NAME_D3D12_OBJECT(rootsig_);
    }
    //
    // -- create pipeline state, which includes loading shaders
    //
    {
        ComPtr<ID3DBlob> vertex_shader;
        ComPtr<ID3DBlob> pixel_shader;
#if defined(_DEBUG)
        // -- enable better shader debugging with graphics debugging tools
        UINT compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compile_flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        ThrowIfFailed(D3DCompileFromFile(
            GetAssetFullPath(L"shaders.hlsl").c_str(),
            nullptr, nullptr,
            "VSMAIN", "vs_5_0",
            compile_flags, 0,
            &vertex_shader, nullptr
        ));
        ThrowIfFailed(D3DCompileFromFile(
            GetAssetFullPath(L"shaders.hlsl").c_str(),
            nullptr, nullptr,
            "PSMAIN", "ps_5_0",
            compile_flags, 0,
            &pixel_shader, nullptr
        ));

        D3D12_INPUT_LAYOUT_DESC input_layout_desc;
        input_layout_desc.pInputElementDescs =
            SampleAssets::StandardVertexDescription;
        input_layout_desc.NumElements =
            ArrayCount(SampleAssets::StandardVertexDescription);

        CD3DX12_DEPTH_STENCIL_DESC depthstncl_desc(D3D12_DEFAULT);
        depthstncl_desc.DepthEnable = true;
        depthstncl_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthstncl_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        depthstncl_desc.StencilEnable = FALSE;

        // -- describe and create pso for rendering the scene
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.InputLayout = input_layout_desc;
        pso_desc.pRootSignature = rootsig_.Get();
        pso_desc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader.Get());
        pso_desc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader.Get());
        pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pso_desc.DepthStencilState = depthstncl_desc;
        pso_desc.SampleMask = UINT_MAX;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pso_desc.SampleDesc.Count = 1;

        ThrowIfFailed(device_->CreateGraphicsPipelineState(
            &pso_desc,
            IID_PPV_ARGS(&pso_)
        ));
        NAME_D3D12_OBJECT(pso_);

        // -- alter description and create pso for rendering the smap
        // -- smap doesn't use pixel shader nor render targets
        pso_desc.PS = CD3DX12_SHADER_BYTECODE(0, 0);
        pso_desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
        pso_desc.NumRenderTargets = 0;

        ThrowIfFailed(device_->CreateGraphicsPipelineState(
            &pso_desc,
            IID_PPV_ARGS(&pso_smap_)
        ));
        NAME_D3D12_OBJECT(pso_smap_);
    }
    //
    // -- create temporary cmdlist for inital gpu setup
    ComPtr<ID3D12GraphicsCommandList> cmdlist;
    ThrowIfFailed(device_->CreateCommandList(
        0 /* node mask */,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        cmdalloc_.Get(),
        pso_.Get(),
        IID_PPV_ARGS(&cmdlist)
    ));

    // -- create RTVs
    CD3DX12_CPU_DESCRIPTOR_HANDLE hrtv(
        rtv_heap_->GetCPUDescriptorHandleForHeapStart()
    );
    for (UINT i = 0; i < FrameCount; ++i) {
        ThrowIfFailed(swapchain_->GetBuffer(
            i, IID_PPV_ARGS(&render_targets_[i])
        ));
        device_->CreateRenderTargetView(
            render_targets_[i].Get(),
            nullptr, hrtv
        );
        hrtv.Offset(1, rtv_descriptor_size_);
        NAME_D3D12_OBJECT_INDEXED(render_targets_, i);
    }
    // -- create depth stencil
    {
        CD3DX12_RESOURCE_DESC shadow_tex_desc(
            D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            0 /* alignment */,
            static_cast<UINT>(viewport_.Width),
            static_cast<UINT>(viewport_.Height),
            1 /* depth or array size*/,
            1 /* mip levels */,
            DXGI_FORMAT_D32_FLOAT,
            1 /* sample count */,
            0 /* sample quality */,
            D3D12_TEXTURE_LAYOUT_UNKNOWN,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
            D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE
        );
        // NOTE(omid): Performance tip: specify desired clear value at resource creation time
        D3D12_CLEAR_VALUE clear_value;
        clear_value.Format = DXGI_FORMAT_D32_FLOAT;
        clear_value.DepthStencil.Depth = 1.0f;
        clear_value.DepthStencil.Stencil = 0;

        ThrowIfFailed(device_->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &shadow_tex_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear_value,
            IID_PPV_ARGS(&depth_stencil_)
        ));
        NAME_D3D12_OBJECT(depth_stencil_);

        device_->CreateDepthStencilView(
            depth_stencil_.Get(),
            nullptr,
            dsv_heap_->GetCPUDescriptorHandleForHeapStart()
        );
    }
    //
    // -- load scene assets
    //
    UINT file_size = 0;
    UINT8 * asset_data;
    ThrowIfFailed(ReadDataFromFile(
        GetAssetFullPath(SampleAssets::DataFilename).c_str(),
        &asset_data, &file_size
    ));
    // -- create vertex buffer:
    {
        ThrowIfFailed(device_->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::VertexDataSize),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&vb_)
        ));
        NAME_D3D12_OBJECT(vb_);
        {
            ThrowIfFailed(device_->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::VertexDataSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&vb_upload_)
            ));

            // -- copy data to upload heap and 
            // -- then schecule a copy from upload heap to vertex buffer
            D3D12_SUBRESOURCE_DATA vertex_data = {};
            vertex_data.pData = asset_data + SampleAssets::VertexDataOffset;
            vertex_data.RowPitch = SampleAssets::VertexDataSize;
            vertex_data.SlicePitch = vertex_data.RowPitch;

            PIXBeginEvent(cmdlist.Get(), 0, L"copy vertex data to default resource...");
            UpdateSubresources<1>(
                cmdlist.Get(),
                vb_.Get(),
                vb_upload_.Get(),
                0 /* intermediate offset */,
                0 /* first subresource */,
                1 /* num of subresources */,
                &vertex_data
            );
            cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                vb_.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
            ));
            PIXEndEvent(cmdlist.Get());
        }
        // -- initialize vertex buffer view
        vb_view_.BufferLocation = vb_->GetGPUVirtualAddress();
        vb_view_.SizeInBytes = SampleAssets::VertexDataSize;
        vb_view_.StrideInBytes = SampleAssets::StandardVertexStride;
    }
    // -- create index buffer:
    {
        ThrowIfFailed(device_->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::IndexDataSize),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&ib_)
        ));
        NAME_D3D12_OBJECT(ib_);
        {
            ThrowIfFailed(device_->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::IndexDataSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&ib_upload_)
            ));

            // -- copy data to upload heap and 
            // -- then schecule a copy from upload heap to index buffer
            D3D12_SUBRESOURCE_DATA index_data = {};
            index_data.pData = asset_data + SampleAssets::IndexDataOffset;
            index_data.RowPitch = SampleAssets::IndexDataSize;
            index_data.SlicePitch = index_data.RowPitch;

            PIXBeginEvent(cmdlist.Get(), 0, L"copy index data to default resource...");
            UpdateSubresources<1>(
                cmdlist.Get(),
                ib_.Get(),
                ib_upload_.Get(),
                0 /* intermediate offset */,
                0 /* first subresource */,
                1 /* num of subresources */,
                &index_data
            );
            cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                ib_.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_INDEX_BUFFER
            ));
            PIXEndEvent(cmdlist.Get());
        }
        // -- initialize index buffer view
        ib_view_.BufferLocation = ib_->GetGPUVirtualAddress();
        ib_view_.SizeInBytes = SampleAssets::IndexDataSize;
        ib_view_.Format = SampleAssets::StandardIndexFormat;
    }
    //
    // -- create shader resources
    {
        // -- get CBV SRV descriptor size for current device
        UINT const cbv_srv_descriptor_size =
            device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        // -- get handle to start of descriptor heap
        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvsrv_handle(
            cbv_srv_heap_->GetCPUDescriptorHandleForHeapStart()
        );
        {
            // -- describe and create 2 null srvs
            // NOTE(omid): null descriptors are needed to achieve the effect of an "unbound" resource 
            D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc = {};
            null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            null_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            null_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            null_srv_desc.Texture2D.MipLevels = 1;
            null_srv_desc.Texture2D.MostDetailedMip = 0;
            null_srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;

            device_->CreateShaderResourceView(
                nullptr,
                &null_srv_desc,
                cbvsrv_handle
            );
            cbvsrv_handle.Offset(cbv_srv_descriptor_size);
            device_->CreateShaderResourceView(
                nullptr,
                &null_srv_desc,
                cbvsrv_handle
            );
            cbvsrv_handle.Offset(cbv_srv_descriptor_size);
        }
        // -- create each texture and srv descriptor
        UINT const srv_count = ArrayCount(SampleAssets::Textures);
        PIXBeginEvent(cmdlist.Get(), 0, L"copy diffuse and normal data to default resource");
        for (UINT i = 0; i < srv_count; ++i) {
            // -- describe and create a Texture2D
            SampleAssets::TextureResource const & tex =
                SampleAssets::Textures[i];
            CD3DX12_RESOURCE_DESC tex_desc(
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                0 /* alignment */,
                tex.Width,
                tex.Height,
                1 /* depthorarray size */,
                static_cast<UINT16>(tex.MipLevels),
                tex.Format,
                1 /* sample count */,
                0 /* sample quality */,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_FLAG_NONE
            );
            ThrowIfFailed(device_->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &tex_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&textures_[i])
            ));
            NAME_D3D12_OBJECT_INDEXED(textures_, i);
            {
                UINT const subresource_count =
                    tex_desc.DepthOrArraySize * tex_desc.MipLevels;
                UINT64 upload_buf_size = GetRequiredIntermediateSize(
                    textures_[i].Get(), 0 /*first subresource */, subresource_count
                );
                ThrowIfFailed(device_->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                    D3D12_HEAP_FLAG_NONE,
                    &CD3DX12_RESOURCE_DESC::Buffer(upload_buf_size),
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(&texture_uploads_[i])
                ));

                // -- copy data to intermediate upload heap
                // -- and schedule a copy from upload heap to the Texture2D
                D3D12_SUBRESOURCE_DATA texture_data = {};
                texture_data.pData = asset_data + tex.Data->Offset;
                texture_data.RowPitch = tex.Data->Pitch;
                texture_data.SlicePitch = tex.Data->Size;

                UpdateSubresources(
                    cmdlist.Get(),
                    textures_[i].Get(),
                    texture_uploads_[i].Get(),
                    0 /* intermediate offset */,
                    0 /* first subresource */,
                    subresource_count,
                    &texture_data
                );
                cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                    textures_[i].Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                ));
            }
            // -- describe and create an SRV
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Format = tex.Format;
            srv_desc.Texture2D.MipLevels = tex.MipLevels;
            srv_desc.Texture2D.MostDetailedMip = 0;
            srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
            device_->CreateShaderResourceView(
                textures_[i].Get(),
                &srv_desc,
                cbvsrv_handle
            );
            cbvsrv_handle.Offset(cbv_srv_descriptor_size);
        }
        PIXEndEvent(cmdlist.Get());
    }
    free(asset_data);
    //
    // -- create samplers
    {
        // -- get the sample descriptor size for the current device
        UINT const sampler_descriptor_size =
            device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        // -- get a handle to the start of the descriptor heap
        CD3DX12_CPU_DESCRIPTOR_HANDLE sampler_handle(
            sampler_heap_->GetCPUDescriptorHandleForHeapStart()
        );

        // -- describe and create wrapping sampler (used for diffuse/normal maps)
        D3D12_SAMPLER_DESC wrap_sampler_desc = {};
        wrap_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        wrap_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        wrap_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        wrap_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        wrap_sampler_desc.MinLOD = 0;
        wrap_sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
        wrap_sampler_desc.MipLODBias = 0.0f;
        wrap_sampler_desc.MaxAnisotropy = 1;
        wrap_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        wrap_sampler_desc.BorderColor[0]
            = wrap_sampler_desc.BorderColor[1]
            = wrap_sampler_desc.BorderColor[2]
            = wrap_sampler_desc.BorderColor[3]
            = 0;
        device_->CreateSampler(&wrap_sampler_desc, sampler_handle);

        // -- move the handle to the next slot in the descriptor heap
        sampler_handle.Offset(sampler_descriptor_size);

        // -- describe and create the point clamping sampler (used for smap)
        D3D12_SAMPLER_DESC clamp_sampler_desc = {};
        clamp_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        clamp_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        clamp_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        clamp_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        clamp_sampler_desc.MinLOD = 0;
        clamp_sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
        clamp_sampler_desc.MipLODBias = 0.0f;
        clamp_sampler_desc.MaxAnisotropy = 1;
        clamp_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        clamp_sampler_desc.BorderColor[0]
            = clamp_sampler_desc.BorderColor[1]
            = clamp_sampler_desc.BorderColor[2]
            = clamp_sampler_desc.BorderColor[3]
            = 0;
        device_->CreateSampler(&clamp_sampler_desc, sampler_handle);
    }
    //
    // -- create lights
    for (int i = 0; i < NumLights; ++i) {
        // -- set up each of the light positions and directions
        // -- (they all start in same place)
        lights_[i].position = {0.0f, 15.0f, -30.0f, 1.0f};
        lights_[i].direction = {0.0f, 0.0f, 1.0f, 0.0f};
        lights_[i].falloff = {800.0f, 1.0f, 0.0f, 1.0f};
        lights_[i].color = {0.7f, 0.7f, 0.7f, 1.0f};

        XMVECTOR eye = XMLoadFloat4(&lights_[i].position);
        XMVECTOR at = XMVectorAdd(eye, XMLoadFloat4(&lights_[i].direction));
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        light_cameras_[i].Set(eye, at, up);
    }
    // -- close the cmdlist and use it to execute gpu inital setup
    ThrowIfFailed(cmdlist->Close());
    ID3D12CommandList * cmdlists [] = {cmdlist.Get()};
    cmdqueue_->ExecuteCommandLists(
        ArrayCount(cmdlists), cmdlists
    );

    // -- create frame resources
    for (int i = 0; i < FrameCount; ++i) {
        frame_resources_[i] = new FrameResource(
            device_.Get(),
            pso_.Get(), pso_smap_.Get(),
            dsv_heap_.Get(), cbv_srv_heap_.Get(),
            &viewport_, i
        );
        frame_resources_[i]->WriteCBuffers(
            &viewport_,
            &camera_,
            light_cameras_,
            lights_
        );
    }
    current_frame_resource_index_ = 0;
    current_frame_resource_ = frame_resources_[current_frame_resource_index_];

    // -- create syncronization objects
    // -- and wait until assets hav been uploaded to gpu
    {
        ThrowIfFailed(device_->CreateFence(
            fence_value_, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&fence_)
        ));
        ++fence_value_;

        // -- create an event handle to use for frame synchronization
        fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (nullptr == fence_event_)
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

        // -- wait for the cmdlist to execute
        // NOTE(omid): we're reusing same cmdlist in main loop but for now
        // (we just want to wait for the setup to complete)

        // -- signal and increment fence value
        UINT64 const fence_to_wait_for = fence_value_;
        ThrowIfFailed(cmdqueue_->Signal(fence_.Get(), fence_to_wait_for));
        ++fence_value_;

        // -- wait until fence is completed
        ThrowIfFailed(
            fence_->SetEventOnCompletion(fence_to_wait_for, fence_event_)
        );
        WaitForSingleObject(fence_event_, INFINITE);
    }
}
//
// -- initialize events and threads
void OdxMultithreading::LoadContexts () {
#if !SINGLETHREADED
    struct ThreadWrapper {
        static unsigned int WINAPI thunk (LPVOID param_ptr) {
            ThreadParameter * param = reinterpret_cast<ThreadParameter *>(param);
            OdxMultithreading::Get()->WorkerThread(param->thread_index);
            return 0;
        }
    };
    for (int i = 0; i < NumContexts; ++i) {
        worker_begin_render_frame_[i] = CreateEvent(
            NULL,
            FALSE,
            FALSE,
            NULL
        );
        worker_finished_render_frame_[i] = CreateEvent(
            NULL,
            FALSE,
            FALSE,
            NULL
        );
        worker_finish_shadow_pass_[i] = CreateEvent(
            NULL,
            FALSE,
            FALSE,
            NULL
        );
        thread_parameters_[i].thread_index = i;
        thread_handles_[i] = reinterpret_cast<HANDLE>(_beginthreadex(
            nullptr, 0,
            ThreadWrapper::thunk,
            reinterpret_cast<LPVOID>(&thread_parameters_[i]),
            0, nullptr
        ));
        assert(worker_begin_render_frame_[i] != NULL);
        assert(worker_finished_render_frame_[i] != NULL);
        assert(thread_handles_[i] != NULL);
    }
#endif // !SINGLETHREADED
}
void OdxMultithreading::RestoreD3DResources ();
void OdxMultithreading::ReleaseD3DResources ();
void OdxMultithreading::WaitForGpu ();
void OdxMultithreading::BeginFrame ();
void OdxMultithreading::MidFrame ();
void OdxMultithreading::EndFrame ();
OdxMultithreading::OdxMultithreading (UINT width, UINT height, std::wstring name);
OdxMultithreading::~OdxMultithreading ();

void OdxMultithreading::OnInit ();
void OdxMultithreading::OnUpdate ();
void OdxMultithreading::OnRender ();
void OdxMultithreading::OnDestroy ();
void OdxMultithreading::OnKeyDown (UINT8 key);
void OdxMultithreading::OnKeyUp (UINT8 key);

