#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "diagnostic_view.h"
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks{};
void check(bool value,const char* what){++checks;if(!value)throw std::runtime_error(what);}
void ok(HRESULT hr,const char* what){check(SUCCEEDED(hr),what);}
void p32(std::uint8_t* p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(i*8));}
void p64(std::uint8_t* p,std::uint64_t v){for(unsigned i=0;i<8;++i)p[i]=static_cast<std::uint8_t>(v>>(i*8));}
auto map_fixture(){std::array<std::uint8_t,144> d{};std::memcpy(d.data(),"DDS ",4);p32(d.data()+4,124);p32(d.data()+8,0x81007);p32(d.data()+12,4);p32(d.data()+16,4);p32(d.data()+20,16);p32(d.data()+28,1);p32(d.data()+76,32);p32(d.data()+80,4);p32(d.data()+84,0x35545844);p32(d.data()+108,0x1000);d[137]=d[139]=0xf8;return d;}
auto packet(std::uint64_t sequence){std::array<std::uint8_t,256> p{};std::memcpy(p.data(),"SRMMTEL1",8);p32(p.data()+8,1);p32(p.data()+12,256);p32(p.data()+16,1);p32(p.data()+20,42);p64(p.data()+24,sequence);p64(p.data()+32,GetTickCount64());p64(p.data()+40,99);std::memcpy(p.data()+48,"level_test_01",13);const double pose[]={100,0,200,1,0,0};std::memcpy(p.data()+144,pose,sizeof(pose));return p;}
struct Files {
    std::filesystem::path root,map,packet_path,next;
    Files(){wchar_t temporary[32768]{};check(GetTempPathW(32768,temporary)>0,"Find own fixture temp directory");
        root=std::filesystem::path(temporary)/(L"srmm-handoff-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(CreateDirectoryW(root.c_str(),nullptr)!=FALSE,"Create exclusive owned fixture directory");map=root/L"map.dds";packet_path=root/L"candidate.bin";next=root/L"next.tmp";}
    void replace(const std::filesystem::path& destination,const void* bytes,std::size_t size){
        HANDLE file=CreateFileW(next.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);check(file!=INVALID_HANDLE_VALUE,"Create new atomic fixture payload");
        DWORD written{};const bool saved=WriteFile(file,bytes,static_cast<DWORD>(size),&written,nullptr)&&written==size;CloseHandle(file);check(saved,"Write complete original fixture");
        check(MoveFileExW(next.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Atomically replace fixture while reader is active");
    }
    ~Files(){DeleteFileW(map.c_str());DeleteFileW(packet_path.c_str());DeleteFileW(next.c_str());RemoveDirectoryW(root.c_str());}
};
template<class Predicate> auto await_snapshot(SrmmFileIngestor& source,Predicate predicate){
    const auto deadline=GetTickCount64()+3000;
    do {auto p=source.snapshot();if(predicate(*p))return p;Sleep(5);}while(GetTickCount64()<deadline);
    throw std::runtime_error("Bounded wait for owned worker snapshot timed out");
}
auto status(SrmmFileIngestor& source,SrmmIngestStatus expected){return await_snapshot(source,[&](const auto& p){return p.status==expected;});}
auto ready(SrmmFileIngestor& source,unsigned sequence){return await_snapshot(source,[&](const auto& p){return p.status==SrmmIngestStatus::ready&&p.packet[24]==sequence;});}
struct Window {
    HWND value{};
    Window(){WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=DefWindowProcW;c.lpszClassName=L"SrmmIngestionOwnedHost";check(RegisterClassW(&c)!=0,"Register hidden fixture window");value=CreateWindowW(c.lpszClassName,L"Owned file handoff",WS_OVERLAPPEDWINDOW,0,0,160,160,nullptr,nullptr,c.hInstance,nullptr);check(value!=nullptr,"Create hidden fixture window");}
    ~Window(){if(value)DestroyWindow(value);UnregisterClassW(L"SrmmIngestionOwnedHost",GetModuleHandleW(nullptr));}
};
}
int main(int argc,char**){if(argc!=1)return 2;try {
    Files files;auto dds=map_fixture();files.replace(files.map,dds.data(),dds.size());
    SrmmIngestConfig config;config.map_path=files.map;config.packet_path=files.packet_path;config.poll_ms=20;
    check(srmm_sha256(dds.data(),dds.size(),config.map_sha256),"Hash original map fixture");
    SrmmFileIngestor source(config);check(source.snapshot()->status==SrmmIngestStatus::starting,"Initial state has no packet");
    check(source.start(),"Start explicit background ingestion");check(!source.start(),"Refuse duplicate worker start");
    auto missing=status(source,SrmmIngestStatus::missing_packet);check(!missing->asset,"Missing packet cannot retain visible data");
    auto p=packet(1);files.replace(files.packet_path,p.data(),p.size());auto first=ready(source,1);
    check(!source.wait_for_stop(0),"Active worker reports Pending without waiting or discarding state");
    check(first->asset&&first->asset->dds.size()==144&&first->asset->sha256==config.map_sha256,"Publish immutable hash-bound map and complete packet together");
    auto held=first;auto bad=p;bad[192]=1;files.replace(files.packet_path,bad.data(),bad.size());
    auto malformed=status(source,SrmmIngestStatus::invalid_packet);check(!malformed->asset,"Malformed packet publishes unavailable snapshot");
    std::array<std::uint8_t,257> oversized{};std::copy(p.begin(),p.end(),oversized.begin());
    files.replace(files.packet_path,oversized.data(),oversized.size());check(!status(source,SrmmIngestStatus::io_error)->asset,"Oversized file never publishes partial telemetry");
    check(held->packet==p&&held->asset->dds.size()==144,"Earlier snapshots remain immutable and memory-safe");
    check(DeleteFileW(files.packet_path.c_str())!=FALSE,"Remove own fixture packet");status(source,SrmmIngestStatus::missing_packet);
    p=packet(2);files.replace(files.packet_path,p.data(),p.size());auto current=ready(source,2);

    Window window;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL feature=D3D_FEATURE_LEVEL_11_0;
    ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&feature,1,D3D11_SDK_VERSION,&device,nullptr,&context),"Create owned WARP device");
    ComPtr<IDXGIFactory1> factory;ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create owned factory");
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=desc.BufferDesc.Height=128;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferCount=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window.value;desc.Windowed=TRUE;
    ComPtr<IDXGISwapChain> swap;ok(factory->CreateSwapChain(device.Get(),&desc,&swap),"Create owned swap chain");
    SrmmCalibration calibration;calibration.affine={.001,0,.25,0,-.001,.75};calibration.validated=true;calibration.texture_v_flipped=true;
    SrmmDiagnosticView view(source.generation(),config.map_sha256,42,99,{},"level_test_01",calibration);
    check(view.draw(swap.Get(),current,GetTickCount64(),false).status==SrmmViewStatus::hidden,"HUD opt-out prevents rendering");
    check(view.draw(swap.Get(),current,GetTickCount64(),true).status==SrmmViewStatus::ready_diagnostic,"Worker snapshot reaches owned render thread");
    ComPtr<ID3D11Texture2D> buffer,readback;ok(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)),"Get fixture pixels");D3D11_TEXTURE2D_DESC texture{};buffer->GetDesc(&texture);texture.Usage=D3D11_USAGE_STAGING;texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;texture.MiscFlags=0;
    ok(device->CreateTexture2D(&texture,nullptr,&readback),"Create fixture pixel readback");context->CopyResource(readback.Get(),buffer.Get());D3D11_MAPPED_SUBRESOURCE mapped{};ok(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"Read handoff output");
    auto pixel=[&](unsigned x,unsigned y){return reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)[x];};
    const bool correct=pixel(74,64)==0xffffffff&&pixel(44,44)==0xff0000ff;context->Unmap(readback.Get(),0);check(correct,"Atomic packet and BC3 asset produce east marker and map pixels");
    buffer.Reset();readback.Reset();ok(swap->ResizeBuffers(1,160,160,DXGI_FORMAT_UNKNOWN,0),"No handoff backbuffer references survive draw");
    SrmmViewResult otherThread{};std::thread other([&]{otherThread=view.draw(nullptr,current,GetTickCount64(),true);});other.join();check(otherThread.status==SrmmViewStatus::wrong_thread,"Wrong-thread drawing refuses before graphics calls");
    check(view.draw(swap.Get(),malformed,GetTickCount64(),true).status==SrmmViewStatus::unavailable,"Input error clears render resources");
    p=packet(3);files.replace(files.packet_path,p.data(),p.size());current=ready(source,3);
    check(view.draw(swap.Get(),current,GetTickCount64(),true).status==SrmmViewStatus::ready_diagnostic,"Fresh packet recovers after input error and resize");
    auto changed=std::make_shared<SrmmIngestSnapshot>(*current);++changed->generation;
    check(view.draw(swap.Get(),changed,GetTickCount64(),true).status==SrmmViewStatus::wrong_generation,"Reject snapshot from another generation");
    p=packet(4);p[54]='b';files.replace(files.packet_path,p.data(),p.size());auto wrongMap=ready(source,4);
    check(view.draw(swap.Get(),wrongMap,GetTickCount64(),true).packet_status==SrmmPacketStatus::wrong_context,"Changed map cannot reuse old texture/calibration");
    p=packet(5);files.replace(files.packet_path,p.data(),p.size());current=ready(source,5);
    check(view.draw(swap.Get(),current,GetTickCount64(),true).status==SrmmViewStatus::ready_diagnostic,"Matching context can recover on a new sample");
    // A device replacement releases only this view's resources on the owning render thread.
    swap.Reset();ComPtr<ID3D11Device> replacement;ComPtr<ID3D11DeviceContext> replacementContext;
    ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&feature,1,D3D11_SDK_VERSION,&replacement,nullptr,&replacementContext),"Create replacement owned device");
    ok(factory->CreateSwapChain(replacement.Get(),&desc,&swap),"Create replacement swap after old release");
    p=packet(6);files.replace(files.packet_path,p.data(),p.size());current=ready(source,6);
    check(view.draw(swap.Get(),current,GetTickCount64(),true).status==SrmmViewStatus::ready_diagnostic,"Recreate graphics resources for changed device identity");
    check(view.draw(swap.Get(),current,GetTickCount64()+1000,true).packet_status==SrmmPacketStatus::stale,"Freshness checked at render time even without new worker publication");
    source.request_stop();check(source.snapshot()->status==SrmmIngestStatus::stop_requested,"Stop publishes immediately");
    check(view.draw(swap.Get(),current,GetTickCount64()+1000,true).status==SrmmViewStatus::unavailable,"Stop revokes even a previously retained ready snapshot");
    for(unsigned i=0;i<100;++i)check(source.snapshot()->status!=SrmmIngestStatus::ready,"No ready publication can race past stop");
    check(source.wait_for_stop(2000),"Worker completes cooperative shutdown");check(source.snapshot()->status==SrmmIngestStatus::stopped,"Confirmed stop state");
    source.request_stop();check(source.snapshot()->status==SrmmIngestStatus::stopped,"Repeated stop is idempotent");check(!source.start(),"Stopped worker cannot silently restart");
    auto wrong=config;wrong.map_sha256[0]^=1;SrmmFileIngestor mismatch(wrong);check(mismatch.start(),"Start digest-mismatch test");check(!status(mismatch,SrmmIngestStatus::asset_error)->asset,"Wrong map digest never enters handoff");check(mismatch.wait_for_stop(2000),"Asset error terminates worker");
    auto broken=dds;broken[0]='x';files.replace(files.map,broken.data(),broken.size());auto corrupt=config;
    check(srmm_sha256(broken.data(),broken.size(),corrupt.map_sha256),"Hash malformed original texture fixture");
    SrmmFileIngestor malformedMap(corrupt);check(malformedMap.start(),"Start malformed map test");check(!status(malformedMap,SrmmIngestStatus::asset_error)->asset,"Correct hash cannot override malformed DDS rejection");check(malformedMap.wait_for_stop(2000),"Malformed map worker exits");
    auto invalid=config;invalid.map_path=L"relative.dds";SrmmFileIngestor relative(invalid);check(!relative.start(),"Reject relative file path");
    SrmmFileIngestor cancelled(config);cancelled.request_stop();check(!cancelled.start(),"Pre-start cancellation remains stopped");
    std::cout<<"PASS: "<<checks<<" background ingestion, immutable handoff, pixels and lifecycle checks; owned local fixtures only.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
