// Original per-instance factory dispatch. No shared method table or code is written.
// Native COM identities remain unchanged; retained factories and dispatch state live until exit.
struct FactoryRoute {
    IDXGIFactory7* object{};
    void** original{};
    std::array<void*,32> dispatch{};
};
struct RouteRegistry {
    std::mutex lock;
    std::array<FactoryRoute*,64> entries{};
    unsigned count{};
};
RouteRegistry& routes() { static auto* value=new RouteRegistry; return *value; }
FactoryRoute* route_for(void* object) noexcept {
    auto& registry=routes(); std::lock_guard<std::mutex> hold(registry.lock);
    for(unsigned i=0;i<registry.count;++i)if(registry.entries[i]->object==object)return registry.entries[i];
    return nullptr;
}
template<class F> F route_call(FactoryRoute* state,unsigned slot) noexcept {
    // Consult the original SYSTEM DXGI table on each call. Do not cache a foreign trampoline.
    return reinterpret_cast<F>(*reinterpret_cast<void* const volatile*>(&state->original[slot]));
}
static HRESULT STDMETHODCALLTYPE route_QueryInterface(IDXGIFactory7* self,REFIID id,void** out) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_QueryInterface)>(state,0);
    return call(self,id,out);
}
static ULONG STDMETHODCALLTYPE route_AddRef(IDXGIFactory7* self) {
    auto* state=route_for(self);
    if(!state)return 0;
    const auto call=route_call<decltype(&route_AddRef)>(state,1);
    return call(self);
}
static ULONG STDMETHODCALLTYPE route_Release(IDXGIFactory7* self) {
    auto* state=route_for(self);
    if(!state)return 0;
    const auto call=route_call<decltype(&route_Release)>(state,2);
    return call(self);
}
static HRESULT STDMETHODCALLTYPE route_SetPrivateData(IDXGIFactory7* self,REFGUID Name, UINT DataSize, const void *pData) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_SetPrivateData)>(state,3);
    return call(self,Name,DataSize,pData);
}
static HRESULT STDMETHODCALLTYPE route_SetPrivateDataInterface(IDXGIFactory7* self,REFGUID Name, const IUnknown *pUnknown) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_SetPrivateDataInterface)>(state,4);
    return call(self,Name,pUnknown);
}
static HRESULT STDMETHODCALLTYPE route_GetPrivateData(IDXGIFactory7* self,REFGUID Name, UINT *pDataSize, void *pData) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_GetPrivateData)>(state,5);
    return call(self,Name,pDataSize,pData);
}
static HRESULT STDMETHODCALLTYPE route_GetParent(IDXGIFactory7* self,REFIID riid, void **ppParent) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_GetParent)>(state,6);
    return call(self,riid,ppParent);
}
static HRESULT STDMETHODCALLTYPE route_EnumAdapters(IDXGIFactory7* self,UINT Adapter, IDXGIAdapter **ppAdapter) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_EnumAdapters)>(state,7);
    return call(self,Adapter,ppAdapter);
}
static HRESULT STDMETHODCALLTYPE route_MakeWindowAssociation(IDXGIFactory7* self,HWND WindowHandle, UINT Flags) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_MakeWindowAssociation)>(state,8);
    return call(self,WindowHandle,Flags);
}
static HRESULT STDMETHODCALLTYPE route_GetWindowAssociation(IDXGIFactory7* self,HWND *pWindowHandle) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_GetWindowAssociation)>(state,9);
    return call(self,pWindowHandle);
}
static HRESULT STDMETHODCALLTYPE route_CreateSwapChain(IDXGIFactory7* self,IUnknown* device,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** out) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_CreateSwapChain)>(state,10);
    srmm_probe_log("route-CreateSwapChain");
    const auto status=call(self,device,desc,out);
    if(SUCCEEDED(status))wrap_swap(out,self);
    return status;
}
static HRESULT STDMETHODCALLTYPE route_CreateSoftwareAdapter(IDXGIFactory7* self,HMODULE Module, IDXGIAdapter **ppAdapter) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_CreateSoftwareAdapter)>(state,11);
    return call(self,Module,ppAdapter);
}
static HRESULT STDMETHODCALLTYPE route_EnumAdapters1(IDXGIFactory7* self,UINT Adapter, IDXGIAdapter1 **ppAdapter) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_EnumAdapters1)>(state,12);
    return call(self,Adapter,ppAdapter);
}
static BOOL STDMETHODCALLTYPE route_IsCurrent(IDXGIFactory7* self) {
    auto* state=route_for(self);
    if(!state)return 0;
    const auto call=route_call<decltype(&route_IsCurrent)>(state,13);
    return call(self);
}
static BOOL STDMETHODCALLTYPE route_IsWindowedStereoEnabled(IDXGIFactory7* self) {
    auto* state=route_for(self);
    if(!state)return 0;
    const auto call=route_call<decltype(&route_IsWindowedStereoEnabled)>(state,14);
    return call(self);
}
static HRESULT STDMETHODCALLTYPE route_CreateSwapChainForHwnd(IDXGIFactory7* self,IUnknown* device,HWND window,const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,IDXGIOutput* restrictOutput,IDXGISwapChain1** out) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_CreateSwapChainForHwnd)>(state,15);
    srmm_probe_log("route-CreateSwapChainForHwnd");
    const auto status=call(self,device,window,desc,fullscreen,restrictOutput,out);
    if(SUCCEEDED(status))wrap_swap(out,self);
    return status;
}
static HRESULT STDMETHODCALLTYPE route_CreateSwapChainForCoreWindow(IDXGIFactory7* self,IUnknown* device,IUnknown* window,const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* restrictOutput,IDXGISwapChain1** out) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_CreateSwapChainForCoreWindow)>(state,16);
    srmm_probe_log("route-CreateSwapChainForCoreWindow");
    const auto status=call(self,device,window,desc,restrictOutput,out);
    if(SUCCEEDED(status))wrap_swap(out,self);
    return status;
}
static HRESULT STDMETHODCALLTYPE route_GetSharedResourceAdapterLuid(IDXGIFactory7* self,HANDLE hResource, LUID *pLuid) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_GetSharedResourceAdapterLuid)>(state,17);
    return call(self,hResource,pLuid);
}
static HRESULT STDMETHODCALLTYPE route_RegisterStereoStatusWindow(IDXGIFactory7* self,HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_RegisterStereoStatusWindow)>(state,18);
    return call(self,WindowHandle,wMsg,pdwCookie);
}
static HRESULT STDMETHODCALLTYPE route_RegisterStereoStatusEvent(IDXGIFactory7* self,HANDLE hEvent, DWORD *pdwCookie) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_RegisterStereoStatusEvent)>(state,19);
    return call(self,hEvent,pdwCookie);
}
static void STDMETHODCALLTYPE route_UnregisterStereoStatus(IDXGIFactory7* self,DWORD dwCookie) {
    auto* state=route_for(self);
    if(!state)return;
    const auto call=route_call<decltype(&route_UnregisterStereoStatus)>(state,20);
    return call(self,dwCookie);
}
static HRESULT STDMETHODCALLTYPE route_RegisterOcclusionStatusWindow(IDXGIFactory7* self,HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_RegisterOcclusionStatusWindow)>(state,21);
    return call(self,WindowHandle,wMsg,pdwCookie);
}
static HRESULT STDMETHODCALLTYPE route_RegisterOcclusionStatusEvent(IDXGIFactory7* self,HANDLE hEvent, DWORD *pdwCookie) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_RegisterOcclusionStatusEvent)>(state,22);
    return call(self,hEvent,pdwCookie);
}
static void STDMETHODCALLTYPE route_UnregisterOcclusionStatus(IDXGIFactory7* self,DWORD dwCookie) {
    auto* state=route_for(self);
    if(!state)return;
    const auto call=route_call<decltype(&route_UnregisterOcclusionStatus)>(state,23);
    return call(self,dwCookie);
}
static HRESULT STDMETHODCALLTYPE route_CreateSwapChainForComposition(IDXGIFactory7* self,IUnknown* device,const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* restrictOutput,IDXGISwapChain1** out) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_CreateSwapChainForComposition)>(state,24);
    srmm_probe_log("route-CreateSwapChainForComposition");
    const auto status=call(self,device,desc,restrictOutput,out);
    if(SUCCEEDED(status))wrap_swap(out,self);
    return status;
}
static UINT STDMETHODCALLTYPE route_GetCreationFlags(IDXGIFactory7* self) {
    auto* state=route_for(self);
    if(!state)return 0;
    const auto call=route_call<decltype(&route_GetCreationFlags)>(state,25);
    return call(self);
}
static HRESULT STDMETHODCALLTYPE route_EnumAdapterByLuid(IDXGIFactory7* self,LUID AdapterLuid, REFIID riid, void **ppvAdapter) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_EnumAdapterByLuid)>(state,26);
    return call(self,AdapterLuid,riid,ppvAdapter);
}
static HRESULT STDMETHODCALLTYPE route_EnumWarpAdapter(IDXGIFactory7* self,REFIID riid, void **ppvAdapter) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_EnumWarpAdapter)>(state,27);
    return call(self,riid,ppvAdapter);
}
static HRESULT STDMETHODCALLTYPE route_CheckFeatureSupport(IDXGIFactory7* self,DXGI_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_CheckFeatureSupport)>(state,28);
    return call(self,Feature,pFeatureSupportData,FeatureSupportDataSize);
}
static HRESULT STDMETHODCALLTYPE route_EnumAdapterByGpuPreference(IDXGIFactory7* self,UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void **ppvAdapter) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_EnumAdapterByGpuPreference)>(state,29);
    return call(self,Adapter,GpuPreference,riid,ppvAdapter);
}
static HRESULT STDMETHODCALLTYPE route_RegisterAdaptersChangedEvent(IDXGIFactory7* self,HANDLE hEvent, DWORD *pdwCookie) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_RegisterAdaptersChangedEvent)>(state,30);
    return call(self,hEvent,pdwCookie);
}
static HRESULT STDMETHODCALLTYPE route_UnregisterAdaptersChangedEvent(IDXGIFactory7* self,DWORD dwCookie) {
    auto* state=route_for(self);
    if(!state)return E_UNEXPECTED;
    const auto call=route_call<decltype(&route_UnregisterAdaptersChangedEvent)>(state,31);
    return call(self,dwCookie);
}

