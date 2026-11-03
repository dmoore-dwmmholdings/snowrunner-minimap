#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "diagnostic_api.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Owned-process design fixture ONLY. These original C++ callbacks model two
// hook layers. No machine-code patch, remote process access, Steam DLL or
// system-function modification is involved. Not a production trust policy.
namespace {
using namespace std::chrono_literals;
enum class Result { ready, refused, load_failed, abi_failed, pending, cancelled, closed, cleanup_pending };
enum class State { idle, loading, ready, cleanup_pending };
struct Counts {
    std::atomic<unsigned> outer_load{}, inner_load{}, windows_load{}, outer_unload{}, inner_unload{}, windows_unload{};
    std::atomic<unsigned> sessions{};
    std::atomic<unsigned> updates{};
    std::atomic<bool> unload_with_session{};
};
class FixtureChain {
    std::mutex config_mutex_, gate_mutex_;
    std::condition_variable gate_cv_;
    bool leased_ = false, known_ = true;
    std::uint64_t generation_ = 1;
    bool gate_enabled_ = false, entered_ = false, released_ = false;
    bool call_gate_enabled_ = false;
    std::filesystem::path dll_;
    HMODULE inner_load(bool missing) {
        ++counts.inner_load;
        {
            std::unique_lock<std::mutex> lock(gate_mutex_);
            if (gate_enabled_) {
                entered_ = true;
                gate_cv_.notify_all();
                if (!gate_cv_.wait_for(lock, 5s, [&] { return released_; })) return nullptr;
            }
        }
        const auto path = missing ? dll_.parent_path()/L"__srmm_fixture_missing__.dll" : dll_;
        if (missing && std::filesystem::exists(path))
            throw std::runtime_error("Missing-file fixture path unexpectedly exists; refusing to execute it");
        ++counts.windows_load;
        return LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    bool inner_unload(HMODULE module) {
        ++counts.inner_unload;
        if (counts.sessions != 0) { counts.unload_with_session = true; return false; }
        if (fail_unload_once.exchange(false)) return false; // Known failure before FreeLibrary; test-only.
        ++counts.windows_unload;
        return FreeLibrary(module) != FALSE;
    }
public:
    Counts counts;
    std::atomic<bool> fail_unload_once{};
    explicit FixtureChain(std::filesystem::path dll) : dll_(std::move(dll)) {}
    bool configure(bool known) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        if (leased_) return false;
        known_ = known;
        ++generation_;
        return true;
    }
    std::uint64_t generation() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return generation_;
    }
    bool acquire(std::uint64_t expected) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        if (leased_ || !known_ || expected != generation_) return false;
        leased_ = true;
        return true;
    }
    void release() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        leased_ = false;
    }
    // The client can only enter through the outer callback. Its inner callback
    // and Windows loader are private, so it cannot select a downstream target.
    HMODULE load(bool missing) { ++counts.outer_load; return inner_load(missing); }
    bool unload(HMODULE module) { ++counts.outer_unload; return inner_unload(module); }
    void hold_next_load() {
        std::lock_guard<std::mutex> lock(gate_mutex_);
        gate_enabled_ = true; entered_ = released_ = false;
    }
    void hold_next_call() {
        std::lock_guard<std::mutex> lock(gate_mutex_);
        call_gate_enabled_ = true; entered_ = released_ = false;
    }
    bool before_api_call() {
        std::unique_lock<std::mutex> lock(gate_mutex_);
        if (!call_gate_enabled_) return true;
        entered_ = true;
        gate_cv_.notify_all();
        return gate_cv_.wait_for(lock, 5s, [&] { return released_; });
    }
    bool await_entry() {
        std::unique_lock<std::mutex> lock(gate_mutex_);
        return gate_cv_.wait_for(lock, 2s, [&] { return entered_; });
    }
    void release_gate() {
        std::lock_guard<std::mutex> lock(gate_mutex_);
        released_ = true;
        gate_cv_.notify_all();
    }
};
class FixtureClient {
    FixtureChain& chain_;
    std::mutex operation_;
    std::atomic<std::uint64_t> stop_epoch_{};
    std::atomic<State> state_{State::idle};
    HMODULE module_ = nullptr; // Exactly one LoadLibraryEx-owned reference.
    SrmmApi api_{};
    SrmmSession session_ = nullptr;
    Result close_locked() {
        if (session_) { api_.destroy(session_); session_ = nullptr; --chain_.counts.sessions; }
        api_ = {}; // No stale entry-point pointers survive attempted unload.
        if (module_ && !chain_.unload(module_)) {
            state_ = State::cleanup_pending;
            return Result::cleanup_pending; // Keep reference and lease tracked; no automatic retry.
        }
        module_ = nullptr;
        chain_.release();
        state_ = State::idle;
        return Result::closed;
    }
public:
    explicit FixtureClient(FixtureChain& chain) : chain_(chain) {}
    ~FixtureClient() {
        // The fixture owner joins every operation before destruction. Never
        // silently retry an uncertain/failed unload in a destructor.
        if (state_ == State::ready) close();
        if (state_ != State::idle) std::cerr << "Fixture ended with unresolved cleanup.\n";
    }
    State state() const { return state_; }
    Result open(std::uint64_t generation, bool missing = false, bool bad_abi = false) {
        const auto started_epoch = stop_epoch_.load();
        std::unique_lock<std::mutex> lock(operation_, std::try_to_lock);
        if (!lock || state_ != State::idle) return Result::refused;
        if (!chain_.acquire(generation)) return Result::refused;
        state_ = State::loading;
        try {
            module_ = chain_.load(missing);
            if (!module_) { close_locked(); return Result::load_failed; }
            if (stop_epoch_ != started_epoch) return close_locked() == Result::closed ? Result::cancelled : Result::cleanup_pending;
            auto get_api = reinterpret_cast<SrmmGetApi>(GetProcAddress(module_, "srmm_get_api"));
            if (!get_api || get_api(bad_abi ? SRMM_DIAGNOSTIC_ABI+1 : SRMM_DIAGNOSTIC_ABI, sizeof(api_), &api_) != SRMM_OK ||
                api_.size != sizeof(api_) || api_.version != SRMM_DIAGNOSTIC_ABI || api_.capabilities != SRMM_CAP_SYNTHETIC_ONLY ||
                !api_.create || !api_.update || !api_.destroy || api_.create(&session_) != SRMM_OK) {
                return close_locked() == Result::closed ? Result::abi_failed : Result::cleanup_pending;
            }
            ++chain_.counts.sessions;
            if (stop_epoch_ != started_epoch) return close_locked() == Result::closed ? Result::cancelled : Result::cleanup_pending;
            state_ = State::ready;
            return Result::ready;
        } catch (...) {
            close_locked();
            throw;
        }
    }
    bool exercise() {
        const auto started_epoch = stop_epoch_.load();
        std::unique_lock<std::mutex> lock(operation_, std::try_to_lock);
        if (!lock || state_ != State::ready || !session_) return false;
        if (!chain_.before_api_call()) return false;
        const SrmmSample sample{sizeof(SrmmSample),SRMM_SYNTHETIC,1,SRMM_ACTIVE,1,100,100,10,20,0};
        SrmmFrame frame{};
        frame.size = sizeof(frame);
        ++chain_.counts.updates;
        const bool success = api_.update(session_,&sample,&frame) == SRMM_OK && frame.state == SRMM_READY && frame.has_geometry == 1;
        return stop_epoch_ == started_epoch && success; // Suppress a result cancelled during the borrowed call.
    }
    Result close() {
        ++stop_epoch_;
        std::unique_lock<std::mutex> lock(operation_, std::try_to_lock);
        if (!lock) return Result::pending; // Pending work owns its DLL/request lifetime.
        if (state_ == State::idle) return Result::closed;
        return close_locked();
    }
};
unsigned checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}
bool absent() { return GetModuleHandleW(L"srmm_diagnostic.dll") == nullptr; } // Observation; never passed to FreeLibrary.
std::filesystem::path adjacent_dll() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("Cannot resolve fixture path");
    return std::filesystem::path(std::wstring(path.data(),length)).parent_path()/L"srmm_diagnostic.dll";
}
void synchronous_cases(const std::filesystem::path& dll) {
    FixtureChain chain(dll);
    FixtureClient client(chain);
    check(absent(),"Unexpected pre-existing diagnostic reference");
    const auto initial = chain.generation();
    check(client.open(initial) == Result::ready,"Known chain load");
    check(!absent() && client.exercise(),"Real diagnostic DLL and ABI frame");
    check(chain.counts.outer_load == 1 && chain.counts.inner_load == 1 && chain.counts.windows_load == 1,"Both load hooks preserved");
    check(client.open(initial) == Result::refused && chain.counts.windows_load == 1,"Duplicate load adds no reference");
    check(!chain.configure(false),"Configuration frozen while module lease exists");
    check(client.close() == Result::closed && absent(),"Owned module unloads");
    check(chain.counts.outer_unload == 1 && chain.counts.inner_unload == 1 && chain.counts.windows_unload == 1,"Both unload hooks preserved");
    check(chain.counts.sessions == 0 && !chain.counts.unload_with_session,"Session destroyed before FreeLibrary");
    check(!client.exercise(),"No API call after unload");
    check(client.close() == Result::closed && chain.counts.windows_unload == 1,"Repeated close does not decrement again");
    check(chain.configure(true),"Configuration changes after clean release");
    check(client.open(initial) == Result::refused && chain.counts.windows_load == 1,"Stale generation rejected before loader");
    check(chain.configure(false),"Unknown test chain configured without a lease");
    check(client.open(chain.generation()) == Result::refused && chain.counts.windows_load == 1,"Unknown chain rejected before loader");
    check(chain.configure(true),"Restore original test chain");
    const auto current = chain.generation();
    check(client.open(current,true) == Result::load_failed && absent() && client.state() == State::idle,"Missing DLL releases lease");
    const auto unloads = chain.counts.windows_unload.load();
    check(client.open(current,false,true) == Result::abi_failed && absent(),"ABI rejection cleans module");
    check(chain.counts.windows_unload == unloads+1 && chain.counts.sessions == 0,"ABI failure uses one matching unload");
    check(client.open(current) == Result::ready,"Load for failed-unload test");
    chain.fail_unload_once = true;
    const auto before_failure = chain.counts.windows_unload.load();
    check(client.close() == Result::cleanup_pending && !absent(),"Known pre-FreeLibrary failure retains module");
    check(chain.counts.windows_unload == before_failure && chain.counts.sessions == 0,"No hidden unload retry and session destroyed");
    check(!client.exercise() && client.open(current) == Result::refused && !chain.configure(true),"Cleanup-pending state blocks calls/reload/config changes");
    // Only this injected failure is known not to have reached FreeLibrary.
    check(client.close() == Result::closed && absent(),"Explicit cleanup after known fixture failure");
    for (unsigned cycle=0; cycle<10; ++cycle) {
        check(client.open(current) == Result::ready && client.exercise(),"Repeated synthetic lifecycle load");
        check(client.close() == Result::closed && absent(),"Repeated synthetic lifecycle unload");
    }
    check(!chain.counts.unload_with_session && chain.counts.sessions == 0,"All sessions drained");
    check(chain.counts.outer_load == chain.counts.inner_load && chain.counts.outer_unload == chain.counts.inner_unload,"All operations retained both hook layers");
}
void pending_case(const std::filesystem::path& dll) {
    FixtureChain chain(dll);
    FixtureClient client(chain);
    chain.hold_next_load();
    auto pending = std::async(std::launch::async,[&] { return client.open(chain.generation()); });
    struct ReleaseGate { FixtureChain& chain; ~ReleaseGate() { chain.release_gate(); } } release{chain};
    check(chain.await_entry(),"Owned worker entered delayed hook");
    check(pending.wait_for(20ms) == std::future_status::timeout,"Caller deadline observed without assuming completion");
    check(client.close() == Result::pending,"Cancel does not unload during active load");
    check(chain.counts.windows_unload == 0 && chain.counts.windows_load == 0,"No loader retry or premature unload");
    check(client.open(chain.generation()) == Result::refused,"No second load while first is pending");
    check(!chain.configure(false),"Pending operation holds configuration lease");
    chain.release_gate();
    check(pending.get() == Result::cancelled,"Late load completes and honors cancellation");
    // Always reconcile after joining: a cancellation racing the final commit
    // could have observed 'pending' just before open returned 'ready'.
    check(client.close() == Result::closed,"Joined operation permits explicit cleanup reconciliation");
    check(absent() && client.state() == State::idle && chain.counts.sessions == 0,"Late completion fully cleaned");
    check(chain.counts.outer_load == 1 && chain.counts.inner_load == 1 && chain.counts.windows_load == 1 &&
          chain.counts.outer_unload == 1 && chain.counts.inner_unload == 1 && chain.counts.windows_unload == 1,"Late result uses exactly one load/unload through both hooks");
    check(chain.configure(true),"Lease released after joined operation");
}
void pending_api_case(const std::filesystem::path& dll) {
    FixtureChain chain(dll);
    FixtureClient client(chain);
    check(client.open(chain.generation()) == Result::ready,"Load for borrowed-call shutdown test");
    chain.hold_next_call();
    auto call = std::async(std::launch::async,[&] { return client.exercise(); });
    struct ReleaseGate { FixtureChain& chain; ~ReleaseGate() { chain.release_gate(); } } release{chain};
    check(chain.await_entry(),"Borrowed API operation reached fixture gate");
    check(client.close() == Result::pending,"Shutdown waits for borrowed API operation");
    check(!absent() && chain.counts.sessions == 1 && chain.counts.windows_unload == 0,"Session and DLL remain alive during borrowed call");
    check(!client.exercise(),"Concurrent API operation rejected");
    chain.release_gate();
    check(!call.get() && chain.counts.updates == 1,"Cancelled operation completes but its result is suppressed");
    check(client.close() == Result::closed && absent(),"Joined API call permits unload");
    check(chain.counts.sessions == 0 && !chain.counts.unload_with_session && chain.counts.windows_unload == 1,"Shutdown drains session and releases one reference");
}
}
int main(int argc, char**) {
    if (argc != 1) { std::cerr << "Fixture accepts no target PID, path or other arguments.\n"; return 2; }
    try {
        const auto dll = adjacent_dll();
        synchronous_cases(dll);
        pending_case(dll);
        pending_api_case(dll);
        check(absent(),"Final diagnostic module absent");
        std::cout << "PASS: " << checks << " owned hook-chain/lifecycle checks. Original simulated hooks; no game access or production compatibility claim.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Hook lifecycle fixture failed: " << error.what() << '\n';
        return 1;
    }
}
