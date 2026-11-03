#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "file_ingestor.h"
#include "candidate_packet.h"
#include "map_dds.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <new>
#include <utility>
namespace {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
enum class ReadResult { ready,missing,error,cancelled };
std::atomic<std::uint64_t> next_generation{1};
bool local_path(const std::wstring& path) {
    const std::filesystem::path p(path);
    return p.is_absolute()&&p.root_name().wstring().size()==2&&GetDriveTypeW(p.root_path().c_str())==DRIVE_FIXED;
}
ReadResult read_file(const std::wstring& path,std::size_t minimum,std::size_t maximum,HANDLE stop,DWORD timeout,std::vector<std::uint8_t>& bytes) {
    if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0)return ReadResult::cancelled;
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(file.value==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND?ReadResult::missing:ReadResult::error;
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if(GetFileType(file.value)!=FILE_TYPE_DISK||!GetFileInformationByHandleEx(file.value,FileAttributeTagInfo,&attributes,sizeof(attributes))||
       (attributes.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return ReadResult::error;
    LARGE_INTEGER length{};
    if(!GetFileSizeEx(file.value,&length)||length.QuadPart<static_cast<LONGLONG>(minimum)||length.QuadPart>static_cast<LONGLONG>(maximum))return ReadResult::error;
    bytes.resize(static_cast<std::size_t>(length.QuadPart));
    Handle complete(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!complete.value)return ReadResult::error;
    const auto deadline=GetTickCount64()+timeout;
    for(std::size_t offset=0;offset<bytes.size();) {
        if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0)return ReadResult::cancelled;
        if(GetTickCount64()>=deadline)return ReadResult::error;
        ResetEvent(complete.value);OVERLAPPED request{};request.hEvent=complete.value;request.Offset=static_cast<DWORD>(offset);
        const auto count=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-offset,1024*1024));
        const bool immediate=ReadFile(file.value,bytes.data()+offset,count,nullptr,&request)!=FALSE;
        if(!immediate&&GetLastError()!=ERROR_IO_PENDING)return ReadResult::error;
        if(!immediate) {
            HANDLE waits[]={stop,complete.value};const auto now=GetTickCount64();
            const auto outcome=WaitForMultipleObjects(2,waits,FALSE,now>=deadline?0:static_cast<DWORD>(deadline-now));
            if(outcome!=WAIT_OBJECT_0+1) {
                CancelIoEx(file.value,&request);
                // Cancellation is a request. Keep OVERLAPPED, buffer and handles alive until completion.
                // A slow driver may delay this worker; stop callers get Pending and never free its state.
                DWORD ignored{};GetOverlappedResult(file.value,&request,&ignored,TRUE);
                return outcome==WAIT_OBJECT_0?ReadResult::cancelled:ReadResult::error;
            }
        }
        DWORD received{};if(!GetOverlappedResult(file.value,&request,&received,FALSE)||received!=count)return ReadResult::error;
        offset+=received;
    }
    LARGE_INTEGER after{};if(!GetFileSizeEx(file.value,&after)||after.QuadPart!=length.QuadPart)return ReadResult::error;
    return ReadResult::ready;
}
}
bool srmm_sha256(const void* bytes,std::size_t size,std::array<std::uint8_t,32>& result) noexcept {
    if(!bytes||size>67108992)return false;
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,static_cast<PUCHAR>(const_cast<void*>(bytes)),static_cast<ULONG>(size),0)>=0;
    if(ok)ok=BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
