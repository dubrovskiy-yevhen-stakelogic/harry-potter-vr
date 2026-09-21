#include "hpvr/quest_asset_cache.h"
#include <iostream>
#include <iterator>
#include <thread>

namespace {
using namespace hpvr::quest::cache;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void Rejected(F&& operation,const char* message){
    bool rejected=false;try{operation();}catch(const CacheError&){rejected=true;}
    Check(rejected,message);
}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("hpvr-cooked-test-"+std::to_string(detail::ProcessId())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary(){Check(std::filesystem::create_directory(path),"isolated temporary test directory");}
    ~Temporary(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
void WriteBytes(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    Check(bool(out),"write synthetic fixture");
}
std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);Check(bool(in),"read synthetic fixture");
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),{});
}
struct Example {
    std::uint32_t unsigned_value=17;
    std::int32_t signed_value=-81;
    std::uint64_t wide=0xfedcba9876543210ULL;
    float scalar=3.25F;
    bool flag=true;
    std::string name="owned scene";
    std::vector<std::uint8_t> pixels{1,2,3,4,0,255};
    std::array<std::uint32_t,3> raw{6,7,8};
    bool operator==(const Example&)const=default;
};
void Save(const std::filesystem::path& path,std::uint64_t fingerprint,const Example& example){
    Writer writer(path,1,fingerprint);writer.U32(example.unsigned_value);writer.I32(example.signed_value);
    writer.U64(example.wide);writer.F32(example.scalar);writer.Bool(example.flag);writer.String(example.name);
    writer.Bytes(example.pixels);writer.Raw(example.raw.data(),sizeof(example.raw));writer.Finish();
}
bool Load(const std::filesystem::path& path,std::uint64_t fingerprint,Example* destination){
    try{
        Reader reader(path,1,fingerprint);Example candidate;
        candidate.unsigned_value=reader.U32();candidate.signed_value=reader.I32();candidate.wide=reader.U64();
        candidate.scalar=reader.F32();candidate.flag=reader.Bool();candidate.name=reader.String(256);
        candidate.pixels=reader.Bytes(256);reader.CheckCount(candidate.raw.size(),3,sizeof(std::uint32_t));
        reader.Raw(candidate.raw.data(),sizeof(candidate.raw));reader.Finish();*destination=std::move(candidate);return true;
    }catch(const CacheError&){return false;}
}
void NoTemporaries(const std::filesystem::path& path){
    for(const auto& entry:std::filesystem::directory_iterator(path))
        Check(entry.path().filename().string().find(".tmp-")==std::string::npos,"private temporary files cleaned");
}
}

