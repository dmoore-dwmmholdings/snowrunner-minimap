#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
#include "dxgi_bridge_api.h"
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks{};
void check(bool value,const char* what) { ++checks; if (!value) throw std::runtime_error(what); }
void ok(HRESULT value,const char* what) { check(SUCCEEDED(value),what); }
struct Window {
    HWND value{};
    Window() {
        WNDCLASSW klass{};
        klass.hInstance=GetModuleHandleW(nullptr);
        klass.lpfnWndProc=DefWindowProcW;
        klass.lpszClassName=L"SrmmOwnedDxgiRoute";
        check(RegisterClassW(&klass)!=0,"Register owned window class");
        value=CreateWindowExW(0,klass.lpszClassName,L"Owned DXGI validation",WS_OVERLAPPEDWINDOW,0,0,256,256,
                              nullptr,nullptr,klass.hInstance,nullptr);
        check(value!=nullptr,"Create hidden owned window");
    }
    ~Window() { if(value) DestroyWindow(value); UnregisterClassW(L"SrmmOwnedDxgiRoute",GetModuleHandleW(nullptr)); }
};
}
int main(int argc,char**) {
    if (argc!=1) { std::cerr<<"Owned host accepts no target or path arguments.\n"; return 2; }
    try {
        HMODULE bridge{};
        check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              reinterpret_cast<LPCWSTR>(&CreateDXGIFactory1),&bridge)!=FALSE,"Resolve our imported factory provider");
        // MSVC may take the address of an executable import thunk. Validate the
        // module export and subsequent wrapper counts instead of mistaking that
        // thunk for the factory implementation.
        bridge=GetModuleHandleW(L"dxgi.dll");
        auto stats=reinterpret_cast<BOOL(WINAPI*)(SrmmBridgeStats*)>(GetProcAddress(bridge,"srmm_bridge_stats"));
        check(stats!=nullptr,"Ordinary executable imports selected our bridge");
        wchar_t systemPath[32768]{};check(GetSystemDirectoryW(systemPath,32740)!=0,"Find system library directory");wcscat_s(systemPath,L"\\dxgi.dll");
        HMODULE system=LoadLibraryExW(systemPath,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(system&&system!=bridge,"System DXGI is distinct from bridge");
        const char* names[]={"ApplyCompatResolutionQuirking","CompatString","CompatValue","DXGIDumpJournal","PIXBeginCapture","PIXEndCapture","PIXGetCaptureState","SetAppCompatStringPointer","UpdateHMDEmulationStatus","CreateDXGIFactory","CreateDXGIFactory1","CreateDXGIFactory2","DXGID3D10CreateDevice","DXGID3D10CreateLayeredDevice","DXGID3D10GetLayeredDeviceSize","DXGID3D10RegisterLayers","DXGIDeclareAdapterRemovalSupport","DXGIDisableVBlankVirtualization","DXGIGetDebugInterface1","DXGIReportAdapterConfiguration"};
        for(unsigned i=0;i<20;++i) {
            const auto named=GetProcAddress(bridge,names[i]);
            check(named&&named==GetProcAddress(bridge,MAKEINTRESOURCEA(i+1)),"Named and ordinal exports match system layout");
            if(i<9||i>11)check(named==GetProcAddress(system,names[i]),"Forwarded export resolves directly to installed system DXGI");
        }
#ifdef SRMM_LOCAL_FACTORY_TEST
        wchar_t d3dPath[32768]{};check(GetSystemDirectoryW(d3dPath,32740)!=0,"Find system D3D directory");
        wcscat_s(d3dPath,L"\\d3d11.dll");
        const auto nativeD3D=LoadLibraryExW(d3dPath,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto proxyD3D=GetModuleHandleW(L"d3d11.dll");
        check(nativeD3D&&proxyD3D&&nativeD3D!=proxyD3D,"Device startup bridge distinct from system runtime");
        check(GetProcAddress(proxyD3D,"CreateDirect3D11DeviceFromDXGIDevice")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(16)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"CreateDirect3D11DeviceFromDXGIDevice")==GetProcAddress(nativeD3D,"CreateDirect3D11DeviceFromDXGIDevice"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"CreateDirect3D11SurfaceFromDXGISurface")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(17)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"CreateDirect3D11SurfaceFromDXGISurface")==GetProcAddress(nativeD3D,"CreateDirect3D11SurfaceFromDXGISurface"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3D11CoreCreateDevice")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(18)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3D11CoreCreateDevice")==GetProcAddress(nativeD3D,"D3D11CoreCreateDevice"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3D11CoreCreateLayeredDevice")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(19)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3D11CoreCreateLayeredDevice")==GetProcAddress(nativeD3D,"D3D11CoreCreateLayeredDevice"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3D11CoreGetLayeredDeviceSize")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(20)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3D11CoreGetLayeredDeviceSize")==GetProcAddress(nativeD3D,"D3D11CoreGetLayeredDeviceSize"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3D11CoreRegisterLayers")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(21)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3D11CoreRegisterLayers")==GetProcAddress(nativeD3D,"D3D11CoreRegisterLayers"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3D11CreateDevice")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(22)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3D11CreateDeviceAndSwapChain")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(23)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3D11CreateDeviceAndSwapChain")==GetProcAddress(nativeD3D,"D3D11CreateDeviceAndSwapChain"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3D11CreateDeviceForD3D12")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(1)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3D11CreateDeviceForD3D12")==GetProcAddress(nativeD3D,"D3D11CreateDeviceForD3D12"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3D11On12CreateDevice")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(24)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3D11On12CreateDevice")==GetProcAddress(nativeD3D,"D3D11On12CreateDevice"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTCloseAdapter")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(2)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTCloseAdapter")==GetProcAddress(nativeD3D,"D3DKMTCloseAdapter"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTCreateAllocation")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(25)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTCreateAllocation")==GetProcAddress(nativeD3D,"D3DKMTCreateAllocation"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTCreateContext")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(26)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTCreateContext")==GetProcAddress(nativeD3D,"D3DKMTCreateContext"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTCreateDevice")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(27)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTCreateDevice")==GetProcAddress(nativeD3D,"D3DKMTCreateDevice"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTCreateSynchronizationObject")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(28)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTCreateSynchronizationObject")==GetProcAddress(nativeD3D,"D3DKMTCreateSynchronizationObject"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTDestroyAllocation")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(3)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTDestroyAllocation")==GetProcAddress(nativeD3D,"D3DKMTDestroyAllocation"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTDestroyContext")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(4)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTDestroyContext")==GetProcAddress(nativeD3D,"D3DKMTDestroyContext"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTDestroyDevice")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(5)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTDestroyDevice")==GetProcAddress(nativeD3D,"D3DKMTDestroyDevice"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTDestroySynchronizationObject")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(6)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTDestroySynchronizationObject")==GetProcAddress(nativeD3D,"D3DKMTDestroySynchronizationObject"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTEscape")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(29)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTEscape")==GetProcAddress(nativeD3D,"D3DKMTEscape"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTGetContextSchedulingPriority")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(30)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTGetContextSchedulingPriority")==GetProcAddress(nativeD3D,"D3DKMTGetContextSchedulingPriority"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTGetDeviceState")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(31)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTGetDeviceState")==GetProcAddress(nativeD3D,"D3DKMTGetDeviceState"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTGetDisplayModeList")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(32)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTGetDisplayModeList")==GetProcAddress(nativeD3D,"D3DKMTGetDisplayModeList"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTGetMultisampleMethodList")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(33)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTGetMultisampleMethodList")==GetProcAddress(nativeD3D,"D3DKMTGetMultisampleMethodList"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTGetRuntimeData")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(34)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTGetRuntimeData")==GetProcAddress(nativeD3D,"D3DKMTGetRuntimeData"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTGetSharedPrimaryHandle")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(35)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTGetSharedPrimaryHandle")==GetProcAddress(nativeD3D,"D3DKMTGetSharedPrimaryHandle"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTLock")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(36)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTLock")==GetProcAddress(nativeD3D,"D3DKMTLock"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTOpenAdapterFromHdc")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(37)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTOpenAdapterFromHdc")==GetProcAddress(nativeD3D,"D3DKMTOpenAdapterFromHdc"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTOpenResource")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(38)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTOpenResource")==GetProcAddress(nativeD3D,"D3DKMTOpenResource"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTPresent")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(7)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTPresent")==GetProcAddress(nativeD3D,"D3DKMTPresent"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTQueryAdapterInfo")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(8)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTQueryAdapterInfo")==GetProcAddress(nativeD3D,"D3DKMTQueryAdapterInfo"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTQueryAllocationResidency")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(39)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTQueryAllocationResidency")==GetProcAddress(nativeD3D,"D3DKMTQueryAllocationResidency"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTQueryResourceInfo")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(40)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTQueryResourceInfo")==GetProcAddress(nativeD3D,"D3DKMTQueryResourceInfo"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTRender")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(41)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTRender")==GetProcAddress(nativeD3D,"D3DKMTRender"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTSetAllocationPriority")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(42)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTSetAllocationPriority")==GetProcAddress(nativeD3D,"D3DKMTSetAllocationPriority"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTSetContextSchedulingPriority")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(43)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTSetContextSchedulingPriority")==GetProcAddress(nativeD3D,"D3DKMTSetContextSchedulingPriority"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTSetDisplayMode")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(44)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTSetDisplayMode")==GetProcAddress(nativeD3D,"D3DKMTSetDisplayMode"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTSetDisplayPrivateDriverFormat")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(9)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTSetDisplayPrivateDriverFormat")==GetProcAddress(nativeD3D,"D3DKMTSetDisplayPrivateDriverFormat"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTSetGammaRamp")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(45)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTSetGammaRamp")==GetProcAddress(nativeD3D,"D3DKMTSetGammaRamp"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTSetVidPnSourceOwner")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(46)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTSetVidPnSourceOwner")==GetProcAddress(nativeD3D,"D3DKMTSetVidPnSourceOwner"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTSignalSynchronizationObject")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(10)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTSignalSynchronizationObject")==GetProcAddress(nativeD3D,"D3DKMTSignalSynchronizationObject"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTUnlock")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(11)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTUnlock")==GetProcAddress(nativeD3D,"D3DKMTUnlock"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTWaitForSynchronizationObject")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(12)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTWaitForSynchronizationObject")==GetProcAddress(nativeD3D,"D3DKMTWaitForSynchronizationObject"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DKMTWaitForVerticalBlankEvent")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(47)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DKMTWaitForVerticalBlankEvent")==GetProcAddress(nativeD3D,"D3DKMTWaitForVerticalBlankEvent"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DPerformance_BeginEvent")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(48)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DPerformance_BeginEvent")==GetProcAddress(nativeD3D,"D3DPerformance_BeginEvent"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DPerformance_EndEvent")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(49)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DPerformance_EndEvent")==GetProcAddress(nativeD3D,"D3DPerformance_EndEvent"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DPerformance_GetStatus")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(50)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DPerformance_GetStatus")==GetProcAddress(nativeD3D,"D3DPerformance_GetStatus"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"D3DPerformance_SetMarker")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(51)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"D3DPerformance_SetMarker")==GetProcAddress(nativeD3D,"D3DPerformance_SetMarker"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"EnableFeatureLevelUpgrade")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(13)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"EnableFeatureLevelUpgrade")==GetProcAddress(nativeD3D,"EnableFeatureLevelUpgrade"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"OpenAdapter10")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(14)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"OpenAdapter10")==GetProcAddress(nativeD3D,"OpenAdapter10"),"D3D untouched export forwards to system");
        check(GetProcAddress(proxyD3D,"OpenAdapter10_2")==GetProcAddress(proxyD3D,MAKEINTRESOURCEA(15)),"D3D named/ordinal export agrees");
        check(GetProcAddress(proxyD3D,"OpenAdapter10_2")==GetProcAddress(nativeD3D,"OpenAdapter10_2"),"D3D untouched export forwards to system");
        FreeLibrary(nativeD3D);