bool install_factory_route(IDXGIFactory7* object) noexcept {
    if(!object)return false;
    try {
        auto& registry=routes(); std::lock_guard<std::mutex> hold(registry.lock);
        for(unsigned i=0;i<registry.count;++i)if(registry.entries[i]->object==object)return true;
        if(registry.count==registry.entries.size())return false;
        // Require the documented factory interface family to share one interface address.
        const IID ids[]={__uuidof(IUnknown),__uuidof(IDXGIObject),__uuidof(IDXGIFactory),__uuidof(IDXGIFactory1),
            __uuidof(IDXGIFactory2),__uuidof(IDXGIFactory3),__uuidof(IDXGIFactory4),__uuidof(IDXGIFactory5),__uuidof(IDXGIFactory6)};
        for(const auto& id:ids) {
            IUnknown* value{}; if(FAILED(object->QueryInterface(id,reinterpret_cast<void**>(&value))))return false;
            const bool same=static_cast<void*>(value)==static_cast<void*>(object); value->Release();if(!same)return false;
        }
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(object,&info,sizeof(info))||info.State!=MEM_COMMIT||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
           (info.Protect&0xff)!=PAGE_READWRITE)return false;
        auto** original=*reinterpret_cast<void***>(object);
        HMODULE tableOwner{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(original),&tableOwner)||tableOwner!=system_dxgi())return false;
        // Table storage must belong to the retained system module, not another overlay's allocation.
        auto* state=new(std::nothrow) FactoryRoute;
        if(!state)return false;
        state->object=object;state->original=original;
        state->dispatch[0]=reinterpret_cast<void*>(&route_QueryInterface);
        state->dispatch[1]=reinterpret_cast<void*>(&route_AddRef);
        state->dispatch[2]=reinterpret_cast<void*>(&route_Release);
        state->dispatch[3]=reinterpret_cast<void*>(&route_SetPrivateData);
        state->dispatch[4]=reinterpret_cast<void*>(&route_SetPrivateDataInterface);
        state->dispatch[5]=reinterpret_cast<void*>(&route_GetPrivateData);
        state->dispatch[6]=reinterpret_cast<void*>(&route_GetParent);
        state->dispatch[7]=reinterpret_cast<void*>(&route_EnumAdapters);
        state->dispatch[8]=reinterpret_cast<void*>(&route_MakeWindowAssociation);
        state->dispatch[9]=reinterpret_cast<void*>(&route_GetWindowAssociation);
        state->dispatch[10]=reinterpret_cast<void*>(&route_CreateSwapChain);
        state->dispatch[11]=reinterpret_cast<void*>(&route_CreateSoftwareAdapter);
        state->dispatch[12]=reinterpret_cast<void*>(&route_EnumAdapters1);
        state->dispatch[13]=reinterpret_cast<void*>(&route_IsCurrent);
        state->dispatch[14]=reinterpret_cast<void*>(&route_IsWindowedStereoEnabled);
        state->dispatch[15]=reinterpret_cast<void*>(&route_CreateSwapChainForHwnd);
        state->dispatch[16]=reinterpret_cast<void*>(&route_CreateSwapChainForCoreWindow);
        state->dispatch[17]=reinterpret_cast<void*>(&route_GetSharedResourceAdapterLuid);
        state->dispatch[18]=reinterpret_cast<void*>(&route_RegisterStereoStatusWindow);
        state->dispatch[19]=reinterpret_cast<void*>(&route_RegisterStereoStatusEvent);
        state->dispatch[20]=reinterpret_cast<void*>(&route_UnregisterStereoStatus);
        state->dispatch[21]=reinterpret_cast<void*>(&route_RegisterOcclusionStatusWindow);
        state->dispatch[22]=reinterpret_cast<void*>(&route_RegisterOcclusionStatusEvent);
        state->dispatch[23]=reinterpret_cast<void*>(&route_UnregisterOcclusionStatus);
        state->dispatch[24]=reinterpret_cast<void*>(&route_CreateSwapChainForComposition);
        state->dispatch[25]=reinterpret_cast<void*>(&route_GetCreationFlags);
        state->dispatch[26]=reinterpret_cast<void*>(&route_EnumAdapterByLuid);
        state->dispatch[27]=reinterpret_cast<void*>(&route_EnumWarpAdapter);
        state->dispatch[28]=reinterpret_cast<void*>(&route_CheckFeatureSupport);
        state->dispatch[29]=reinterpret_cast<void*>(&route_EnumAdapterByGpuPreference);
        state->dispatch[30]=reinterpret_cast<void*>(&route_RegisterAdaptersChangedEvent);
        state->dispatch[31]=reinterpret_cast<void*>(&route_UnregisterAdaptersChangedEvent);

        object->AddRef(); // Deliberate bounded process-lifetime ownership; no Release interception cleanup.
        registry.entries[registry.count++]=state;
        auto* previous=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(object),state->dispatch.data(),original);
        if(previous!=original) {
            --registry.count;object->Release();delete state;return false;
        }
        retain_bridge();++factories;++live_factories;srmm_probe_log("factory-instance-route",factories.load());
        return true;
    } catch(...) { return false; }
}
