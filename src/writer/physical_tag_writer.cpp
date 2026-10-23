#include "stdafx.h"
#include "physical_tag_writer.h"
#include "physical_write_filter_once.h"
#include "host_file_probe.h"
#include "metadata_adapter.h"
#include "djmeta/physical_sdk_postimage.h"
#include <SDK/file_info_filter.h>
#include <SDK/input.h>
#include <SDK/metadb.h>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace djmeta_foobar {
namespace {
using namespace djmeta;
using namespace djmeta::online;
bool physical_identity_unchanged(const FrozenPhysicalTagWrite& frozen) {
    const auto live=probe_host_file_readonly(frozen.source_path);
    return live.state==HostFileState::ExistingFile &&
           live.host_physical_key==frozen.physical.host_physical_key &&
           live.source_guard==frozen.physical.source_guard;
}
bool cue_extension(std::string_view path) {
    if(path.size()<4)return false;
    std::string ext(path.substr(path.size()-4));
    for(char& c:ext)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);
    return ext==".cue";
}
PhysicalWriteGate fresh_gate(const FrozenPhysicalTagWrite& frozen,
                             const file_info& info) {
    return verify_frozen_physical_write(
        frozen,metadata_from_file_info(info),frozen.source_path,
        frozen.physical.host_physical_key,frozen.physical.source_guard);
}
}
PhysicalWriteReport submit_frozen_physical_tag_write(
    const metadb_handle_ptr& source,const FrozenPhysicalTagWrite& frozen,
    PhysicalWriteCompletion completion) {
    // This entrypoint is MAIN-THREAD ONLY. Callers must separately collect a
    // final human confirmation. The backend never schedules itself.
    if(!source.is_valid() || !source->get_path() ||
       source->get_subsong_index()!=0 ||
       frozen.source_path!=source->get_path() ||
       cue_extension(frozen.source_path) ||
       !physical_identity_unchanged(frozen))
        return {PhysicalWriteOutcome::Refused,"PHYSICAL_IDENTITY_NOT_QUALIFIED"};
    const auto cache=source->get_info_ref();
    const auto original=fresh_gate(frozen,cache->info());
    if(original.state==PhysicalWriteGateState::Blocked)
        return {PhysicalWriteOutcome::Refused,"FROZEN_ONLINE_EVIDENCE_REJECTED"};
    if(original.state==PhysicalWriteGateState::Noop)
        return {PhysicalWriteOutcome::Noop,"Exactly unchanged: no SDK operation."};
    const auto approved_changes=physical_sdk_changed_field_approvals(
        frozen.physical.original,original.postimage.planned);
    file_info_impl detached;
    const auto prepared=project_explicit_physical_sdk_postimage(
        cache->info(),detached,frozen.physical,
        frozen.physical.host_physical_key,frozen.physical.source_guard,
        approved_changes,original.postimage,
        [](const file_info& i){return metadata_from_file_info(i);});
    if(prepared.status!=PhysicalSdkPostimageStatus::Detached ||
       !file_info::g_is_info_equal(cache->info(),detached))
        return {PhysicalWriteOutcome::Refused,"SDK_TAG_ROUNDTRIP_REFUSED"};
    // This shared outcome and all frozen provider evidence are captured by
    // value, safely surviving modeless UI close and SDK worker lifetime.
    auto filter_result=std::make_shared<FilterOnce>();
    auto filter=file_info_filter::create(
        [frozen,filter_result](trackRef handle,t_filestats,
                              file_info& on_disk)->bool {
            if(!handle.is_valid() || !handle->get_path() ||
               handle->get_subsong_index()!=0 ||
               frozen.source_path!=handle->get_path() ||
               !physical_identity_unchanged(frozen))
                throw std::runtime_error("SDK_LOCKED_SOURCE_CHANGED");
            if(!filter_result->enter())
                throw std::runtime_error("SDK_FILTER_DUPLICATE_INVOCATION");
            const auto approved=fresh_gate(frozen,on_disk);
            if(approved.state==PhysicalWriteGateState::Blocked)
                throw std::runtime_error("SDK_RELOADED_TAGS_STALE");
            if(approved.state==PhysicalWriteGateState::Noop) {
                filter_result->noop();
                return false;
            }
            const auto actual=metadata_from_file_info(on_disk);
            const auto changes=physical_sdk_changed_field_approvals(
                actual,approved.postimage.planned);
            file_info_impl proposed;
            const auto status=project_explicit_physical_sdk_postimage(
                on_disk,proposed,frozen.physical,
                frozen.physical.host_physical_key,frozen.physical.source_guard,
                changes,approved.postimage,
                [](const file_info& i){return metadata_from_file_info(i);});
            if(status.status!=PhysicalSdkPostimageStatus::Detached ||
               !file_info::g_is_info_equal(on_disk,proposed))
                throw std::runtime_error("SDK_LOCKED_POSTIMAGE_MISMATCH");
            on_disk.copy(proposed);
            filter_result->wrote();
            return true;
        });
    auto notify=fb2k::makeCompletionNotify(
        [frozen,filter_result,completion=std::move(completion)](unsigned code){
            PhysicalWriteReport outcome;
            if(code==metadb_io::update_info_aborted)
                outcome={PhysicalWriteOutcome::Cancelled,"SDK_UPDATE_CANCELLED"};
            else if(code!=metadb_io::update_info_success)
                outcome={PhysicalWriteOutcome::Failed,"SDK_UPDATE_FAILED"};
            else if(filter_result->result()==FilterResult::Noop)
                outcome={PhysicalWriteOutcome::Noop,"SDK_NO_CHANGED_TAGS"};
            else if(filter_result->result()!=FilterResult::Wrote)
                outcome={PhysicalWriteOutcome::ReadbackMismatch,
                         "SDK_FILTER_DID_NOT_CONFIRM_WRITE"};
            else {
                // Never use a virtual playlist handle or cached tag container
                // as evidence of a completed physical disk mutation.
                try {
                    input_info_reader::ptr input;
                    input_entry::g_open_for_info_read(
                        input,nullptr,frozen.source_path.c_str(),fb2k::noAbort);
                    file_info_impl readback;
                    input->get_info(0,readback,fb2k::noAbort);
                    const auto reference=fresh_gate(
                        frozen,*static_cast<const file_info*>(&readback));
                    // The readback is compared against the *frozen* planned
                    // postimage, not re-approved as a new source snapshot.
                    const auto expected=verify_frozen_physical_write(
                        frozen,frozen.physical.original,frozen.source_path,
                        frozen.physical.host_physical_key,
                        frozen.physical.source_guard);
                    const auto now=probe_host_file_readonly(frozen.source_path);
                    if(reference.state!=PhysicalWriteGateState::Blocked ||
                       expected.state!=PhysicalWriteGateState::Changed ||
                       now.state!=HostFileState::ExistingFile ||
                       now.host_physical_key!=frozen.physical.host_physical_key ||
                       compare_physical_text_tags(
                           metadata_from_file_info(readback),
                           expected.postimage.planned)!=TagWriteDecision::Unchanged)
                        outcome={PhysicalWriteOutcome::ReadbackMismatch,
                                 "POST_WRITE_PHYSICAL_READBACK_MISMATCH"};
                    else
                        outcome={PhysicalWriteOutcome::Completed,
                                 "SDK_WRITE_AND_PHYSICAL_TAG_READBACK_OK"};
                } catch(const std::exception&) {
                    outcome={PhysicalWriteOutcome::ReadbackMismatch,
                             "POST_WRITE_PHYSICAL_READBACK_FAILED"};
                }
            }
            if(completion)completion(std::move(outcome));
        });
    metadb_handle_list one;
    one.add_item(source);
    metadb_io_v2::get()->update_info_async(
        one,filter,nullptr,metadb_io_v2::op_flag_delay_ui,notify);
    return {PhysicalWriteOutcome::Submitted,
            "SDK callback attached, awaiting completion/readback."};
}
} // namespace djmeta_foobar