#endif
        FreeLibrary(system);
        auto snapshot=[&] { SrmmBridgeStats value{}; value.size=sizeof(value); check(stats(&value)!=FALSE,"Read bridge counts"); return value; };
        Window window;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL wanted[]={D3D_FEATURE_LEVEL_11_0};
        ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,wanted,1,D3D11_SDK_VERSION,
                            &device,nullptr,&context),"Create actual owned WARP device through forwarded DXGI exports");
#ifdef SRMM_LOCAL_FACTORY_TEST
        // Identity must remain native along both context and resource return paths.
        ComPtr<ID3D11Device> contextDevice;context->GetDevice(&contextDevice);
        check(contextDevice.Get()==device.Get(),"Context GetDevice preserves native device identity");
        D3D11_BUFFER_DESC bufferDesc{};bufferDesc.ByteWidth=16;bufferDesc.Usage=D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Buffer> identityBuffer;ok(device->CreateBuffer(&bufferDesc,nullptr,&identityBuffer),"Create identity test resource");
        ComPtr<ID3D11Device> resourceDevice;identityBuffer->GetDevice(&resourceDevice);
        check(resourceDevice.Get()==device.Get(),"Resource GetDevice preserves native device identity");
        ComPtr<IDXGIDevice> identityDxgi;ok(device.As(&identityDxgi),"Query native DXGI device identity");
        ComPtr<IUnknown> identityD3D,identityDXGI;
        ok(device.As(&identityD3D),"D3D device IUnknown");ok(identityDxgi.As(&identityDXGI),"DXGI device IUnknown");
        check(identityD3D.Get()==identityDXGI.Get(),"D3D and DXGI device interfaces share native identity");
        auto routeDevice=reinterpret_cast<BOOL(WINAPI*)(ID3D11Device*)>(GetProcAddress(bridge,"srmm_route_device"));
        check(routeDevice!=nullptr,"Explicit route export exists");
        const auto beforeRepeat=snapshot();check(routeDevice(device.Get())!=FALSE,"Repeated registration succeeds");
        check(snapshot().factories==beforeRepeat.factories,"Repeated device registration is idempotent");
        identityD3D.Reset();identityDXGI.Reset();identityDxgi.Reset();resourceDevice.Reset();identityBuffer.Reset();contextDevice.Reset();
