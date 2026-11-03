#define NOMINMAX
#include "shared_renderer.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks=0;
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void ok(HRESULT hr,const char* label){check(SUCCEEDED(hr),label);}
struct Window {
 HWND value{};
 Window(){WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=DefWindowProcW;c.lpszClassName=L"SrmmAlignmentHost";check(RegisterClassW(&c)!=0,"Register window");value=CreateWindowW(c.lpszClassName,L"Owned alignment test",WS_OVERLAPPEDWINDOW,0,0,160,160,nullptr,nullptr,c.hInstance,nullptr);check(value!=nullptr,"Create hidden window");}
 ~Window(){if(value)DestroyWindow(value);UnregisterClassW(L"SrmmAlignmentHost",GetModuleHandleW(nullptr));}
};
}
int main(int argc,char**) {
 if(argc!=1)return 2;
 try {
  Window window;
  ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL feature=D3D_FEATURE_LEVEL_11_0;
  ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&feature,1,D3D11_SDK_VERSION,&device,nullptr,&context),"Create device");
  ComPtr<IDXGIFactory1> factory;ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create factory");
  DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=desc.BufferDesc.Height=128;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferCount=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window.value;desc.Windowed=TRUE;
  ComPtr<IDXGISwapChain> swap;ok(factory->CreateSwapChain(device.Get(),&desc,&swap),"Create swap");
  std::array<std::uint32_t,64*64> map{};
  constexpr std::uint32_t red=0xff0000ff,green=0xff00ff00,blue=0xffff0000,yellow=0xff00ffff;
  for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)map[y*64+x]=y<32?(x<32?red:green):(x<32?blue:yellow);
  SharedRenderer renderer;ok(renderer.initialize(device.Get(),map.data(),64,64),"Initialize original quadrant texture");
  ComPtr<ID3D11Texture2D> buffer,readback;ok(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)),"Get owned buffer");
  D3D11_TEXTURE2D_DESC texture{};buffer->GetDesc(&texture);texture.Usage=D3D11_USAGE_STAGING;texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;texture.MiscFlags=0;
  ok(device->CreateTexture2D(&texture,nullptr,&readback),"Create readback");
  std::array<std::uint32_t,128*128> pixels{};
  auto draw=[&](const SrmmMapFrame& frame) {
    ok(renderer.draw(swap.Get(),frame),"Draw alignment frame");context->CopyResource(readback.Get(),buffer.Get());
    D3D11_MAPPED_SUBRESOURCE data{};ok(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&data),"Map readback");
    for(unsigned y=0;y<128;++y)std::memcpy(pixels.data()+y*128,static_cast<const char*>(data.pData)+y*data.RowPitch,128*4);
    context->Unmap(readback.Get(),0);
  };
  auto pixel=[&](unsigned x,unsigned y){return pixels[y*128+x];};
  SrmmMapFrame frame;draw(frame);
  check(pixel(44,44)==red&&pixel(84,44)==green&&pixel(44,84)==blue&&pixel(84,84)==yellow,"Unflipped texture quadrants");
  frame.texture_v_flipped=true;draw(frame);
  check(pixel(44,44)==blue&&pixel(84,44)==yellow&&pixel(44,84)==red&&pixel(84,84)==green,"Flip only the texture vertical axis");
  check(pixel(64,54)==0xffffffff,"North marker remains up after texture flip");
  frame.heading=1.57079632679f;draw(frame);
  check(pixel(74,64)==0xffffffff&&pixel(64,54)!=0xffffffff,"East marker rotates clockwise independently of texture flip");
  frame.heading=0;frame.center_v=.25f;draw(frame);
  check(pixel(44,44)==blue&&pixel(44,84)==blue,"Pan center uses north-up UV before storage flip");
  auto invalid=frame;invalid.heading=std::nanf("");check(renderer.draw(swap.Get(),invalid)==E_INVALIDARG,"Reject nonfinite heading");
  invalid=frame;invalid.left=100;check(renderer.draw(swap.Get(),invalid)==E_INVALIDARG,"Reject viewport beyond backbuffer");
  buffer.Reset();readback.Reset();ok(swap->ResizeBuffers(1,160,160,DXGI_FORMAT_UNKNOWN,0),"No renderer backbuffer references survive draw");
  renderer.reset();std::cout<<"PASS: "<<checks<<" actual pixel orientation/pan/heading checks; original owned data only.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
