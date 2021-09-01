#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include "d3dx12.h"
#include <pix3.h>

#include <directxmath.h>

#include <string>
#include <wrl.h>
#include <process.h>

// -- include helpers to work with command line arguments
#include <shellapi.h>

static constexpr UINT FrameCount = 3;

static constexpr UINT NumContexts = 3;
static constexpr UINT NumLights = 3;    // -- update shader code if changed

// -- number of frames to not update the titlebar
static constexpr UINT TitlebarThrottle = 200; 

// -- cmdlist submissions (from main thread)
static constexpr int CmdlistCount = 3;
static constexpr int CmdlistPre = 0;
static constexpr int CmdlistMid = 1;
static constexpr int CmdlistPost = 2;


