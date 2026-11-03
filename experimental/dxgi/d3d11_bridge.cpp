#include <d3d11_exports.inc>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cwchar>
extern "C" __declspec(dllimport) BOOL WINAPI srmm_route_device(ID3D11Device*);
extern "C" HRESULT WINAPI BridgeD3D11CreateDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,
    UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,ID3D11Device** device,
    D3D_FEATURE_LEVEL* selected,ID3D11DeviceContext** context) {
    static HMODULE module=[]() noexcept {
        wchar_t path[32768]{};
        if(!GetSystemDirectoryW(path,32740))return static_cast<HMODULE>(nullptr);
        wcscat_s(path,L"\\d3d11.dll");
        return LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    const auto call=module?reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(module,"D3D11CreateDevice")):nullptr;
    if(!call)return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    const auto result=call(adapter,type,software,flags,levels,count,sdk,device,selected,context);
    if(SUCCEEDED(result)&&device&&*device)srmm_route_device(*device);
    return result;
}
