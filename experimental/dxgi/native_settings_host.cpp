#define NOMINMAX
#include "native_settings.h"
#include <windows.h>
#include <d3d11.h>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <cmath>
using Microsoft::WRL::ComPtr;
namespace {
int checks=0;
void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
struct Window {
    HWND handle=nullptr;
    Window(){WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"SrmmSettingsOwnedFixture";RegisterClassW(&type);handle=CreateWindowW(type.lpszClassName,L"Owned settings fixture",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,type.hInstance,nullptr);}
    ~Window(){if(handle)DestroyWindow(handle);}
};
void test_model(){
    SrmmSettingsModel model;SrmmDisplaySettings v;
    auto step=[&](unsigned key,bool foreground=true,bool menu_visible=true){model.update({0,foreground,menu_visible},v,1920,1080);auto next=model.update({key,foreground,menu_visible},v,1920,1080);if(next)v=*next;return next;};
    step(srmm_settings_increase);check(v.zoom==1&&!model.open(),"closed settings ignore adjustment");
    step(srmm_settings_toggle);check(model.open(),"toggle opens panel");
    step(srmm_settings_increase,true,false);check(v.zoom>1&&model.editable(),"explicitly opened foreground panel edits without menu detection");
    step(srmm_settings_increase);check(v.zoom>1,"zoom increases");
    const auto held=v.zoom;auto again=model.update({srmm_settings_increase,true,true},v,1920,1080);check(!again&&v.zoom==held,"held key does not repeat");
    for(int i=0;i<40;++i)step(srmm_settings_increase);check(v.zoom==8,"zoom upper bound");
    for(int i=0;i<80;++i)step(srmm_settings_decrease);check(v.zoom==.25f,"zoom lower bound");
    step(srmm_settings_next);step(srmm_settings_increase);check(v.tracking==0,"tracking cycles motion to north");
    step(srmm_settings_decrease);check(v.tracking==2,"tracking reverse wraps");
    step(srmm_settings_next);step(srmm_settings_increase);check(v.square,"shape toggle");
    step(srmm_settings_next);for(int i=0;i<40;++i)step(srmm_settings_increase);check(v.size==800,"size upper bound");
    for(int i=0;i<40;++i)step(srmm_settings_decrease);check(v.size==96,"size lower bound");
    step(srmm_settings_next);step(srmm_settings_increase);check(v.left>1500&&v.top==32,"corner placement top right");
    step(srmm_settings_next);for(int i=0;i<30;++i)step(srmm_settings_increase);check(v.left+v.size<=1920,"horizontal stays in viewport");
    step(srmm_settings_next);for(int i=0;i<100;++i)step(srmm_settings_increase);check(v.top+v.size<=1080,"vertical stays in viewport");
    const auto before=v;step(srmm_settings_defaults,false);check(srmm_equal_display_settings(v,before),"background window cannot edit");
    step(srmm_settings_next);check(model.row()==7,"eighth row selects defaults");step(srmm_settings_increase);check(srmm_equal_display_settings(v,SrmmDisplaySettings{}),"defaults row restores all fields");
    model.update({0,true,true},v,1920,1080);auto small_view=model.update({srmm_settings_defaults,true,true},v,240,180);check(small_view&&small_view->size==180&&small_view->left==50&&small_view->top==0,"small viewport clamps complete minimap");
    step(srmm_settings_toggle);check(!model.open(),"toggle closes");
    auto invalid=v;invalid.zoom=std::numeric_limits<float>::quiet_NaN();check(!srmm_valid_display_settings(invalid),"nonfinite preference rejected");
}
void test_renderer(){
    Window window;check(window.handle!=nullptr,"hidden fixture window created");
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=800;desc.BufferDesc.Height=600;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window.handle;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> swap;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context)),"owned WARP swap created");
    ComPtr<ID3D11Texture2D> buffer;check(SUCCEEDED(swap->GetBuffer(0,IID_PPV_ARGS(&buffer))),"owned backbuffer");
    ComPtr<ID3D11RenderTargetView> rtv;check(SUCCEEDED(device->CreateRenderTargetView(buffer.Get(),nullptr,&rtv)),"owned target");
    const float color[]={.1f,.2f,.3f,1};context->ClearRenderTargetView(rtv.Get(),color);
    D3D11_VIEWPORT initial{7,11,123,145,0,1};context->RSSetViewports(1,&initial);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    SrmmNativeSettings panel;SrmmDisplaySettings values;
    panel.update({srmm_settings_toggle,true,true},values,800,600);panel.navigation_status(true,42);
    check(SUCCEEDED(panel.draw(swap.Get())),"native panel draws with GDI-generated text");
    UINT count=1;D3D11_VIEWPORT restored{};context->RSGetViewports(&count,&restored);
    check(count==1&&restored.TopLeftX==7&&restored.TopLeftY==11&&restored.Width==123&&restored.Height==145,"application viewport restored");
    D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);check(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"application topology restored");
    D3D11_TEXTURE2D_DESC texture{};buffer->GetDesc(&texture);texture.Usage=D3D11_USAGE_STAGING;texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;
    check(SUCCEEDED(device->CreateTexture2D(&texture,nullptr,&staging)),"staging readback created");context->CopyResource(staging.Get(),buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"panel readback");
    auto pixel=[&](unsigned x,unsigned y){return reinterpret_cast<const unsigned char*>(mapped.pData)+mapped.RowPitch*y+x*4;};
    const auto* center=pixel(400,300);check(center[0]<70&&center[1]<80&&center[2]<100,"panel background replaces center");
    const auto* outside=pixel(0,0);check(outside[0]>=24&&outside[0]<=27&&outside[1]>=50&&outside[1]<=52,"outside panel remains untouched");
    unsigned bright=0;for(unsigned y=50;y<550;++y)for(unsigned x=80;x<720;++x){const auto* p=pixel(x,y);if(p[0]>180&&p[1]>180&&p[2]>150)++bright;}
    check(bright>1000,"readable text rasterized into panel");context->Unmap(staging.Get(),0);
    buffer.Reset();rtv.Reset();staging.Reset();context->ClearState();
    check(SUCCEEDED(swap->ResizeBuffers(1,1024,768,DXGI_FORMAT_UNKNOWN,0)),"panel retains no backbuffer references across resize");
    panel.update({0,true,false},values,1024,768);check(SUCCEEDED(panel.draw(swap.Get())),"panel without menu signal survives resize");
    for(auto state:{SrmmSettingsSaveStatus::pending,SrmmSettingsSaveStatus::failed,SrmmSettingsSaveStatus::saved}){panel.preference_status(state);check(SUCCEEDED(panel.draw(swap.Get())),"save state feedback renders without changing settings");}
    panel.update({srmm_settings_toggle,true,false},values,1024,768);check(panel.draw(swap.Get())==S_FALSE,"closed panel does not render");
    panel.reset();
}
}
int main(){try{test_model();test_renderer();std::cout<<"PASS: "<<checks<<" native settings checks; owned hidden WARP fixture only.\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}


