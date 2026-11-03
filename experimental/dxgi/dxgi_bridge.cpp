#include <dxgi_exports.inc>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <cwchar>
#include "dxgi_bridge_api.h"
#include "shared_renderer.h"
#include <array>
#include "bootstrap_probe.h"
using Microsoft::WRL::ComPtr;
namespace {
std::atomic<unsigned long long> factories{}, swaps{}, presents{}, resizes{}, live_factories{}, live_swaps{}, unknown_queries{};
std::atomic<unsigned long long> rendered{},render_failures{};
// No DllMain, worker, remote call, import write or code detour.
// The opt-in factory route replaces a guarded native instance dispatch pointer.
HMODULE system_dxgi() noexcept {
    static HMODULE module = []() noexcept -> HMODULE {
        wchar_t path[32768]{};
        const auto n = GetSystemDirectoryW(path, 32740);
        if (!n || n >= 32740) return nullptr;
        wcscat_s(path,L"\\dxgi.dll");
        return LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    }(); // Retain our system-module reference until process exit.
    return module;
}
template<class F> F entry(const char* name) noexcept {
    auto module = system_dxgi();
    return module ? reinterpret_cast<F>(GetProcAddress(module,name)) : nullptr;
}
bool enabled() noexcept {
    // Owned-host validation ONLY until a separately reviewed deployment exists.
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr,path,32768)) return false;
    const auto* slash = wcsrchr(path,L'\\');
    return _wcsicmp(slash ? slash+1 : path,L"srmm_dxgi_host.exe") == 0;
}
bool wrapped_application() noexcept { return enabled()||srmm_verified_game(); }
void retain_bridge() noexcept {
    static HMODULE held = []() noexcept {
        HMODULE module{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          reinterpret_cast<LPCWSTR>(&retain_bridge),&module);
        return module;
    }();
    (void)held; // Resident dispatcher; no hot-unload support.
}
bool factory_iid(REFIID id) {
    return id==__uuidof(IUnknown) || id==__uuidof(IDXGIObject) || id==__uuidof(IDXGIFactory) ||
        id==__uuidof(IDXGIFactory1) || id==__uuidof(IDXGIFactory2) || id==__uuidof(IDXGIFactory3) ||
        id==__uuidof(IDXGIFactory4) || id==__uuidof(IDXGIFactory5) || id==__uuidof(IDXGIFactory6) || id==__uuidof(IDXGIFactory7);
}
bool swap_iid(REFIID id) {
    return id==__uuidof(IUnknown) || id==__uuidof(IDXGIObject) || id==__uuidof(IDXGIDeviceSubObject) ||
        id==__uuidof(IDXGISwapChain) || id==__uuidof(IDXGISwapChain1) || id==__uuidof(IDXGISwapChain2) ||
        id==__uuidof(IDXGISwapChain3) || id==__uuidof(IDXGISwapChain4);
}
class Swap final : public IDXGISwapChain4 {
    std::atomic<ULONG> refs_{1};
    ComPtr<IDXGISwapChain4> real_;
    ComPtr<IUnknown> parent_;
    SharedRenderer renderer_;
    bool initialized_=false,failed_=false;
    void draw_owned_fixture(UINT flags) noexcept {
        // This shader path is only reachable in the explicitly named owned host.
        if((flags&DXGI_PRESENT_TEST)||!enabled()||failed_) return;
        if(!initialized_) {
            ComPtr<ID3D11Device> device;
            if(FAILED(real_->GetDevice(IID_PPV_ARGS(&device)))) {failed_=true;++render_failures;return;}
            std::array<std::uint32_t,64*64> map{};map.fill(0xff006000);
            if(FAILED(renderer_.initialize(device.Get(),map.data(),64,64))) {failed_=true;++render_failures;return;}
            initialized_=true;
        }
        if(SUCCEEDED(renderer_.draw(real_.Get(),SrmmMapFrame{}))) ++rendered;
        else { failed_=true;++render_failures;renderer_.reset(); }
    }
public:
    Swap(IDXGISwapChain4* value,IUnknown* parent) : real_(value),parent_(parent) { ++swaps; ++live_swaps; srmm_probe_log("swap-created",swaps.load()); }
    ~Swap() { --live_swaps; srmm_probe_log("swap-released",live_swaps.load()); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if (!out) return E_POINTER;
        *out=nullptr;
        if (!swap_iid(id)) { ++unknown_queries; srmm_probe_log("swap-unknown-interface",id.Data1,unknown_queries.load());return E_NOINTERFACE; }
        *out=static_cast<IDXGISwapChain4*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** out) override { return parent_->QueryInterface(id,out); }
    HRESULT STDMETHODCALLTYPE Present(UINT sync,UINT flags) override {
        ++presents;
        if(presents.load()==1||presents.load()%300==0)srmm_probe_log("present",presents.load(),flags);
        draw_owned_fixture(flags);
        return real_->Present(sync,flags); // Normal virtual call, preserving the underlying implementation.
    }
    HRESULT STDMETHODCALLTYPE Present1(UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* parameters) override {
        ++presents;if(presents.load()==1||presents.load()%300==0)srmm_probe_log("present1",presents.load(),flags);draw_owned_fixture(flags); return real_->Present1(sync,flags,parameters);
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) override {
        ++resizes;srmm_probe_log("resize",width,height);return real_->ResizeBuffers(count,width,height,format,flags);
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags,
                                             const UINT* masks,IUnknown* const* queues) override {
        ++resizes; return real_->ResizeBuffers1(count,width,height,format,flags,masks,queues);
    }
#include "swap_forwarders.inc"
};
template<class T> void wrap_swap(T** out,IUnknown* parent) noexcept {
    if (!out || !*out) return;
    ComPtr<IDXGISwapChain4> real;
    const auto version_status=(*out)->QueryInterface(IID_PPV_ARGS(&real));
    srmm_probe_log("swap-version4-query",static_cast<unsigned long>(version_status));
    if (FAILED(version_status)) return;
    auto* wrapper = new(std::nothrow) Swap(real.Get(),parent);
    if (!wrapper) return;
    T* replacement{};
    const auto status=wrapper->QueryInterface(__uuidof(T),reinterpret_cast<void**>(&replacement));
    if (SUCCEEDED(status)) { (*out)->Release(); *out=replacement; }
    wrapper->Release();
}
#ifdef SRMM_LOCAL_FACTORY_ROUTE
#include "factory_route.inc"
#endif
class Factory final : public IDXGIFactory7 {
    std::atomic<ULONG> refs_{1};
    ComPtr<IDXGIFactory7> real_;
public:
    explicit Factory(IDXGIFactory7* value) : real_(value) { ++factories; ++live_factories;srmm_probe_log("factory-created",factories.load()); }
    ~Factory() { --live_factories;srmm_probe_log("factory-released",live_factories.load()); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if (!out) return E_POINTER;
        *out=nullptr;
        if (!factory_iid(id)) { ++unknown_queries;srmm_probe_log("factory-unknown-interface",id.Data1,unknown_queries.load());return E_NOINTERFACE; }
        *out=static_cast<IDXGIFactory7*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* device,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** out) override {
        srmm_probe_log("create-swap-request");
        const auto status=real_->CreateSwapChain(device,desc,out);
        srmm_probe_log("create-swap-result",static_cast<unsigned long>(status));
        if (SUCCEEDED(status)) wrap_swap(out,this); return status;
    }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* device,HWND window,const DXGI_SWAP_CHAIN_DESC1* desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,IDXGIOutput* restrictOutput,IDXGISwapChain1** out) override {
        srmm_probe_log("create-swap-hwnd-request");
        const auto status=real_->CreateSwapChainForHwnd(device,window,desc,fullscreen,restrictOutput,out);
        if (SUCCEEDED(status)) wrap_swap(out,this); return status;
    }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown* device,IUnknown* window,const DXGI_SWAP_CHAIN_DESC1* desc,
        IDXGIOutput* restrictOutput,IDXGISwapChain1** out) override {
        srmm_probe_log("create-swap-core-window-request");
        const auto status=real_->CreateSwapChainForCoreWindow(device,window,desc,restrictOutput,out);
        if (SUCCEEDED(status)) wrap_swap(out,this); return status;
    }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown* device,const DXGI_SWAP_CHAIN_DESC1* desc,
        IDXGIOutput* restrictOutput,IDXGISwapChain1** out) override {
        srmm_probe_log("create-swap-composition-request");
        const auto status=real_->CreateSwapChainForComposition(device,desc,restrictOutput,out);
        if (SUCCEEDED(status)) wrap_swap(out,this); return status;
    }
