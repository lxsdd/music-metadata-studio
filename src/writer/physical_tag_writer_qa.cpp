#include "stdafx.h"
#include "physical_tag_writer.h"
#include "host_file_probe.h"
#include "metadata_adapter.h"
#include "guid.h"
#include <SDK/filesystem.h>
#include <cstring>
#include <string>

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
       (output[dir.size()]!='\\\\' && output[dir.size()]!='/') ||
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
const GUID qa_guid={
    0xc52b7f9d,0x5088,0x46e5,{0xb4,0x9d,0x0d,0xaa,0x43,0xb1,0xe9,0x29}
};
class isolated_writer_qa_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override{return guids::context_group;}
    unsigned get_num_items() override{return 1;}
    t_enabled_state get_enabled_state(unsigned) override{
        return contextmenu_item::DEFAULT_ON;
    }
    void get_item_name(unsigned i,pfc::string_base& out) override {
        if(i==0)out="QA write synthetic tags";
    }
    GUID get_item_guid(unsigned i) override {
        return i==0?qa_guid:pfc::guid_null;
    }
    bool get_item_description(unsigned i,pfc::string_base& out) override {
        if(i!=0)return false;
        out="Private GitHub-runner disposable FLAC only. Never user music.";
        return true;
    }
    bool context_get_display(unsigned i,metadb_handle_list_cref data,
        pfc::string_base& out,unsigned& flags,const GUID& caller) override {
        if(i!=0 || data.get_count()!=1)return false;
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
contextmenu_item_factory_t<isolated_writer_qa_menu> g_isolated_writer_qa;
}
}
#endif