struct SrmmFileIngestor::State {
    const SrmmIngestConfig config;
    const std::uint64_t generation=next_generation.fetch_add(1);
    Handle stop{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::mutex publication;
    bool stopping=false;
    std::shared_ptr<std::atomic<bool>> admission=std::make_shared<std::atomic<bool>>(true);
    std::uint64_t revision{};
    std::shared_ptr<const SrmmIngestSnapshot> current;
    // Preallocate stop snapshots: shutdown must not allocate memory or throw.
    std::shared_ptr<const SrmmIngestSnapshot> requested,stopped;
    explicit State(SrmmIngestConfig c):config(std::move(c)) {
        auto make=[&](SrmmIngestStatus status){auto p=std::make_shared<SrmmIngestSnapshot>();p->status=status;p->generation=generation;p->admission=admission;return p;};
        current=make(SrmmIngestStatus::starting);requested=make(SrmmIngestStatus::stop_requested);stopped=make(SrmmIngestStatus::stopped);
    }
    void publish(SrmmIngestStatus status,std::shared_ptr<const SrmmMapAsset> asset={},const std::vector<std::uint8_t>* packet=nullptr) {
        auto p=std::make_shared<SrmmIngestSnapshot>();p->status=status;p->generation=generation;p->asset=std::move(asset);p->admission=admission;
        if(packet&&packet->size()==256)std::copy(packet->begin(),packet->end(),p->packet.begin());
        std::lock_guard<std::mutex> hold(publication);
        if(stopping)return;p->revision=++revision;
        std::atomic_store_explicit(&current,std::shared_ptr<const SrmmIngestSnapshot>(std::move(p)),std::memory_order_release);
    }
};
SrmmFileIngestor::SrmmFileIngestor(SrmmIngestConfig config):state_(std::make_shared<State>(std::move(config))){}
SrmmFileIngestor::~SrmmFileIngestor(){request_stop();wait_for_stop(250);if(thread_)CloseHandle(thread_);}
bool SrmmFileIngestor::start() noexcept {
    if(attempted_)return false;attempted_=true;
    try {
        const auto& c=state_->config;
        if(!state_->stop.value||!local_path(c.map_path)||!local_path(c.packet_path)||c.map_path==c.packet_path||c.poll_ms<20||c.poll_ms>1000||c.read_timeout_ms<20||c.read_timeout_ms>5000||
           std::all_of(c.map_sha256.begin(),c.map_sha256.end(),[](auto b){return b==0;}))return false;
        {std::lock_guard<std::mutex> hold(state_->publication);if(state_->stopping)return false;}
        HMODULE own{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&own))return false;
        auto* context=new std::shared_ptr<State>(state_);
        thread_=CreateThread(nullptr,0,&worker,context,0,nullptr);
        if(!thread_){delete context;return false;}return true;
    }catch(...){return false;}
}
void SrmmFileIngestor::request_stop() noexcept {
    std::lock_guard<std::mutex> hold(state_->publication);if(state_->stopping)return;state_->stopping=true;
    state_->admission->store(false,std::memory_order_release);
    std::atomic_store_explicit(&state_->current,state_->requested,std::memory_order_release);
    if(state_->stop.value)SetEvent(state_->stop.value);
}
bool SrmmFileIngestor::wait_for_stop(std::uint32_t timeout) noexcept {
    if(thread_&&WaitForSingleObject(thread_,timeout)!=WAIT_OBJECT_0)return false;
    std::lock_guard<std::mutex> hold(state_->publication);
    if(state_->stopping)std::atomic_store_explicit(&state_->current,state_->stopped,std::memory_order_release);
    return true;
}
std::shared_ptr<const SrmmIngestSnapshot> SrmmFileIngestor::snapshot() const noexcept {return std::atomic_load_explicit(&state_->current,std::memory_order_acquire);}
std::uint64_t SrmmFileIngestor::generation() const noexcept{return state_->generation;}
unsigned long __stdcall SrmmFileIngestor::worker(void* opaque) noexcept {
    auto* owner=static_cast<std::shared_ptr<State>*>(opaque);auto state=*owner;delete owner;
    try {
        const auto& c=state->config;auto asset=std::make_shared<SrmmMapAsset>();
        const auto mapResult=read_file(c.map_path,144,67108992,state->stop.value,c.read_timeout_ms,asset->dds);
        if(mapResult!=ReadResult::ready||!srmm_parse_map_dds(asset->dds.data(),asset->dds.size())||
           !srmm_sha256(asset->dds.data(),asset->dds.size(),asset->sha256)||asset->sha256!=c.map_sha256) {
            state->publish(SrmmIngestStatus::asset_error);return 0;
        }
        while(WaitForSingleObject(state->stop.value,0)!=WAIT_OBJECT_0) {
            std::vector<std::uint8_t> packet;
            const auto result=read_file(c.packet_path,256,256,state->stop.value,c.read_timeout_ms,packet);
            if(result==ReadResult::cancelled)break;
            if(result==ReadResult::missing)state->publish(SrmmIngestStatus::missing_packet);
            else if(result!=ReadResult::ready)state->publish(SrmmIngestStatus::io_error);
            else if(!srmm_parse_candidate(packet.data(),packet.size()))state->publish(SrmmIngestStatus::invalid_packet);
            else state->publish(SrmmIngestStatus::ready,asset,&packet);
            if(WaitForSingleObject(state->stop.value,c.poll_ms)==WAIT_OBJECT_0)break;
        }
    }catch(...){try{state->publish(SrmmIngestStatus::io_error);}catch(...){}}
    return 0;
}
