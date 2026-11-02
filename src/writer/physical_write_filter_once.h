#pragma once
#include <atomic>
namespace djmeta_foobar {
enum class FilterResult { Pending, Wrote, Noop, Duplicate };
class FilterOnce {
public:
 bool enter() noexcept {int old=0; if(!state.compare_exchange_strong(old,1)){state.store(4);return false;}return true;}
 void wrote() noexcept {finish(2);}
 void noop() noexcept {finish(3);}
 FilterResult result() const noexcept {const int n=state.load();return n==2?FilterResult::Wrote:n==3?FilterResult::Noop:n==4?FilterResult::Duplicate:FilterResult::Pending;}
private:
 void finish(int n) noexcept {int old=1;if(!state.compare_exchange_strong(old,n))state.store(4);}
 std::atomic<int> state{0};
};
}
