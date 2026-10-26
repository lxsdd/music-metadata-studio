#pragma once
// Bounded, source-identity-free, in-memory audit of user-triggered preview
// actions. No metadata values, paths, provider credentials or write approvals.
#include <cstddef>
#include <vector>

namespace djmeta_foobar {

enum class PreviewHistoryKind {
    Accept, Reject, Reset, Manual, Route, UndoReview, UndoRoute
};
struct PreviewHistoryEvent {
    std::size_t sequence=0;
    PreviewHistoryKind kind=PreviewHistoryKind::Reset;
    std::size_t sources=0;
    std::size_t items=0;
    unsigned scope=0; // review: field/selected/all/visible; route: selected/all
};
class SessionPreviewHistory {
public:
    static constexpr std::size_t limit=256;
    bool append(PreviewHistoryKind kind,std::size_t sources,
                std::size_t items,unsigned scope) {
        if(sources==0 || items==0) return false; // no-op is not an event
        if(events_.size()==limit) events_.erase(events_.begin());
        events_.push_back({++sequence_,kind,sources,items,scope});
        return true;
    }
    void clear() noexcept {events_.clear();sequence_=0;}
    const std::vector<PreviewHistoryEvent>& events() const noexcept {
        return events_;
    }
private:
    std::size_t sequence_=0;
    std::vector<PreviewHistoryEvent> events_;
};
} // namespace djmeta_foobar