#endif
        ComPtr<IDXGIFactory1> factory;
        ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create wrapped factory");
        auto counts=snapshot();
        check(counts.factories>=1 && counts.live_factories>=1,"Factory wrapper active");
        ComPtr<IDXGIFactory7> factory7;
        ok(factory.As(&factory7),"Factory interface negotiation");
        ComPtr<IUnknown> first,second;
        ok(factory.As(&first),"Factory identity from v1");
        ok(factory7.As(&second),"Factory identity from v7");
        check(first.Get()==second.Get(),"Factory COM identity preserved across wrapper interfaces");
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width=desc.BufferDesc.Height=128;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferCount=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SampleDesc.Count=1;
        desc.SwapEffect=DXGI_SWAP_EFFECT_SEQUENTIAL;
        desc.OutputWindow=window.value;
        desc.Windowed=TRUE;
        // Regression: normal device ancestry bypasses an export-only factory wrapper.
        // This is an actual WARP runtime object graph, not a fabricated COM fixture.
        {
            ComPtr<IDXGIDevice> dxgiDevice;
            ok(device.As(&dxgiDevice),"Device exposes native DXGI ancestry");
            ComPtr<IDXGIAdapter> adapter;
            ok(dxgiDevice->GetAdapter(&adapter),"Get actual device adapter");
            ComPtr<IDXGIFactory> nativeFactory;
            ok(adapter->GetParent(IID_PPV_ARGS(&nativeFactory)),"Get factory through device ancestry");
            const auto beforeNative=snapshot();
            ComPtr<IDXGISwapChain> nativeSwap;
            ok(nativeFactory->CreateSwapChain(device.Get(),&desc,&nativeSwap),"Create swap through native ancestry");
            ok(nativeSwap->Present(0,DXGI_PRESENT_TEST),"Present through native ancestry");
            const auto afterNative=snapshot();
#ifdef SRMM_LOCAL_FACTORY_TEST
            check(afterNative.swaps==beforeNative.swaps+1 && afterNative.presents==beforeNative.presents+1,
                  "Instance route observes device ancestry without replacing device identity");
#else
            check(afterNative.swaps==beforeNative.swaps && afterNative.presents==beforeNative.presents,
                  "Export-only bridge cannot observe the device ancestry route");
#endif
        }
        ComPtr<IDXGISwapChain> swap;
        ok(factory->CreateSwapChain(device.Get(),&desc,&swap),"Create actual wrapped swap chain");
        ComPtr<IDXGISwapChain4> swap4;
        ok(swap.As(&swap4),"Swap interface negotiation");
        ComPtr<IUnknown> swapIdentity;
        ok(swap.As(&swapIdentity),"Swap identity");
        ComPtr<IUnknown> swapIdentity4;
        ok(swap4.As(&swapIdentity4),"Swap v4 identity");
        check(swapIdentity.Get()==swapIdentity4.Get(),"Swap COM identity preserved");
        ComPtr<IUnknown> parent;
        ok(swap->GetParent(IID_PPV_ARGS(&parent)),"Swap parent reference");
        check(parent.Get()==first.Get(),"Swap parent remains wrapped factory");
        GUID unknown={0x454c1399,0x1270,0x4d93,{0x80,0x09,0x31,0x01,0x01,0x01,0x11,0x09}};
        void* missing=reinterpret_cast<void*>(1);
        check(swap->QueryInterface(unknown,&missing)==E_NOINTERFACE && !missing,"Unknown interface safely rejected");
        ComPtr<ID3D11Device> returnedDevice;
        ok(swap->GetDevice(IID_PPV_ARGS(&returnedDevice)),"GetDevice forwards");
        check(returnedDevice.Get()==device.Get(),"Underlying device preserved");
        const auto before=snapshot();
        ok(swap->Present(0,DXGI_PRESENT_TEST),"Present forwards without displaying owned window");
        check(snapshot().rendered==0,"TEST presents never draw");
        ComPtr<ID3D11Texture2D> backbuffer;
        ok(swap->GetBuffer(0,IID_PPV_ARGS(&backbuffer)),"Get owned buffer for render validation");
        ComPtr<ID3D11RenderTargetView> target;
        ok(device->CreateRenderTargetView(backbuffer.Get(),nullptr,&target),"Create host target");
        const float blue[]={0,0,1,1};context->ClearRenderTargetView(target.Get(),blue);
        auto* hostTarget=target.Get();context->OMSetRenderTargets(1,&hostTarget,nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        D3D11_VIEWPORT hostViewport{3,5,31,37,.1f,.9f};context->RSSetViewports(1,&hostViewport);
        ok(swap->Present(0,0),"Execute real Present with owned minimap drawing");
        check(snapshot().rendered==1&&snapshot().render_failures==0,"Bridge drew through the native Present route");
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
        check(topology==D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,"Host topology restored after renderer");
        UINT viewportCount=1;D3D11_VIEWPORT afterViewport{};context->RSGetViewports(&viewportCount,&afterViewport);
        check(viewportCount==1&&afterViewport.TopLeftX==3&&afterViewport.Width==31&&afterViewport.MaxDepth==.9f,"Host viewport restored");
        ComPtr<ID3D11RenderTargetView> afterTarget;context->OMGetRenderTargets(1,&afterTarget,nullptr);
        check(afterTarget.Get()==target.Get(),"Host render target restored");afterTarget.Reset();
        D3D11_TEXTURE2D_DESC readDesc{};backbuffer->GetDesc(&readDesc);readDesc.Usage=D3D11_USAGE_STAGING;readDesc.BindFlags=0;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;readDesc.MiscFlags=0;
        ComPtr<ID3D11Texture2D> readback;ok(device->CreateTexture2D(&readDesc,nullptr,&readback),"Create pixel readback");
        context->CopyResource(readback.Get(),backbuffer.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        ok(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"Read native minimap pixels");
        auto pixel=[&](unsigned x,unsigned y) {return reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)[x];};
        const auto outside=pixel(0,0),center=pixel(64,64),mapPixel=pixel(40,64);
        context->Unmap(readback.Get(),0);
        check(outside==0xffff0000,"Host pixels outside minimap preserved");
        check(center==0xffffffff,"Player marker drawn into actual swap-chain buffer");
        check(mapPixel==0xff006000,"Original map texture drawn into actual swap-chain buffer");
        readback.Reset();backbuffer.Reset();target.Reset();context->OMSetRenderTargets(0,nullptr,nullptr);
        ok(swap->ResizeBuffers(1,192,160,DXGI_FORMAT_UNKNOWN,0),"Resize forwards");
        DXGI_SWAP_CHAIN_DESC resized{};
        ok(swap->GetDesc(&resized),"Query resized buffers");
        check(resized.BufferDesc.Width==192 && resized.BufferDesc.Height==160,"Underlying buffers really resized");
        counts=snapshot();
        check(counts.presents==before.presents+2 && counts.resizes==before.resizes+1,"Only owned callbacks counted");
        first.Reset(); second.Reset(); factory7.Reset(); factory.Reset(); parent.Reset();
        check(snapshot().live_factories>=1,"Swap retains its factory owner");
        returnedDevice.Reset(); swapIdentity.Reset(); swapIdentity4.Reset(); swap4.Reset(); swap.Reset();
        counts=snapshot();
#ifdef SRMM_LOCAL_FACTORY_TEST
        check(counts.live_swaps==0 && counts.live_factories>=1,"Swaps drain; bounded native factory routes remain resident");
#else
        check(counts.live_swaps==0 && counts.live_factories==0,"Wrapper objects drain to zero after final release");
#endif
        context->ClearState(); context.Reset(); device.Reset();
        std::cout<<"PASS: "<<checks<<" actual DXGI forwarding/COM lifecycle checks; owned hidden WARP host only.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"DXGI host failed: "<<error.what()<<'\n'; return 1; }
}

