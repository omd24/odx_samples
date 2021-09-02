#pragma once

#include <stdexcept>

// NOTE(omid): ComPtr manages lifetime on cpu side 
// and is agnostic toward gpu lifetime of a resource 
// so be cautious when destorying gpu objects
using Microsoft::WRL::ComPtr;

inline std::string
Hr2String (HRESULT hr) {
    char str[64] = {};
    sprintf_s(str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
    return std::string(str);
}

struct HrException : public std::runtime_error {
private:
    HRESULT const hr_;
public:
    HrException (HRESULT hr) : std::runtime_error(Hr2String(hr)), hr_(hr) {}
    HRESULT Error () const { return hr_; }
};

#define SAFE_RELEASE(p) if (p) (p)->Release()

inline void
ThrowIfFailed (HRESULT hr) {
    if (FAILED(hr))
        throw HrException(hr);
}
inline void
GetAssetsPath (_Out_writes_(path_size) WCHAR * path, UINT path_size) {
    if (nullptr == path)
        throw std::exception();
    DWORD size = GetModuleFileName(nullptr, path, path_size);
    if (0 == size || size == path_size) // method failure or truncated path
        throw std::exception();
    // -- scans the path string for the last occurrence of slash.
    WCHAR * last_slash = wcsrchr(path, L'\\');
    if (last_slash)
        *(last_slash + 1) = L'\0';
}
inline HRESULT
ReadDataFromFile (LPCWSTR filename, byte ** data, UINT * size) {
    #if WINVER <= _WIN32_WINNT_WIN8
    throw std::exception();     // -- not supporting systems older than Win8
    #endif
    CREATEFILE2_EXTENDED_PARAMETERS params = {};
    params.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
    params.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    params.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
    params.dwSecurityQosFlags = SECURITY_ANONYMOUS;
    params.lpSecurityAttributes = nullptr;
    params.hTemplateFile = nullptr;

    Microsoft::WRL::Wrappers::FileHandle file(CreateFile2(
        filename,
        GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
        &params
    ));
    if (INVALID_HANDLE_VALUE == file.Get())
        throw std::exception();
    FILE_STANDARD_INFO file_info = {};
    if (
        FALSE == GetFileInformationByHandleEx(
        file.Get(), FileStandardInfo, &file_info, sizeof(file_info)
        )) {
        throw std::exception();
    }

    if (0 != file_info.EndOfFile.HighPart)
        throw std::exception();

    *data = reinterpret_cast<byte *>(malloc(file_info.EndOfFile.LowPart));
    *size = file_info.EndOfFile.LowPart;
    if (
        FALSE == ReadFile(
        file.Get(), *data, file_info.EndOfFile.LowPart, nullptr, nullptr
        )) {
        throw std::exception();
    }
    return S_OK;
}
inline HRESULT
ReadDataFromDDSFile (
    LPCWSTR filename, byte ** data, UINT * offset, UINT * size
) {
    if (FAILED(ReadDataFromFile(filename, data, size)))
        throw std::exception();
    // -- dds files start with a magic number
    static constexpr UINT DDS_MAGIC = 0x20534444;
    UINT magic_number = *reinterpret_cast<UINT const *>(*data);
    if (magic_number != DDS_MAGIC)
        return E_FAIL;
    struct DDS_PIXELFORMAT {
        UINT size;
        UINT flags;
        UINT four_cc;
        UINT rgb_bit_count;
        UINT r_bit_mask;
        UINT g_bit_mask;
        UINT b_bit_mask;
        UINT a_bit_mask;
    };
    struct DDS_HEADER {
        UINT size;
        UINT flags;
        UINT height;
        UINT width;
        UINT pitch_or_linear_size;
        UINT depth;
        UINT mipmap_count;
        UINT reserved1[11];
        DDS_PIXELFORMAT dds_pixel_format;
        UINT caps;
        UINT caps2;
        UINT caps3;
        UINT caps4;
        UINT reserved2;
    };

    DDS_HEADER const * header =
        reinterpret_cast<DDS_HEADER const *>(*data + sizeof(UINT));
    if (
        sizeof(DDS_HEADER) != header->size ||
        sizeof(DDS_PIXELFORMAT) != header->dds_pixel_format.size
    ) {
        return E_FAIL;
    }
    ptrdiff_t const data_offset = sizeof(UINT) + sizeof(DDS_HEADER);
    *offset = data_offset;
    *size = *size - data_offset;

    return S_OK;
}

// -- setting objects names for debugging purposes
#if defined(_DEBUG) || defined(DBG)
inline void
SetName (ID3D12Object * obj, LPCWSTR name) {
    obj->SetName(name);
}
inline void
SetNameIndexed (ID3D12Object * obj, LPCWSTR name, UINT index) {
    WCHAR fullname[50];
    if (swprintf_s(fullname, L"%s[%u]", name, index) > 0)
        obj->SetName(fullname);
}
#else
inline void
SetName (ID3D12Object * obj, LPCWSTR name) {}
inline void
SetNameIndexed (ID3D12Object * obj, LPCWSTR name, UINT index) {}
#endif

// -- naming helpers for ComPtr<T>
#define NAME_D3D12_OBJECT(x) SetName((x).Get(), L#x)
#define NAME_D3D12_OBJECT_INDEXED(x, n) SetNameIndexed((x)[n].Get(), L#x, n)

inline constexpr UINT
CalculateCBufferByteSize (UINT byte_size) {
    UINT ret =
        (byte_size + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) &
        ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
    return ret;
}

#ifdef D3D_COMPILE_STANDARD_FILE_INCLUDE
inline Microsoft::WRL::ComPtr<ID3DBlob>
CompileShader (
    std::wstring const & filename,
    D3D_SHADER_MACRO const * defines,
    std::string const & entrypoint,
    std::string const & target
) {
    UINT compile_flags = 0;
#if defined(_DEBUG) || defined(DBG)
    compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr;
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    hr = D3DCompileFromFile(
        filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entrypoint.c_str(), target.c_str(), compile_flags, 0,
        &bytecode, &errors
    );
    if (errors != nullptr)
        OutputDebugStringA((char *)errors->GetBufferPointer());
    ThrowIfFailed(hr);
    return bytecode;
}
#endif

// -- reset array of ComPtr
template <typename T>
inline void
ResetComPtrArray (T * comptr_array) {
    for (auto & i : *comptr_array)
        i.Reset();
}
// -- reset elements in a unique_ptr array
template <typename T>
inline void
ResetUniquePtrArray (T * unique_ptr_array) {
    for (auto & i : *unique_ptr_array)
        i.reset();
}

