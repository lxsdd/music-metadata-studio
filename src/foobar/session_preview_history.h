#pragma once
// Bounded, source-identity-free, in-memory audit of user-triggered preview
// actions. No metadata values, paths, provider credentials or write approvals.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
// Optional exact 1-based physical source filter. Invalid input must not
// silently fall back to exporting every source's history.
struct PreviewHistorySourceQuery {
    bool valid=true;
    std::optional<std::size_t> source;
};
inline PreviewHistorySourceQuery preview_history_parse_source(
    std::wstring_view text,std::size_t captured_batch_size) noexcept {
    if(text.empty()) return {};
    if(text.size()>7 || captured_batch_size==0 ||
       captured_batch_size>1000000) return {false,std::nullopt};
    std::size_t ordinal=0;
    for(wchar_t ch:text) {
        if(ch<L'0' || ch>L'9') return {false,std::nullopt};
        ordinal=ordinal*10+static_cast<std::size_t>(ch-L'0');
        if(ordinal>captured_batch_size) return {false,std::nullopt};
    }
    if(ordinal==0) return {false,std::nullopt};
    return {true,ordinal-1};
}
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
// Only visible History Ctrl+C, without other modifiers, requests a receipt.
inline bool preview_history_copy_hotkey(unsigned key,bool ctrl,bool alt,
                                        bool shift,bool visible) noexcept {
    return visible && key==static_cast<unsigned>(L'C') &&
           ctrl && !alt && !shift;
}
inline std::vector<std::size_t> preview_history_visible_indices(
    const SessionPreviewHistory& history,PreviewHistoryFilter filter,
    std::optional<std::size_t> source=std::nullopt) {
    std::vector<std::size_t> out;
    const auto& events=history.events();
    out.reserve(events.size());
    for(std::size_t i=0;i<events.size();++i)
        if(preview_history_matches(events[i].kind,filter) &&
           (!source || (events[i].sources==1 &&
                        events[i].source_index==*source))) out.push_back(i);
    return out;
}
inline std::optional<std::size_t> preview_history_single_source(
    const PreviewHistoryEvent& event,std::size_t batch_size) noexcept {
    if(event.sources!=1 || event.source_index>=batch_size) return std::nullopt;
    return event.source_index;
}

// The clipboard receipt mirrors the on-screen History labels using the
// same vocabulary. This is an audit of PREVIEW decisions, never an execution
// receipt or an implicit authorization to write.
inline const wchar_t* preview_history_receipt_action(PreviewHistoryKind kind) noexcept {
    switch(kind) {
    case PreviewHistoryKind::Accept: return L"Accept proposals";
    case PreviewHistoryKind::Reject: return L"Reject proposals";
    case PreviewHistoryKind::Reset: return L"Reset decisions";
    case PreviewHistoryKind::Manual: return L"Manual replacement";
    case PreviewHistoryKind::Route: return L"Recalculate route";
    case PreviewHistoryKind::UndoReview: return L"Undo review";
    case PreviewHistoryKind::UndoRoute: return L"Undo route";
    }
    return L"Unknown";
}
inline const wchar_t* preview_history_receipt_scope(const PreviewHistoryEvent& event) noexcept {
    if(event.kind==PreviewHistoryKind::Route)
        return event.scope==2 ? L"All tracks" : L"Selected tracks";
    if(event.kind==PreviewHistoryKind::UndoReview ||
       event.kind==PreviewHistoryKind::UndoRoute) return L"Last action";
    switch(event.scope) {
    case 0: return L"Selected fields";
    case 1: return L"Selected tracks";
    case 2: return L"All tracks";
    case 3: return L"Visible queue";
    default: return L"Unknown";
    }
}

// Exact filtered-row identity is resolved from the immutable event sequence,
// not from a sorted ListView caption. A selected row exports one event;
// otherwise the user explicitly exports the entire current filtered view.
// No path, tag value, provider ID, account or secret is stored or exported.
inline std::optional<std::wstring> preview_history_export_tsv(
    const SessionPreviewHistory& history, PreviewHistoryFilter filter,
    std::optional<std::size_t> selected_visible_row,
    std::size_t captured_batch_size,
    std::optional<std::size_t> source=std::nullopt) {
    if(source && *source>=captured_batch_size) return std::nullopt;
    if(captured_batch_size==0 || captured_batch_size>1000000 ||
       history.events().empty() ||
       history.events().size()>SessionPreviewHistory::limit)
        return std::nullopt;
    const auto visible=preview_history_visible_indices(history,filter,source);
    if(visible.empty() ||
       (selected_visible_row && *selected_visible_row>=visible.size()))
        return std::nullopt;
    std::wstring report=
        L"Music Metadata Studio - read-only session preview history\r\n"
        L"Step\tAction\tScope\tTracks\tItems\tPhysical source #\r\n";
    std::size_t previous_sequence=0;
    for(std::size_t pos=0;pos<visible.size();++pos) {
        if(selected_visible_row && pos!=*selected_visible_row) continue;
        const auto index=visible[pos];
        if(index>=history.events().size()) return std::nullopt;
        const auto& event=history.events()[index];
        if(event.sequence<=previous_sequence || event.sources==0 ||
           event.sources>captured_batch_size || event.items==0 ||
           event.items>1000000 || event.scope>3 ||
           std::wstring_view(preview_history_receipt_action(event.kind))==L"Unknown" ||
           (event.sources!=1 &&
            event.source_index!=static_cast<std::size_t>(-1)) ||
           (event.sources==1 &&
            event.source_index!=static_cast<std::size_t>(-1) &&
            event.source_index>=captured_batch_size))
            return std::nullopt;
        previous_sequence=event.sequence;
        report+=std::to_wstring(event.sequence);
        report+=L"\t";
        report+=preview_history_receipt_action(event.kind);
        report+=L"\t";
        report+=preview_history_receipt_scope(event);
        report+=L"\t";
        report+=std::to_wstring(event.sources);
        report+=L"\t";
        report+=std::to_wstring(event.items);
        report+=L"\t";
        const auto source=preview_history_single_source(event,captured_batch_size);
        report+=source ? std::to_wstring(*source+1) : L"(not captured / multiple)";
        report+=L"\r\n";
        if(report.size()>65536) return std::nullopt;
    }
    if(source) report+=L"Filtered to physical source #"+
                       std::to_wstring(*source+1)+L" (single-source events only).\r\n";
    report+=L"Only in-memory preview decisions are listed. "
            L"No metadata, CUE or file operation was executed by this history.\r\n";
    return report.size()<=65536 ? std::optional<std::wstring>(std::move(report))
                                : std::nullopt;
}

} // namespace djmeta_foobar
