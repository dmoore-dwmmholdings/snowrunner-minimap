#define NOMINMAX
#include "shared_renderer.h"
#include "candidate_packet.h"
#include <array>
#include <cmath>
#include <iostream>
#include <fstream>
#include <vector>
#include "map_dds.h"
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
int main(int argc,char** argv) {
 if(argc!=1&&(argc!=3||std::strcmp(argv[1],"--local-map")))return 2;
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
  frame.texture_v_flipped=false;frame.heading=0;frame.map_rotation=1.57079632679f;draw(frame);
  check(pixel(44,44)==green&&pixel(84,44)==yellow&&pixel(44,84)==red&&pixel(84,84)==blue,"East-up map rotates while marker stays up");
  frame.map_rotation=0;frame.square=true;draw(frame);check(pixel(20,20)==red,"Square includes corner outside circle");
  frame.square=false;frame.texture_v_flipped=true;frame.heading=0;frame.center_v=.25f;draw(frame);
  check(pixel(44,44)==blue&&pixel(44,84)==blue,"Pan center uses north-up UV before storage flip");
  // Actual saved-waypoint glyph pixels, independent of copied game data.
  auto amber=[](std::uint32_t p){return (p&255)>240&&((p>>8)&255)>150&&((p>>8)&255)<200&&((p>>16)&255)<70;};
  frame=SrmmMapFrame{};frame.destination_count=1;frame.destinations[0]={24,0,0,false};draw(frame);
  check(amber(pixel(94,64)),"Saved waypoint has an amber outlined diamond");
  check(!amber(pixel(88,64)),"Saved diamond remains hollow");
  frame.destinations[0].kind=2;draw(frame);check(amber(pixel(88,64)),"Saved objective has a filled amber diamond");
  frame.destinations[0]={36,0,1.57079632679f,true};draw(frame);
  check(amber(pixel(102,64))&&!amber(pixel(100,58)),"East edge arrow points toward off-map destination");
  frame.destinations[0]={0,-36,0,true};draw(frame);
  check(amber(pixel(64,26)),"Rotated north edge arrow points upward");
  frame.destination_count=0;frame.center_u=.41f;frame.center_v=.37f;frame.uv_per_pixel=.004f;frame.uv_per_pixel_v=.008f;frame.map_rotation=1.57079632679f;draw(frame);
  check(pixel(88,32)==yellow,"Rectangular texture scale applies after rotation");
  auto invalidDestination=frame;invalidDestination.destination_count=17;check(renderer.draw(swap.Get(),invalidDestination)==E_INVALIDARG,"Destination bound validated before shader");
  invalidDestination=frame;invalidDestination.destination_count=1;invalidDestination.destinations[0].x=NAN;check(renderer.draw(swap.Get(),invalidDestination)==E_INVALIDARG,"Nonfinite destination rejected");
  // Original 4x4 BC3 block: red RGB, intentionally irrelevant alpha.
  std::array<std::uint8_t,144> dds{};
  auto word=[&](unsigned offset,std::uint32_t value){for(unsigned i=0;i<4;++i)dds[offset+i]=static_cast<std::uint8_t>(value>>(i*8));};
  std::memcpy(dds.data(),"DDS ",4);word(4,124);word(8,0x81007);word(12,4);word(16,4);word(20,16);word(28,1);word(76,32);word(80,4);word(84,0x35545844);word(108,0x1000);
  dds[137]=0xf8;dds[139]=0xf8;
  ok(renderer.initialize_dds(device.Get(),dds.data(),dds.size()),"Upload original bounded BC3 map");
  frame=SrmmMapFrame{};draw(frame);check(pixel(44,44)==red&&pixel(84,84)==red,"BC3 map RGB reaches actual backbuffer independently of alpha");
  // Complete diagnostic packet -> calibration -> native texture/render path, original data only.
  std::array<std::uint8_t,256> packet{};std::memcpy(packet.data(),"SRMMTEL1",8);
  auto p32=[&](unsigned offset,std::uint32_t value){for(unsigned i=0;i<4;++i)packet[offset+i]=static_cast<std::uint8_t>(value>>(i*8));};
  p32(8,1);p32(12,256);p32(16,1);p32(20,42);p32(24,1);p32(32,1000);p32(40,99);std::memcpy(packet.data()+48,"level_test_01",13);
  const double pose[]={100,0,200,1,0,0};std::memcpy(packet.data()+144,pose,sizeof(pose));
  SrmmCalibration calibration;calibration.affine={.001,0,.25,0,-.001,.75};calibration.validated=true;calibration.texture_v_flipped=true;
  SrmmCandidateGate gate(42,99,{},"level_test_01",calibration);
  auto candidate=gate.consume(packet.data(),packet.size(),1000);
  check(candidate.frame.has_value(),"Calibrated diagnostic packet produces frame");draw(*candidate.frame);
  check(pixel(74,64)==0xffffffff&&pixel(64,54)==red,"Packet east heading renders over BC3 texture");
  check(!gate.consume(packet.data(),packet.size(),1751).frame,"Expired diagnostic packet produces no render request");
  check(renderer.initialize_dds(device.Get(),dds.data(),143)==E_INVALIDARG,"Malformed map rejected before GPU upload");
  check(renderer.draw(swap.Get(),frame)==E_INVALIDARG,"Invalid replacement clears renderer rather than showing old map");
  ok(renderer.initialize(device.Get(),map.data(),64,64),"Restore original texture for invalid frame checks");
  auto invalid=frame;invalid.heading=std::nanf("");check(renderer.draw(swap.Get(),invalid)==E_INVALIDARG,"Reject nonfinite heading");
  invalid=frame;invalid.left=100;check(renderer.draw(swap.Get(),invalid)==E_INVALIDARG,"Reject viewport beyond backbuffer");
  if(argc==3) {
    std::ifstream file(argv[2],std::ios::binary|std::ios::ate);check(bool(file),"Open explicit private map copy");
    const auto length=file.tellg();check(length>=144&&length<=67108992,"Bound private map file size");
    std::vector<std::uint8_t> local(static_cast<std::size_t>(length));file.seekg(0);file.read(reinterpret_cast<char*>(local.data()),static_cast<std::streamsize>(local.size()));check(bool(file),"Read private map copy");
    const auto localView=srmm_parse_map_dds(local.data(),local.size());check(localView.has_value(),"Validate real local map DDS framing");
    ok(renderer.initialize_dds(device.Get(),local.data(),local.size()),"Upload real local map into owned WARP renderer");
    draw(SrmmMapFrame{});
    std::cout<<"Local map accepted: "<<localView->width<<"x"<<localView->height<<"; owned rendering only, world calibration unverified.\n";
  }
  buffer.Reset();readback.Reset();ok(swap->ResizeBuffers(1,160,160,DXGI_FORMAT_UNKNOWN,0),"No renderer backbuffer references survive draw");
  renderer.reset();std::cout<<"PASS: "<<checks<<(argc==3?" owned renderer checks with explicit local map; no game access.\n":" actual pixel orientation/pan/heading checks; original owned data only.\n");return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
