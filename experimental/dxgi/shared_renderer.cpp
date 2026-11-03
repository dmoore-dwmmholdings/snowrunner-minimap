#define NOMINMAX
#include "shared_renderer.h"
#include "map_dds.h"
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace {
constexpr char shader[]=R"(
cbuffer Frame:register(b0) { float4 viewport; float4 mapView; float4 marker; };
Texture2D mapImage:register(t0); SamplerState mapSampler:register(s0);
float4 vertex(uint id:SV_VertexID):SV_POSITION {
 float2 p=id==0?float2(-1,-1):(id==1?float2(-1,3):float2(3,-1)); return float4(p,0,1);
}
float4 pixel(float4 p:SV_POSITION):SV_TARGET {
 float2 d=p.xy-viewport.xy-viewport.z*.5;
 float r=length(d); if(r>viewport.z*.5) discard;
 float2 uv=mapView.xy+d*mapView.z;
 if(mapView.w<0) uv.y=1-uv.y;
 float3 color=mapImage.Sample(mapSampler,uv).rgb;
 if(r>viewport.z*.5-2) color=marker.z>0?float3(1,.1,.8):float3(.75,.83,.85);
 float2 m=float2(marker.x*d.x+marker.y*d.y,-marker.y*d.x+marker.x*d.y);
 if(m.y>=-12 && m.y<=8 && abs(m.x)<.4*(m.y+12)) color=1;
 return float4(color,1);
}
)";
struct Constants { float viewport[4],map[4],marker[4]; };
bool valid(const SrmmMapFrame& f) {
 return std::isfinite(f.left)&&std::isfinite(f.top)&&std::isfinite(f.size)&&
  std::isfinite(f.center_u)&&std::isfinite(f.center_v)&&std::isfinite(f.uv_per_pixel)&&std::isfinite(f.heading)&&
  f.left>=0&&f.top>=0&&f.size>=32&&f.size<=2048&&f.uv_per_pixel>0&&f.uv_per_pixel<=1;
}
}
void SharedRenderer::reset() noexcept {
 // The deferred context is exclusively ours. Never clear the app's context.
 if(deferred_) { deferred_->ClearState(); ComPtr<ID3D11CommandList> discarded; deferred_->FinishCommandList(FALSE,&discarded); }
 map_.Reset();sampler_.Reset();raster_.Reset();constants_.Reset();ps_.Reset();vs_.Reset();deferred_.Reset();device_.Reset();
}
HRESULT SharedRenderer::initialize(ID3D11Device* device,const std::uint32_t* rgba,UINT width,UINT height) noexcept {
 return initialize_texture(device,rgba,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,width*4);
}
HRESULT SharedRenderer::initialize_dds(ID3D11Device* device,const void* bytes,std::size_t size) noexcept {
 const auto view=srmm_parse_map_dds(bytes,size);
 if(!view){reset();return E_INVALIDARG;}
 return initialize_texture(device,view->blocks,view->width,view->height,DXGI_FORMAT_BC3_UNORM,view->row_pitch);
}
HRESULT SharedRenderer::initialize_texture(ID3D11Device* device,const void* pixels,UINT width,UINT height,DXGI_FORMAT format,UINT pitch) noexcept {
 reset();
 if(!device||!pixels||!width||!height||width>8192||height>8192) return E_INVALIDARG;
 device_=device;
 HRESULT hr=device->CreateDeferredContext(0,&deferred_); if(FAILED(hr)) {reset();return hr;}
 ComPtr<ID3DBlob> vertex,pixel,errors;
 hr=D3DCompile(shader,sizeof(shader)-1,"srmm-original-shared-renderer",nullptr,nullptr,"vertex","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vertex,&errors);
 if(SUCCEEDED(hr)) hr=D3DCompile(shader,sizeof(shader)-1,"srmm-original-shared-renderer",nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&pixel,&errors);
 if(SUCCEEDED(hr)) hr=device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs_);
 if(SUCCEEDED(hr)) hr=device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps_);
 D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(Constants);buffer.Usage=D3D11_USAGE_DYNAMIC;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
 if(SUCCEEDED(hr)) hr=device->CreateBuffer(&buffer,nullptr,&constants_);
 D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
 if(SUCCEEDED(hr)) hr=device->CreateRasterizerState(&raster,&raster_);
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;sampler.BorderColor[0]=.08f;sampler.BorderColor[1]=.08f;sampler.BorderColor[2]=.08f;sampler.BorderColor[3]=1;sampler.MaxLOD=D3D11_FLOAT32_MAX;
 if(SUCCEEDED(hr)) hr=device->CreateSamplerState(&sampler,&sampler_);
 D3D11_TEXTURE2D_DESC texture{};texture.Width=width;texture.Height=height;texture.MipLevels=texture.ArraySize=texture.SampleDesc.Count=1;texture.Format=format;texture.Usage=D3D11_USAGE_IMMUTABLE;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA initial{pixels,pitch,0};ComPtr<ID3D11Texture2D> image;
 if(SUCCEEDED(hr)) hr=device->CreateTexture2D(&texture,&initial,&image);
 if(SUCCEEDED(hr)) hr=device->CreateShaderResourceView(image.Get(),nullptr,&map_);
 if(FAILED(hr)) reset(); return hr;
}
HRESULT SharedRenderer::draw(IDXGISwapChain* swap,const SrmmMapFrame& f) noexcept {
 if(!swap||!device_||!valid(f)) return E_INVALIDARG;
 ComPtr<ID3D11Device> device;
 HRESULT hr=swap->GetDevice(IID_PPV_ARGS(&device));if(FAILED(hr)||device.Get()!=device_.Get())return E_INVALIDARG;
 ComPtr<ID3D11Texture2D> buffer;hr=swap->GetBuffer(0,IID_PPV_ARGS(&buffer));if(FAILED(hr))return hr;
 D3D11_TEXTURE2D_DESC desc{};buffer->GetDesc(&desc);
 if(f.left+f.size>desc.Width||f.top+f.size>desc.Height||desc.SampleDesc.Count!=1)return E_INVALIDARG;
 ComPtr<ID3D11RenderTargetView> target;hr=device_->CreateRenderTargetView(buffer.Get(),nullptr,&target);if(FAILED(hr))return hr;
 const Constants values{{f.left,f.top,f.size,0},{f.center_u,f.center_v,f.uv_per_pixel,f.texture_v_flipped?-1.f:1.f},{std::cos(f.heading),std::sin(f.heading),f.alignment_preview?1.f:0.f,0}};
 D3D11_MAPPED_SUBRESOURCE mapped{};hr=deferred_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
 std::memcpy(mapped.pData,&values,sizeof(values));deferred_->Unmap(constants_.Get(),0);
 auto* rtv=target.Get();deferred_->OMSetRenderTargets(1,&rtv,nullptr);
 D3D11_VIEWPORT viewport{f.left,f.top,f.size,f.size,0,1};deferred_->RSSetViewports(1,&viewport);deferred_->RSSetState(raster_.Get());
 deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);deferred_->VSSetShader(vs_.Get(),nullptr,0);deferred_->PSSetShader(ps_.Get(),nullptr,0);
 auto* constants=constants_.Get();auto* map=map_.Get();auto* sampler=sampler_.Get();
 deferred_->PSSetConstantBuffers(0,1,&constants);deferred_->PSSetShaderResources(0,1,&map);deferred_->PSSetSamplers(0,1,&sampler);
 deferred_->Draw(3,0);
 ComPtr<ID3D11CommandList> commands;hr=deferred_->FinishCommandList(FALSE,&commands);
 if(FAILED(hr)){ reset();return hr; }
 ComPtr<ID3D11DeviceContext> immediate;device_->GetImmediateContext(&immediate);
 immediate->ExecuteCommandList(commands.Get(),TRUE);
 // All command-list, RTV and backbuffer references leave with this call.
 return device_->GetDeviceRemovedReason();
}
