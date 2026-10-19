#pragma once

// Native Win32 HTTP worker boundary. The callable passed to this helper may
// perform only network I/O with independently qualified immutable parameters.
// foobar SDK handles, CUE readers, native controls, parsing/review mutations
// and metadata snapshots stay on the dialog's main UI thread.
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

namespace djmeta_foobar {

inline constexpr UINT kReadOnlyNetworkDoneMessage = WM_APP + 0x351;

// UI-thread-only request identity; text edits and restoration must not
// resurrect a pending provider reply.
class ReadOnlyContextVersion {
public:
    std::uint64_t current() const noexcept { return value_; }
    void invalidate() noexcept { ++value_; }
    bool matches(std::uint64_t captured) const noexcept {
        return captured == value_;
    }
private:
    std::uint64_t value_=1;
};

struct ReadOnlyNetworkJob {
    // Join before response/error/atomic members are destroyed. Worker never
    // retains a self-owning shared_ptr that could cause a self-join.
    ~ReadOnlyNetworkJob() { if(worker.joinable()) worker.join(); }
    ReadOnlyNetworkJob()=default;
    ReadOnlyNetworkJob(const ReadOnlyNetworkJob&)=delete;
    ReadOnlyNetworkJob& operator=(const ReadOnlyNetworkJob&)=delete;
    std::jthread worker;
    std::atomic<bool> finished{false};
    std::string response;
    std::string error;
};

template<typename Fetch>
std::shared_ptr<ReadOnlyNetworkJob> start_readonly_network(
    HWND notify, Fetch fetch) {
    if(!notify || !IsWindow(notify))
        throw std::invalid_argument("Missing native request window");
    auto job=std::make_shared<ReadOnlyNetworkJob>();
    auto* target=job.get();
    job->worker=std::jthread(
        [target,notify,task=std::move(fetch)](std::stop_token stop) mutable {
            try {
                // Always invoke task to wipe captured provider credentials.
                target->response=task(stop);
            } catch(const std::exception& error) {
                target->error=error.what();
            } catch(...) {
                target->error="Provider request failed without details.";
            }
            if(stop.stop_requested()) {
                target->response.clear();
                target->error="Provider request cancelled.";
            }
            // Release/acquire makes response/error immutable to the UI.
            target->finished.store(true,std::memory_order_release);
            // No pointers or secrets travel through a posted message.
            // A failed PostMessage during teardown is safe: the owner joins.
            (void)PostMessageW(notify,kReadOnlyNetworkDoneMessage,0,0);
        });
    return job;
}
inline void request_readonly_network_stop(ReadOnlyNetworkJob& job) noexcept {
    job.worker.request_stop();
}
inline bool readonly_network_finished(const ReadOnlyNetworkJob& job) {
    return job.finished.load(std::memory_order_acquire);
}

} // namespace djmeta_foobar
