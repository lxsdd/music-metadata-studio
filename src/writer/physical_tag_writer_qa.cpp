#include "stdafx.h"
#include "physical_tag_writer.h"
#include "host_file_probe.h"
#include "metadata_adapter.h"
#include "cue_readonly_source.h"
#include "external_cue_write_controller.h"
#include "guid.h"
#include <SDK/filesystem.h>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

// QA-only context entry, never compiled into the shipping read-only binary.
// It can operate exclusively on one byte-exact named synthetic FLAC in a
// disposable GitHub runner. Its provider evidence is synthetic and MUST
// never become a real online tag-import shortcut.
#if defined(MMS_ENABLE_PHYSICAL_WRITES)
namespace djmeta_foobar {
namespace {
// Bounded QA-only diagnostics in the runner's disposable directory.
void trace_writer_qa(const char* value) {
    if(!value || std::strlen(value)>120) return;
    char ci[16]{},root[4096]{},path[4096]{};
    if(GetEnvironmentVariableA("GITHUB_ACTIONS",ci,sizeof(ci))!=4 ||
       std::strcmp(ci,"true")!=0 ||
       GetEnvironmentVariableA("RUNNER_TEMP",root,sizeof(root))==0 ||
       GetEnvironmentVariableA("MMS_QA_REPORT_PATH",path,sizeof(path))==0)
        return;
    const std::string dir=root,output=path;
    if(output.size()<dir.size()+24 ||
       _strnicmp(output.c_str(),dir.c_str(),dir.size())!=0 ||
       (output[dir.size()]!='\\' && output[dir.size()]!='/') ||
       output.find("..")!=std::string::npos ||
       output.substr(output.size()-21)!="mms-writer-report.txt")
        return;
    const HANDLE file=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ |
        FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    std::string message(value);
    message+='\n';
    DWORD count=0;
    WriteFile(file,message.data(),static_cast<DWORD>(message.size()),&count,nullptr);
    CloseHandle(file);
}
bool qa_source_qualified(metadb_handle_ptr handle) {
    if(!handle.is_valid() || handle->get_subsong_index()!=0 ||
       !handle->get_path())return false;
    char ci[16]{}, expected[4096]{}, root[4096]{};
    if(GetEnvironmentVariableA("GITHUB_ACTIONS",ci,sizeof(ci))!=4 ||
       std::strcmp(ci,"true")!=0 ||
       GetEnvironmentVariableA("MMS_QA_WRITE_SOURCE",expected,sizeof(expected))==0 ||
       GetEnvironmentVariableA("RUNNER_TEMP",root,sizeof(root))==0)
        return false;
    pfc::string8 canonical,native;
    try {
        filesystem::g_get_canonical_path(handle->get_path(),canonical);
        if(!filesystem::g_get_native_path(
            canonical.get_ptr(),native,fb2k::noAbort))return false;
    } catch(...) {return false;}
    const std::string actual=native.get_ptr();
    const std::string selected=expected;
    const std::string sandbox=root;
    if(actual.size()<sandbox.size()+24 || selected!=actual ||
       _strnicmp(actual.c_str(),sandbox.c_str(),sandbox.size())!=0)
        return false;
    const char separator=actual[sandbox.size()];
    if(separator!='\\' && separator!='/')return false;
    const auto slash=actual.find_last_of("\\/");
    if(slash==std::string::npos ||
       _stricmp(actual.substr(slash+1).c_str(),"mms-writer-qa.flac")!=0)
        return false;
    const auto observed=probe_host_file_readonly(handle->get_path());
    return observed.state==HostFileState::ExistingFile &&
           !observed.host_physical_key.empty() &&
           !observed.source_guard.empty();
}
bool qa_cue_source_qualified(metadb_handle_ptr handle) {
    if(!handle.is_valid() || !handle->get_path())return false;
    char ci[16]{},expected[4096]{},root[4096]{};
    if(GetEnvironmentVariableA("GITHUB_ACTIONS",ci,sizeof(ci))!=4 ||
       std::strcmp(ci,"true")!=0 ||
       GetEnvironmentVariableA("MMS_QA_CUE_SOURCE",expected,sizeof(expected))==0 ||
       GetEnvironmentVariableA("RUNNER_TEMP",root,sizeof(root))==0)
        return false;
    pfc::string8 canonical,native;
    try {
        filesystem::g_get_canonical_path(handle->get_path(),canonical);
        if(!filesystem::g_get_native_path(
            canonical.get_ptr(),native,fb2k::noAbort))return false;
    } catch(...) {return false;}
    const std::string actual=native.get_ptr();
    const std::string selected=expected;
    const std::string sandbox=root;
    if(actual.size()<sandbox.size()+24 || selected!=actual ||
       _strnicmp(actual.c_str(),sandbox.c_str(),sandbox.size())!=0)
        return false;
    if(actual[sandbox.size()]!='\\' && actual[sandbox.size()]!='/')
        return false;
    const auto slash=actual.find_last_of("\\/");
    if(slash==std::string::npos ||
       _stricmp(actual.substr(slash+1).c_str(),"mms-writer-qa.cue")!=0)
        return false;
    const auto observed=probe_host_file_readonly(handle->get_path());
    return observed.state==HostFileState::ExistingFile &&
           !observed.host_physical_key.empty() &&
           !observed.source_guard.empty();
}

djmeta::online::ReleaseEdition qa_cue_edition() {
    using namespace djmeta::online;
    ReleaseEdition edition;
    edition.provider="musicbrainz";
    edition.edition_id="00000000-0000-0000-0000-000000000001";
    edition.fields.push_back({"ALBUM",{"Other album"},"musicbrainz",
                              edition.edition_id,EvidenceScope::Edition});
    EditionTrack track;
    track.source_track_id="00000000-0000-0000-0000-000000000002";
    track.recording={"Sunrise (Extended Mix)","DJ","",
                     MixKind::Extended,"GBABC2600001",-1};
    track.track_number=1;
    track.disc_number=1;
    track.fields.push_back({"TITLE",{"Corrected Sunrise (Extended Mix)"},
                            "musicbrainz",track.source_track_id,
                            EvidenceScope::Edition});
    edition.tracks.push_back(track);
    return edition;
}

void run_cue_writer_qa(metadb_handle_ptr handle) {
    trace_writer_qa("QA_CUE_ACTION_ENTERED");
    if(!qa_cue_source_qualified(handle)) {
        trace_writer_qa("QA_CUE_SOURCE_REJECTED");
        return;
    }
    try {
        const std::string path=handle->get_path();
        const auto source=read_cue_raw_on_demand(handle,path);
        if(source.carrier!=djmeta::CueCarrierKind::ExternalText)
            throw std::invalid_argument("QA_CUE_WRONG_CARRIER");
        const auto edition=qa_cue_edition();
        const auto snapshot=djmeta::online::capture_qualified_cue_edition_postimage(
            source.raw_text,source.carrier,edition);
        if(!snapshot.eligible || snapshot.rows.size()!=2)
            throw std::invalid_argument("QA_CUE_EDITION_NOT_QUALIFIED");
        const std::vector<std::size_t> selected{0,1};
        const auto post=djmeta::online::preview_selected_cue_review_postimage(
            snapshot,source.raw_text,selected);
        if(!post.eligible)
            throw std::invalid_argument("QA_CUE_SELECTION_NOT_QUALIFIED");
        if(!post.changed) {
            trace_writer_qa("QA_CUE_NOOP");
            return;
        }
        const auto reviewed=prepare_external_cue_host_review(
            handle,path,edition,selected,post);
        const auto result=commit_confirmed_external_cue_review(
            reviewed,handle,edition);
        if(result.status==djmeta::online::ExternalCueCommitStatus::Committed)
            trace_writer_qa("QA_CUE_COMMITTED");
        else
            trace_writer_qa("QA_CUE_COMMIT_REFUSED");
    } catch(const std::exception& error) {
        trace_writer_qa("QA_CUE_EXCEPTION");
        console::print((std::string("MMS_QA_CUE_EXCEPTION=")+error.what()).c_str());
    }
}

const GUID qa_guid={
    0xc52b7f9d,0x5088,0x46e5,{0xb4,0x9d,0x0d,0xaa,0x43,0xb1,0xe9,0x29}
};
const GUID qa_cue_guid={
    0x7c1c4ab8,0x864e,0x431e,{0x9a,0x83,0x8a,0xc4,0xd2,0x2e,0x9b,0x15}
};
class isolated_writer_qa_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override{return guids::context_group;}
    unsigned get_num_items() override{return 2;}
    t_enabled_state get_enabled_state(unsigned) override{
        return contextmenu_item::DEFAULT_ON;
    }
    void get_item_name(unsigned i,pfc::string_base& out) override {
        if(i==0)out="QA write synthetic tags";
        if(i==1)out="QA write synthetic CUE";
    }
    GUID get_item_guid(unsigned i) override {
        return i==0?qa_guid:(i==1?qa_cue_guid:pfc::guid_null);
    }
    bool get_item_description(unsigned i,pfc::string_base& out) override {
        if(i>1)return false;
        out=i==0?"Private disposable FLAC only. Never user music.":
                 "Private disposable external CUE only. Never user music.";
        return true;
    }
    bool context_get_display(unsigned i,metadb_handle_list_cref data,
        pfc::string_base& out,unsigned& flags,const GUID& caller) override {
        if(i>1 || data.get_count()!=1)return false;
        if(i==1)
            return qa_cue_source_qualified(data[0]) &&
                contextmenu_item_simple::context_get_display(
                    i,data,out,flags,caller);
        trace_writer_qa("QA_MENU_VISITED");
        if(!qa_source_qualified(data[0])) {
            trace_writer_qa("QA_MENU_SOURCE_REJECTED");
            return false;
        }
        trace_writer_qa("QA_MENU_SOURCE_ACCEPTED");
        return contextmenu_item_simple::context_get_display(
            i,data,out,flags,caller);
    }
    void context_command(unsigned i,metadb_handle_list_cref data,
                         const GUID&) override {
        if(i==1) {
            if(data.get_count()==1)run_cue_writer_qa(data[0]);
            return;
        }
        trace_writer_qa("QA_ACTION_ENTERED");
        if(i!=0 || data.get_count()!=1 || !qa_source_qualified(data[0])) {
            trace_writer_qa("QA_ACTION_SOURCE_REJECTED");
            return;
        }
        trace_writer_qa("QA_ACTION_SOURCE_ACCEPTED");
        const auto handle=data[0];
        const auto observed=probe_host_file_readonly(handle->get_path());
        const auto current=metadata_from_file_info(handle->get_info_ref()->info());
        djmeta::PhysicalTagSourceSnapshot captured{
            true,false,observed.host_physical_key,
            observed.source_guard,current};
        const djmeta::online::FieldEvidence synthetic{
            "TITLE",{"MMS QA UPDATED (SDK)"},"discogs",
            "qa-synthetic-physical",djmeta::online::EvidenceScope::Edition};
        const auto review=djmeta::online::capture_online_field_review(
            current,{synthetic},true,true);
        const auto projected=djmeta::online::stage_selected_online_fields(
            review,current,{0});
        const djmeta::online::FrozenPhysicalTagWrite frozen{
            handle->get_path(),captured,review,projected,{0},{}};
        const auto start=submit_frozen_physical_tag_write(
            handle,frozen,[](PhysicalWriteReport result) {
                trace_writer_qa(("QA_CALLBACK_"+result.diagnostic).c_str());
                console::print(("MMS_SYNTHETIC_QA_CALLBACK="+
                    result.diagnostic).c_str());
            });
        trace_writer_qa(("QA_DISPATCH_"+start.diagnostic).c_str());
        console::print(("MMS_SYNTHETIC_QA_DISPATCH="+start.diagnostic).c_str());
    }
};
// Probe only the disposable QA build at foobar's normal component init.
class isolated_qa_init : public initquit {
public:
    void on_init() override { trace_writer_qa("QA_COMPONENT_INITIALIZED"); }
};
initquit_factory_t<isolated_qa_init> g_isolated_qa_init;
contextmenu_item_factory_t<isolated_writer_qa_menu> g_isolated_writer_qa;
}
}
#endif
