#pragma once
// Bounded, source-identity-free, in-memory audit of user-triggered preview
// actions. No metadata values, paths, provider credentials or write approvals.
#include <cstddef>
#include <optional>
#include <vector>

namespace djmeta_foobar {

enum class PreviewHistoryKind {
    Accept, Reject, Reset, Manual, Route, UndoReview, UndoRoute
};
enum class PreviewHistoryFilter { All, Review, Routing, Undo };
struct PreviewHistoryEvent {
    std::size_t sequence=0;
    PreviewHistoryKind kind=PreviewHistoryKind::Reset;
    std::size_t sources=0;
    std::size_t items=0;
    unsigned scope=0; // review: field/selected/all/visible; route: selected/all
    std::size_t source_index=static_cast<std::size_t>(-1); // single captured source only
};
class SessionPreviewHistory {
public:
    static constexpr std::size_t limit=256;
    bool append(PreviewHistoryKind kind,std::size_t sources,
                std::size_t items,unsigned scope,
                std::size_t source_index=static_cast<std::size_t>(-1)) {
        if(sources==0 || items==0) return false; // no-op is not an event
        if(events_.size()==limit) events_.erase(events_.begin());
        events_.push_back({++sequence_,kind,sources,items,scope,
            sources==1 ? source_index : static_cast<std::size_t>(-1)});
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
// A display filter never mutates, renumbers or authorizes the source events.
inline bool preview_history_matches(PreviewHistoryKind kind,
                                    PreviewHistoryFilter filter) noexcept {
    if(filter==PreviewHistoryFilter::All) return true;
    if(filter==PreviewHistoryFilter::Routing) return kind==PreviewHistoryKind::Route;
    if(filter==PreviewHistoryFilter::Undo)
        return kind==PreviewHistoryKind::UndoReview ||
               kind==PreviewHistoryKind::UndoRoute;
    return kind==PreviewHistoryKind::Accept ||
           kind==PreviewHistoryKind::Reject ||
           kind==PreviewHistoryKind::Reset ||
           kind==PreviewHistoryKind::Manual;
}
inline std::vector<std::size_t> preview_history_visible_indices(
    const SessionPreviewHistory& history,PreviewHistoryFilter filter) {
    std::vector<std::size_t> out;
    const auto& events=history.events();
    out.reserve(events.size());
    for(std::size_t i=0;i<events.size();++i)
        if(preview_history_matches(events[i].kind,filter)) out.push_back(i);
    return out;
}
inline std::optional<std::size_t> preview_history_single_source(
    const PreviewHistoryEvent& event,std::size_t batch_size) noexcept {
    if(event.sources!=1 || event.source_index>=batch_size) return std::nullopt;
    return event.source_index;
}
} // namespace djmeta_foobar
