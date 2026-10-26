#include "djmeta/precision_cue_physical_envelope.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>

using namespace djmeta;
#define CHECK(cond) do { if (!(cond)) { \
    std::fprintf(stderr,"Precision CUE storage-envelope test failed line %d: %s\n",\
        __LINE__,#cond); std::exit(1); } } while(false)

namespace {
namespace fs=std::filesystem;
struct TestDirectory {
    fs::path location;
    TestDirectory() {
        std::random_device random;
        location=fs::canonical(fs::temp_directory_path()) /
            ("precision-cue-storage-fixture-"+std::to_string(random())+
             "-"+std::to_string(random()));
        CHECK(fs::create_directory(location));
    }
    ~TestDirectory() {
        std::error_code ec;
        fs::remove_all(location,ec);
    }
};
void save(const fs::path& f,std::string_view bytes) {
    std::ofstream output(f,std::ios::binary);
    CHECK(output.good());
    output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    CHECK(output.good());
}
std::string read_all(const fs::path& f) {
    std::ifstream input(f,std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void synthetic_storage_envelope_matrix() {
    using State=PrecisionCuePhysicalEnvelopeState;
    TestDirectory dir;
    const auto audio=dir.location/"synthetic.mp3";
    std::string prefix="ID3";
    prefix+=std::string(4093,'T');
    const std::string payload(2u*65536u+81u,'A');
    const auto bytes=prefix+payload;
    save(audio,bytes);
    const auto original_time=fs::last_write_time(audio);
    const auto original=capture_precision_cue_physical_full_snapshot(audio,prefix);
    CHECK(original.has_value());
    auto inspection=inspect_precision_cue_physical_envelope(*original,prefix);
    CHECK(inspection.state==State::QualifiedForReadOnlyPlanning);
    CHECK(inspection.hard_link_count==1);
#ifdef _WIN32
    CHECK(inspection.stream_count==1);
    // A named NTFS data stream is outside the default-stream SHA256.
    // Reject rather than silently losing it on a future file replacement.
    const auto stream_name=audio.wstring()+L":cue_synthetic_hidden:$DATA";
    const HANDLE hidden=CreateFileW(stream_name.c_str(),GENERIC_WRITE,0,nullptr,
        CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    CHECK(hidden!=INVALID_HANDLE_VALUE);
    constexpr char secret[]="synthetic alternate stream; no real user data";
    DWORD written=0;
    CHECK(WriteFile(hidden,secret,sizeof(secret),&written,nullptr)!=0);
    CHECK(written==sizeof(secret));
    CHECK(CloseHandle(hidden)!=0);
    // NTFS stream writes may advance the parent file's last-write timestamp.
    // Preserve the reviewed timestamp so ADS detection is tested independently.
    fs::last_write_time(audio,original_time);
    inspection=inspect_precision_cue_physical_envelope(*original,prefix);
    CHECK(inspection.state==State::NeedsReview);
    CHECK(inspection.stream_count>=2);
    CHECK(DeleteFileW(stream_name.c_str())!=0);
    fs::last_write_time(audio,original_time);
    inspection=inspect_precision_cue_physical_envelope(*original,prefix);
    CHECK(inspection.state==State::QualifiedForReadOnlyPlanning);
#endif
    // Additional hard link must be rejected, even though the file bytes
    // and physical identity match the originally approved source.
    const auto alias=dir.location/"another-name.mp3";
    fs::create_hard_link(audio,alias);
    inspection=inspect_precision_cue_physical_envelope(*original,prefix);
    CHECK(inspection.state==State::NeedsReview);
    CHECK(inspection.hard_link_count>=2);
    fs::remove(alias);
    inspection=inspect_precision_cue_physical_envelope(*original,prefix);
    CHECK(inspection.state==State::QualifiedForReadOnlyPlanning);

    // Full-file SHA rejects an in-place audio modification even with a
    // deliberately restored timestamp and identical file length.
    {
        std::fstream file(audio,std::ios::binary|std::ios::in|std::ios::out);
        CHECK(file.good());
        file.seekp(static_cast<std::streamoff>(prefix.size()+101));
        file.put('X');
    }
    fs::last_write_time(audio,original_time);
    CHECK(fs::file_size(audio)==bytes.size());
    CHECK(inspect_precision_cue_physical_envelope(
        *original,prefix).state==State::NeedsReview);
    save(audio,bytes);
    fs::last_write_time(audio,original_time);
    CHECK(inspect_precision_cue_physical_envelope(
        *original,prefix).state==State::QualifiedForReadOnlyPlanning);
    CHECK(read_all(audio)==bytes);

    // An incorrect prefix never becomes approved, regardless of disk state.
    CHECK(inspect_precision_cue_physical_envelope(
        *original,"WRONG").state==State::NeedsReview);
    CHECK(!capture_precision_cue_physical_full_snapshot(
        dir.location,prefix));
}
}
int main() {
    synthetic_storage_envelope_matrix();
    std::puts("PASS: read-only physical envelope rejects NTFS ADS, aliases and stale audio");
    return 0;
}