int main(){try{
    using namespace hpvr::quest::cache;
    Temporary temporary;const auto good=temporary.path/"map1.hpvrscene";
    constexpr SourceFingerprint fingerprint=0x1234567890abcdefULL;
    Check(CookRevision(0)==45&&CookRevision(1)==45&&CookRevision(2)==65&&CookRevision(3)==70,
          "card and charms fixes invalidate only their two scene caches");
    const auto broom=temporary.path/"map2.hpvrscene";
    {Writer writer(broom,2,fingerprint);writer.U32(42);writer.Finish();}
    {Reader reader(broom,2,fingerprint);Check(reader.U32()==42,"new broom cache loads");reader.Finish();}
    auto old_broom=ReadBytes(broom);detail::WriteLE(old_broom.data()+12,45,4);
    const auto stale=temporary.path/"old-map2.hpvrscene";WriteBytes(stale,old_broom);
    Rejected([&]{Reader reader(stale,2,fingerprint);},"C55 broom cache must not mask C56 fixes");
    Example expected;Save(good,fingerprint,expected);
    Check(hpvr::quest::kQuestMaps.size()==5&&hpvr::quest::FindQuestMap(0)->package_path=="Maps/Lev_Tut1.unr"&&
          hpvr::quest::FindQuestMap(1)->package_path=="Maps/Lev_Tut1b.unr"&&
          hpvr::quest::FindQuestMap(2)->package_path=="Maps/Lev_Tut2.unr"&&
          hpvr::quest::FindQuestMap(3)->package_path=="Maps/Lev_Tut3.unr"&&
          hpvr::quest::FindQuestMap(4)->package_path=="Maps/Lev_Tut3b.unr","stable five-map descriptors");
    Check(!hpvr::quest::FindQuestMap(5)&&!hpvr::quest::FindQuestMap(std::numeric_limits<unsigned>::max()),
          "unknown map descriptors rejected");
    const auto flying=temporary.path/"map2.hpvc";
    {Writer writer(flying,2,fingerprint);writer.U32(202);writer.Finish();}
    {Reader reader(flying,2,fingerprint);Check(reader.U32()==202,"third-map cache round trip");reader.Finish();}
    Rejected([&]{Reader reader(flying,1,fingerprint);},"flying cache cannot be read as challenge");
    const auto returning=temporary.path/"map4.hpvc";
    {Writer writer(returning,4,fingerprint);writer.U32(404);writer.Finish();}
    {Reader reader(returning,4,fingerprint);Check(reader.U32()==404,"return-map cache round trip");reader.Finish();}
    Rejected([&]{Reader reader(returning,3,fingerprint);},"return-map cache cannot be read as charms");
    Rejected([&]{Writer writer(temporary.path/"unsupported.hpvc",5,fingerprint);},"unknown cache writer map rejected");
    Example loaded;loaded.name="untouched";Check(Load(good,fingerprint,&loaded)&&loaded==expected,"typed stream round trip");
    const auto good_bytes=ReadBytes(good);
    Check(std::string(reinterpret_cast<const char*>(good_bytes.data()),8)=="HPVRSCN1","portable magic");
    Check(detail::ReadLE(good_bytes.data()+8,4)==1&&detail::ReadLE(good_bytes.data()+12,4)==45,"schema and cook revision envelope");
    Check(good_bytes[kHeaderSize]==17&&good_bytes[kHeaderSize+1]==0&&good_bytes[kHeaderSize+4]==175,"scalar little-endian encoding");
    const auto invalid=temporary.path/"invalid.hpvrscene";
    Example sentinel;sentinel.name="live scene remains unchanged";sentinel.unsigned_value=999;
    const auto failed=[&](std::vector<std::uint8_t> bytes,const char* message){
        WriteBytes(invalid,bytes);Example destination=sentinel;
        Check(!Load(invalid,fingerprint,&destination)&&destination==sentinel,message);
    };
    for(const std::size_t offset:{std::size_t(0),std::size_t(8),std::size_t(12),std::size_t(16),std::size_t(20),std::size_t(24),std::size_t(40)}){
        auto bytes=good_bytes;bytes[offset]^=0x40;failed(std::move(bytes),"wrong envelope rejected without live-state mutation");
    }
    auto corrupt=good_bytes;corrupt.back()^=1;failed(corrupt,"payload checksum failure cannot commit candidate");
    auto short_file=good_bytes;short_file.pop_back();failed(short_file,"truncated file rejected");
    short_file.resize(12);failed(short_file,"truncated envelope rejected");
    auto trailing=good_bytes;trailing.push_back(0);failed(trailing,"unexpected trailing file byte rejected");
    auto huge=good_bytes;detail::WriteLE(huge.data()+32,kMaximumPayload+1,8);failed(huge,"oversized payload rejected before allocation");
    Example destination=sentinel;
    Check(!Load(good,fingerprint+1,&destination)&&destination==sentinel,"changed original content rejects stale cache");
    Rejected([&]{Reader reader(good,0,fingerprint);},"wrong level rejected");
    Rejected([&]{Reader reader(good,1,fingerprint);reader.Finish();},"unconsumed payload cannot finish");
    Rejected([&]{Writer writer(good,1,fingerprint);},"existing final never overwritten");
    Check(ReadBytes(good)==good_bytes,"existing final data unchanged");

    const auto oversized_bytes=temporary.path/"oversized-bytes.cache";
    {Writer writer(oversized_bytes,1,fingerprint);writer.U64(std::numeric_limits<std::uint64_t>::max());writer.Finish();}
    Rejected([&]{Reader reader(oversized_bytes,1,fingerprint);(void)reader.Bytes();},"untrusted byte length rejected before allocation");
    const auto oversized_string=temporary.path/"oversized-string.cache";
    {Writer writer(oversized_string,1,fingerprint);writer.U32(std::numeric_limits<std::uint32_t>::max());writer.Finish();}
    Rejected([&]{Reader reader(oversized_string,1,fingerprint);(void)reader.String();},"untrusted string length rejected before allocation");
    const auto invalid_bool=temporary.path/"invalid-bool.cache";
    {Writer writer(invalid_bool,1,fingerprint);const std::uint8_t value=2;writer.Raw(&value,1);writer.Finish();}
    Rejected([&]{Reader reader(invalid_bool,1,fingerprint);(void)reader.Bool();},"invalid boolean rejected");
    Rejected([&]{Reader reader(good,1,fingerprint);reader.CheckCount(1000,1000,44);},"typed record count bounded by bytes remaining");
    Rejected([&]{Reader reader(good,1,fingerprint);reader.CheckCount(1,1000,0);},"zero-sized count divisor rejected");
    const auto aborted=temporary.path/"aborted.cache";
    {Writer writer(aborted,0,fingerprint);writer.U32(123);}
    Check(!std::filesystem::exists(aborted),"uncommitted writer never publishes partial file");NoTemporaries(temporary.path);
    const auto raced=temporary.path/"raced.cache";
    {
        Writer writer(raced,0,fingerprint);writer.U32(234);
        WriteBytes(raced,{7,8,9});Rejected([&]{writer.Finish();},"publication race refuses to clobber winner");
        Check(ReadBytes(raced)==std::vector<std::uint8_t>({7,8,9}),"publication race winner preserved");
    }
    NoTemporaries(temporary.path);
    const auto writer_limit=temporary.path/"writer-limit.cache";
    Rejected([&]{Writer writer(writer_limit,0,fingerprint);writer.String("long",1);},"writer enforces string limit");
    Check(!std::filesystem::exists(writer_limit),"failed writer does not publish");NoTemporaries(temporary.path);

    // Reader and writer chunk boundaries differ deliberately; hashing is
    // streaming and independent of serialization call boundaries.
    const auto streamed=temporary.path/"streamed.cache";
    std::array<std::uint8_t,4096> block{};for(std::size_t i=0;i<block.size();++i)block[i]=static_cast<std::uint8_t>(i&255U);
    {Writer writer(streamed,0,fingerprint);for(unsigned i=0;i<256;++i)writer.Raw(block.data(),block.size());writer.Finish();}
    {Reader reader(streamed,0,fingerprint);std::array<std::uint8_t,257> part{};std::uint64_t offset=0;
        while(reader.Remaining()){
            const auto count=std::min<std::uint64_t>(reader.Remaining(),part.size());reader.Raw(part.data(),count);
            for(std::size_t i=0;i<count;++i)Check(part[i]==static_cast<std::uint8_t>((offset+i)&255U),"stream bytes preserved");offset+=count;
        }reader.Finish();}
    detail::Hash64 full,chunked;full.Update(block.data(),block.size());
    for(std::size_t i=0;i<block.size();++i)chunked.Update(block.data()+i,1);
    Check(full.Finish()==chunked.Finish(),"checksum invariant across single-byte chunks");

    const auto source_a=temporary.path/"source-a",source_b=temporary.path/"source-b";
    std::filesystem::create_directories(source_a/"Maps");std::filesystem::create_directories(source_b/"maps");
    const auto file_a=source_a/"Maps/Lev_Tut1.unr",file_b=source_b/"maps/lev_tut1.unr";
    WriteBytes(file_a,{1,2,3,4,5});WriteBytes(file_b,{1,2,3,4,5});
    const auto time=std::filesystem::last_write_time(file_a);
    std::filesystem::last_write_time(file_b,time-std::chrono::hours(24));
    const auto hash_a=detail::FingerprintFiles(source_a,{file_a},0),hash_b=detail::FingerprintFiles(source_b,{file_b},0);
    Check(hash_a==hash_b,"source fingerprint ignores absolute root casing and transfer timestamps");
    WriteBytes(file_a,{1,2,9,4,5});std::filesystem::last_write_time(file_a,time);
    Check(detail::FingerprintFiles(source_a,{file_a},0)!=hash_a,"same-size same-mtime content mutation invalidates fingerprint");
    Check(detail::FingerprintFiles(source_b,{file_b,file_b},0)==hash_b,"duplicate graph references do not change fingerprint");
    Check(detail::FingerprintFiles(source_b,{file_b},1)!=hash_b,"source fingerprint includes map identity");
    Rejected([&]{(void)detail::FingerprintFiles(source_a,{file_b},0);},"source outside root rejected");
    Check(detail::FingerprintFiles(source_b,{file_b},2)!=hash_b&&
          detail::FingerprintFiles(source_b,{file_b},2)!=detail::FingerprintFiles(source_b,{file_b},1),
          "third-map source identity remains isolated");
    Rejected([&]{(void)ComputeSourceFingerprint(source_a,4);},"unsupported source map rejected");
    Rejected([&]{(void)ComputeSourceFingerprint(source_a,0);},"invalid original dependency graph rejected");
    std::cout<<"ASSET_CACHE_TESTS=PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
