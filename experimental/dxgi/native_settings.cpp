#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "native_settings.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
constexpr unsigned panel_width=600,panel_height=530;
template<class T> T clamp(T v,T low,T high) noexcept {return (std::max)(low,(std::min)(high,v));}
void fit(SrmmDisplaySettings& v,unsigned width,unsigned height) noexcept {
    const float extent=static_cast<float>((std::min)(width,height));
    if(extent<96)return;
    v.size=clamp(v.size,96.f,(std::min)(800.f,extent));
    v.left=clamp(v.left,0.f,static_cast<float>(width)-v.size);
    v.top=clamp(v.top,0.f,static_cast<float>(height)-v.size);
}
unsigned corner(const SrmmDisplaySettings& v,unsigned width,unsigned height) noexcept {
    const bool right=v.left+v.size*.5f>width*.5f;
    const bool bottom=v.top+v.size*.5f>height*.5f;
    return bottom?(right?2:3):(right?1:0);
}
void move_corner(SrmmDisplaySettings& v,unsigned value,unsigned width,unsigned height) noexcept {
    const float right=(std::max)(0.f,static_cast<float>(width)-v.size-32.f);
    const float bottom=(std::max)(0.f,static_cast<float>(height)-v.size-32.f);
    v.left=value==1||value==2?right:32.f;v.top=value>=2?bottom:32.f;
    fit(v,width,height);
}
constexpr char shader[]=R"(
Texture2D panel:register(t0); SamplerState pointSampler:register(s0);
struct Vertex {float4 p:SV_POSITION;float2 uv:TEXCOORD0;};
Vertex vertex(uint id:SV_VertexID) {
 Vertex v;float2 p=id==0?float2(-1,-1):(id==1?float2(-1,3):float2(3,-1));
 v.p=float4(p,0,1);v.uv=float2((p.x+1)*.5,(1-p.y)*.5);return v;
}
float4 pixel(Vertex v):SV_TARGET {return float4(panel.Sample(pointSampler,v.uv).rgb,1);}
)";
struct GdiSurface {
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=nullptr;HFONT font=nullptr;
    HGDIOBJ old_bitmap=nullptr,old_font=nullptr;
    void* pixels=nullptr;
    ~GdiSurface(){
        if(dc&&old_font)SelectObject(dc,old_font);
        if(dc&&old_bitmap)SelectObject(dc,old_bitmap);
        if(font)DeleteObject(font);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);
    }
    bool create(){
        if(!dc)return false;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=panel_width;info.bmiHeader.biHeight=-static_cast<LONG>(panel_height);
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap||!pixels)return false;
        old_bitmap=SelectObject(dc,bitmap);
        font=CreateFontW(-19,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!font)return false;old_font=SelectObject(dc,font);return old_bitmap&&old_font;
    }
    void fill(int left,int top,int right,int bottom,COLORREF color){
        RECT rect{left,top,right,bottom};HBRUSH brush=CreateSolidBrush(color);
        if(brush){FillRect(dc,&rect,brush);DeleteObject(brush);}
    }
    bool text(int x,int y,const std::wstring& value,COLORREF color){
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
        return TextOutW(dc,x,y,value.data(),static_cast<int>(value.size()))!=FALSE;
    }
};
}
bool srmm_valid_display_settings(const SrmmDisplaySettings& v) noexcept {
    return std::isfinite(v.left)&&std::isfinite(v.top)&&std::isfinite(v.size)&&std::isfinite(v.zoom)&&
        v.left>=0&&v.top>=0&&v.size>=96&&v.size<=800&&v.zoom>=.25f&&v.zoom<=8&&v.tracking>=0&&v.tracking<=2;
}
bool srmm_equal_display_settings(const SrmmDisplaySettings& a,const SrmmDisplaySettings& b) noexcept {
    return a.left==b.left&&a.top==b.top&&a.size==b.size&&a.zoom==b.zoom&&a.tracking==b.tracking&&a.square==b.square;
}
SrmmSettingsInput srmm_poll_settings_input(HWND window,bool menu_visible) noexcept {
    SrmmSettingsInput input;input.foreground=window&&window==GetForegroundWindow();input.menu_visible=menu_visible;
    constexpr int keys[]={VK_F10,VK_F1,VK_F2,VK_F3,VK_F11};
    for(unsigned i=0;i<5;++i)if(GetAsyncKeyState(keys[i])&0x8000)input.down|=1u<<i;
    return input;
}
std::optional<SrmmDisplaySettings> SrmmSettingsModel::update(const SrmmSettingsInput& input,const SrmmDisplaySettings& current,unsigned width,unsigned height) noexcept {
    const unsigned edges=input.down&~previous_;previous_=input.down;
    editing_allowed_=input.foreground;menu_visible_=input.menu_visible;
    if(!srmm_valid_display_settings(current))return {};
    settings_=current;
    if(!input.foreground)return {};
    if(edges&srmm_settings_toggle)open_=!open_;
    if(!open_)return {};
    if(edges&srmm_settings_previous)row_=(row_+7)%8;
    if(edges&srmm_settings_next)row_=(row_+1)%8;
    if(!editing_allowed_||width<96||height<96)return {};
    auto changed=current;
    const int step=(edges&srmm_settings_increase?1:0)-(edges&srmm_settings_decrease?1:0);
    if(edges&srmm_settings_defaults)changed=SrmmDisplaySettings{};
    else if(step){
        switch(row_){
        case 0:changed.zoom=clamp(changed.zoom*std::pow(2.f,step*.25f),.25f,8.f);break;
        case 1:changed.tracking=(changed.tracking+step+3)%3;break;
        case 2:changed.square=!changed.square;break;
        case 3:changed.size+=step*32.f;break;
        case 4:move_corner(changed,(corner(changed,width,height)+step+4)%4,width,height);break;
        case 5:changed.left+=step*16.f;break;
        case 6:changed.top+=step*16.f;break;
        case 7:changed=SrmmDisplaySettings{};break;
        }
    }
    fit(changed,width,height);
    if(srmm_equal_display_settings(current,changed))return {};
    settings_=changed;return changed;
}
std::optional<SrmmDisplaySettings> SrmmNativeSettings::update(const SrmmSettingsInput& input,const SrmmDisplaySettings& current,unsigned width,unsigned height) noexcept {
    if(std::this_thread::get_id()!=owner_)return {};
    return model_.update(input,current,width,height);
}
void SrmmNativeSettings::reset() noexcept {
    if(deferred_){deferred_->ClearState();ComPtr<ID3D11CommandList> ignored;deferred_->FinishCommandList(FALSE,&ignored);}
    image_.Reset();sampler_.Reset();raster_.Reset();ps_.Reset();vs_.Reset();deferred_.Reset();device_.Reset();rendered_row_=-1;
}
HRESULT SrmmNativeSettings::initialize(ID3D11Device* device) noexcept {
    reset();if(!device)return E_INVALIDARG;device_=device;
    HRESULT hr=device->CreateDeferredContext(0,&deferred_);ComPtr<ID3DBlob> vertex,pixel,error;
    if(SUCCEEDED(hr))hr=D3DCompile(shader,sizeof(shader)-1,"srmm-original-native-settings",nullptr,nullptr,"vertex","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vertex,&error);
    if(SUCCEEDED(hr))hr=D3DCompile(shader,sizeof(shader)-1,"srmm-original-native-settings",nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&pixel,&error);
    if(SUCCEEDED(hr))hr=device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs_);
    if(SUCCEEDED(hr))hr=device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps_);
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    if(SUCCEEDED(hr))hr=device->CreateRasterizerState(&raster,&raster_);
    D3D11_SAMPLER_DESC sample{};sample.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sample.AddressU=sample.AddressV=sample.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sample.MaxLOD=D3D11_FLOAT32_MAX;
    if(SUCCEEDED(hr))hr=device->CreateSamplerState(&sample,&sampler_);
    if(FAILED(hr))reset();return hr;
}
HRESULT SrmmNativeSettings::refresh_image() noexcept {
    try {
        const auto& v=model_.values();std::array<std::wstring,15> lines;
        wchar_t number[128]{};
        lines[0]=L"SNOWRUNNER MINIMAP  /  DISPLAY SETTINGS";
        swprintf_s(number,L"Zoom                         %.2fx",v.zoom);lines[1]=number;
        const wchar_t* tracking[]={L"North up",L"Truck heading up",L"Direction of motion up"};
        lines[2]=L"Tracking                      "+std::wstring(tracking[v.tracking]);
        lines[3]=L"Shape                         "+std::wstring(v.square?L"Square":L"Round");
        swprintf_s(number,L"Size                            %.0f px",v.size);lines[4]=number;
        lines[5]=L"Corner                        F3 / F11 to cycle corners";
        swprintf_s(number,L"Horizontal offset        %.0f px",v.left);lines[6]=number;
        swprintf_s(number,L"Vertical offset            %.0f px",v.top);lines[7]=number;
        lines[8]=L"Restore defaults           F3 / F11 to reset";
        lines[9]=L"F1 / F2  Select row       F3 / F11  Adjust value";
        lines[10]=save_status_==SrmmSettingsSaveStatus::pending?L"F10  Close       Saving display settings...":
            save_status_==SrmmSettingsSaveStatus::failed?L"F10  Close       Not saved; retrying in background":
            save_status_==SrmmSettingsSaveStatus::saved?L"F10  Close       Display settings saved":L"F10  Close       Changes apply immediately";
        lines[11]=model_.menu_visible()?L"Game input is not blocked by this panel.":L"Pause before editing; game input is not blocked.";
        lines[12]=alignment_preview_?L"Alignment preview: map calibration is unverified.":L"Map calibration: measured.";
        lines[13]=L"Saved destinations (not live).";
        if(saved_age_seconds_){swprintf_s(number,L"Destination source: last save %llu seconds ago",static_cast<unsigned long long>(*saved_age_seconds_));lines[14]=number;}
        else lines[14]=L"Destination source: no saved destination data loaded.";
        if(image_&&rendered_row_==static_cast<int>(model_.row())&&rendered_lines_==lines)return S_OK;
        GdiSurface surface;if(!surface.create())return E_FAIL;
        surface.fill(0,0,panel_width,panel_height,RGB(20,27,36));
        surface.fill(0,0,panel_width,4,RGB(255,208,68));
        surface.fill(16,65+static_cast<int>(model_.row())*34,panel_width-16,97+static_cast<int>(model_.row())*34,RGB(49,65,80));
        bool ok=surface.text(24,22,lines[0],RGB(255,221,100));
        for(unsigned i=1;i<=8;++i)ok=surface.text(30,68+static_cast<int>(i-1)*34,lines[i],RGB(235,241,246))&&ok;
        ok=surface.text(24,354,lines[9],RGB(223,231,238))&&ok;
        ok=surface.text(24,379,lines[10],RGB(223,231,238))&&ok;
        ok=surface.text(24,409,lines[11],RGB(255,204,108))&&ok;
        ok=surface.text(24,434,lines[12],RGB(156,174,188))&&ok;
        ok=surface.text(24,464,lines[13],RGB(255,204,108))&&ok;
        ok=surface.text(24,489,lines[14],RGB(156,174,188))&&ok;
        if(!ok||!GdiFlush())return E_FAIL;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=panel_width;desc.Height=panel_height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{surface.pixels,panel_width*4,0};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> image;
        HRESULT hr=device_->CreateTexture2D(&desc,&initial,&texture);
        if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&image);
        if(SUCCEEDED(hr)){image_=image;rendered_lines_=std::move(lines);rendered_row_=static_cast<int>(model_.row());}
        return hr;
    }catch(...){return E_OUTOFMEMORY;}
}
HRESULT SrmmNativeSettings::draw(IDXGISwapChain* swap) noexcept {
    if(std::this_thread::get_id()!=owner_)return RPC_E_WRONG_THREAD;
    if(!model_.open())return S_FALSE;if(!swap)return E_INVALIDARG;
    ComPtr<ID3D11Device> device;HRESULT hr=swap->GetDevice(IID_PPV_ARGS(&device));
    if(FAILED(hr))return hr;if(device.Get()!=device_.Get()){hr=initialize(device.Get());if(FAILED(hr))return hr;}
    hr=refresh_image();if(FAILED(hr))return hr;
    ComPtr<ID3D11Texture2D> buffer;hr=swap->GetBuffer(0,IID_PPV_ARGS(&buffer));if(FAILED(hr))return hr;
    D3D11_TEXTURE2D_DESC desc{};buffer->GetDesc(&desc);
    if(desc.Width<160||desc.Height<120||desc.SampleDesc.Count!=1)return E_INVALIDARG;
    ComPtr<ID3D11RenderTargetView> target;hr=device_->CreateRenderTargetView(buffer.Get(),nullptr,&target);if(FAILED(hr))return hr;
    const float scale=(std::min)(1.25f,(std::min)((desc.Width-32.f)/panel_width,(desc.Height-32.f)/panel_height));
    const float width=panel_width*scale,height=panel_height*scale;
    D3D11_VIEWPORT viewport{(desc.Width-width)*.5f,(desc.Height-height)*.5f,width,height,0,1};
    auto* rtv=target.Get();deferred_->OMSetRenderTargets(1,&rtv,nullptr);deferred_->RSSetViewports(1,&viewport);deferred_->RSSetState(raster_.Get());
    deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);deferred_->VSSetShader(vs_.Get(),nullptr,0);deferred_->PSSetShader(ps_.Get(),nullptr,0);
    auto* image=image_.Get();auto* sampler=sampler_.Get();deferred_->PSSetShaderResources(0,1,&image);deferred_->PSSetSamplers(0,1,&sampler);deferred_->Draw(3,0);
    ComPtr<ID3D11CommandList> commands;hr=deferred_->FinishCommandList(FALSE,&commands);if(FAILED(hr)){reset();return hr;}
    ComPtr<ID3D11DeviceContext> immediate;device_->GetImmediateContext(&immediate);immediate->ExecuteCommandList(commands.Get(),TRUE);
    return device_->GetDeviceRemovedReason();
}

void SrmmNativeSettings::navigation_status(bool preview,std::optional<std::uint64_t> age) noexcept {
    if(std::this_thread::get_id()!=owner_)return;alignment_preview_=preview;saved_age_seconds_=age;
}

