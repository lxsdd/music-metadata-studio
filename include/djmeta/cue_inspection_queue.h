#pragma once
// Read-only, transient CUE FILE inspection queue. Exact plan evidence,
// not an SDK File Operations resolver, CUE writer or execution approval.
#include "djmeta/cue_raw_route_impact.h"
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace djmeta {
struct CueQueuePlanRow {
    std::string physical_key,source_guard,destination_root,raw_filename;
    FileAction action=FileAction::None;
    bool explicitly_staged=false,physical_qualified=false;
    bool pending_review=false,raw_collision=false;
    bool operator==(const CueQueuePlanRow&) const = default;
};
struct CueQueueReceipt {
    std::size_t epoch=0,files=0,unselected=0,blocked=0;
    bool qualified_graph=false,raw_only=false,original_kept=false;
};
enum class CueQueueStage { Missing, Stale, Blocked, RawOnly, OriginalKept };
inline const char* cue_queue_label(CueQueueStage s) {
    switch(s) {
    case CueQueueStage::Missing:return "NOT_INSPECTED";
    case CueQueueStage::Stale:return "STALE_REINSPECT";
    case CueQueueStage::Blocked:return "BLOCKED";
    case CueQueueStage::RawOnly:return "RAW_ONLY_FINAL_CUE_UNQUALIFIED";
    case CueQueueStage::OriginalKept:return "ORIGINAL_CUE_KEPT";
    }
    return "INVALID";
}
struct CueQueueCounts {
    std::size_t total=0,missing=0,stale=0,blocked=0,raw_only=0,kept=0;
};
struct CueInspectionQueue {
    std::vector<CueQueuePlanRow> plan;
    std::map<std::size_t,CueQueueReceipt> receipts;
    std::size_t epoch=1;
    void observe(const std::vector<CueQueuePlanRow>& next) {
        if(next.size()>100000)throw std::invalid_argument("CUE_QUEUE_LIMIT");
        if(next==plan)return;
        if(epoch==static_cast<std::size_t>(-1))
            throw std::invalid_argument("CUE_QUEUE_EPOCH_EXHAUSTED");
        plan=next;++epoch; // all old receipts visibly STALE
    }
    void record(std::size_t id,const CueRawRouteImpact& graph,
                const CueSidecarRawImpact& sidecar) {
        if(id>=plan.size() || graph.references.size()>128)
            throw std::invalid_argument("CUE_QUEUE_RECEIPT_INVALID");
        receipts[id]={epoch,graph.references.size(),graph.unselected,
            graph.selected_blocked,graph.source_graph_qualified,
            sidecar.raw_only_candidate,
            sidecar.code=="CUE_SIDECAR_AND_ORIGINAL_FILE_LINKS_UNCHANGED"};
    }
    CueQueueStage stage(std::size_t id) const {
        if(id>=plan.size())throw std::invalid_argument("CUE_QUEUE_SOURCE_INVALID");
        auto it=receipts.find(id);
        if(it==receipts.end())return CueQueueStage::Missing;
        const auto& r=it->second;
        if(r.epoch!=epoch)return CueQueueStage::Stale;
        if(!r.qualified_graph || !r.files || r.unselected || r.blocked)
            return CueQueueStage::Blocked;
        if(r.raw_only)return CueQueueStage::RawOnly;
        if(r.original_kept)return CueQueueStage::OriginalKept;
        return CueQueueStage::Blocked;
    }
    CueQueueCounts counts(const std::vector<std::size_t>& carriers) const {
        if(carriers.size()>100000)throw std::invalid_argument("CUE_QUEUE_ROWS");
        CueQueueCounts c;
        std::size_t previous=0;
        for(std::size_t i=0;i<carriers.size();++i) {
            const auto id=carriers[i];
            if(id>=plan.size() || (i && id<=previous))
                throw std::invalid_argument("CUE_QUEUE_SOURCE_ORDER_INVALID");
            previous=id;++c.total;
            switch(stage(id)) {
            case CueQueueStage::Missing:++c.missing;break;
            case CueQueueStage::Stale:++c.stale;break;
            case CueQueueStage::Blocked:++c.blocked;break;
            case CueQueueStage::RawOnly:++c.raw_only;break;
            case CueQueueStage::OriginalKept:++c.kept;break;
            }
        }
        return c;
    }
    std::size_t next_attention(const std::vector<std::size_t>& carriers,
                               std::size_t cursor) const {
        (void)counts(carriers);
        const auto pending=[&](std::size_t id) {
            const auto value=stage(id);
            return value==CueQueueStage::Missing ||
                   value==CueQueueStage::Stale ||
                   value==CueQueueStage::Blocked;
        };
        for(auto id:carriers)if(id>cursor && pending(id))return id;
        for(auto id:carriers)if(pending(id))return id;
        return plan.size();
    }
    std::string export_tsv(const std::vector<std::size_t>& carriers,
                           bool issues_only=false) const {
        const auto c=counts(carriers);
        std::string out="CUE dependency queue\tREAD_ONLY_NOT_FILEOPS_APPROVAL\r\n";
        out+="Carriers\t"+std::to_string(c.total)+
            "\tNot inspected\t"+std::to_string(c.missing)+
            "\tStale\t"+std::to_string(c.stale)+
            "\tBlocked\t"+std::to_string(c.blocked)+
            "\tRAW only\t"+std::to_string(c.raw_only)+
            "\tOriginal kept\t"+std::to_string(c.kept)+"\r\n";
        out+="Source row\tCUE stage\tFinal FILE links\r\n";
        for(auto id:carriers) {
            const auto state=stage(id);
            if(issues_only && state==CueQueueStage::OriginalKept)continue;
            out+="#"+std::to_string(id+1)+"\t"+cue_queue_label(state)+
                 "\tNOT_QUALIFIED\r\n";
            if(out.size()>8u*1024u*1024u)
                throw std::invalid_argument("CUE_QUEUE_EXPORT_TOO_LARGE");
        }
        return out;
    }
};
// Project only the existing full-source CUE receipt queue onto an existing
// complete sorted permutation. No path resolver, CUE writer or approval.
inline std::vector<std::size_t> filter_cue_queue_view(
    const CueInspectionQueue& queue,
    const std::vector<std::size_t>& cue_sources,
    const std::vector<std::size_t>& sorted_full_source_ids,
    BatchLocationQueue filter) {
    if(filter!=BatchLocationQueue::CueAll &&
       filter!=BatchLocationQueue::CueAttention &&
       filter!=BatchLocationQueue::CueRawOnly &&
       filter!=BatchLocationQueue::CueOriginalKept)
        throw std::invalid_argument("CUE_QUEUE_FILTER_KIND");
    (void)queue.counts(cue_sources);
    const auto n=queue.plan.size();
    if(n>100000 || sorted_full_source_ids.size()!=n)
        throw std::invalid_argument("CUE_QUEUE_FILTER_SHAPE");
    std::vector<unsigned char> cue(n,0),seen(n,0);
    for(auto id:cue_sources)cue[id]=1;
    std::vector<std::size_t> visible;
    visible.reserve(cue_sources.size());
    for(const auto id:sorted_full_source_ids) {
        if(id>=n || seen[id]++)
            throw std::invalid_argument("CUE_QUEUE_FILTER_SORTED_ID");
        if(!cue[id])continue;
        const auto s=queue.stage(id);
        const bool yes=filter==BatchLocationQueue::CueAll ||
            (filter==BatchLocationQueue::CueAttention &&
             (s==CueQueueStage::Missing || s==CueQueueStage::Stale ||
              s==CueQueueStage::Blocked)) ||
            (filter==BatchLocationQueue::CueRawOnly &&
             s==CueQueueStage::RawOnly) ||
            (filter==BatchLocationQueue::CueOriginalKept &&
             s==CueQueueStage::OriginalKept);
        if(yes)visible.push_back(id);
    }
    return visible;
}
} // namespace djmeta