#include "factory_forwarders.inc"
};
void wrap_factory(REFIID id,void** out) noexcept {
    if (!out || !*out || !wrapped_application() || !factory_iid(id)) return;
    auto* original=static_cast<IUnknown*>(*out);
    ComPtr<IDXGIFactory7> real;
    if (FAILED(original->QueryInterface(IID_PPV_ARGS(&real)))) return;
#ifdef SRMM_LOCAL_FACTORY_ROUTE
    install_factory_route(real.Get());
    return;
#endif
    retain_bridge();
    auto* wrapper=new(std::nothrow) Factory(real.Get());
    if (!wrapper) return;
    void* replacement{};
    if (SUCCEEDED(wrapper->QueryInterface(id,&replacement))) { original->Release(); *out=replacement; }
    wrapper->Release();
}
}
extern "C" HRESULT WINAPI BridgeCreateDXGIFactory(REFIID id,void** out) {
    const auto call=entry<HRESULT(WINAPI*)(REFIID,void**)>("CreateDXGIFactory");
    if (!call) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    const auto status=call(id,out); if (SUCCEEDED(status)) wrap_factory(id,out); return status;
}
extern "C" HRESULT WINAPI BridgeCreateDXGIFactory1(REFIID id,void** out) {
    const auto call=entry<HRESULT(WINAPI*)(REFIID,void**)>("CreateDXGIFactory1");
    if (!call) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    const auto status=call(id,out); if (SUCCEEDED(status)) wrap_factory(id,out); return status;
}
extern "C" HRESULT WINAPI BridgeCreateDXGIFactory2(UINT flags,REFIID id,void** out) {
    const auto call=entry<HRESULT(WINAPI*)(UINT,REFIID,void**)>("CreateDXGIFactory2");
    if (!call) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    const auto status=call(flags,id,out); if (SUCCEEDED(status)) wrap_factory(id,out); return status;
}
extern "C" __declspec(dllexport) BOOL WINAPI srmm_bridge_stats(SrmmBridgeStats* out) {
    if (!out || out->size!=sizeof(*out)) return FALSE;
    *out={sizeof(*out),1,factories.load(),swaps.load(),presents.load(),resizes.load(),live_factories.load(),live_swaps.load(),unknown_queries.load(),rendered.load(),render_failures.load()};
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI srmm_route_device(ID3D11Device* device) {
#ifdef SRMM_LOCAL_FACTORY_ROUTE
    if(!device||!wrapped_application())return FALSE;
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory7> factory;
    if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi)))||FAILED(dxgi->GetAdapter(&adapter))||
       FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))return FALSE;
    const auto result=install_factory_route(factory.Get());
    srmm_probe_log("device-factory-route",result);return result;
#else
    (void)device;return FALSE;
#endif
}
