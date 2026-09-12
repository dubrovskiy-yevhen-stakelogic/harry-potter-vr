#pragma once
#include "hpvr/hp1_package_graph.h"
#include "hpvr/quest_maps.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <share.h>
#else
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace hpvr::quest::cache {
inline constexpr std::uint32_t kSchema=1,kCookRevision=45;
inline constexpr std::uint32_t CookRevision(std::uint32_t map){return map==2?56:kCookRevision;}
inline constexpr std::uint64_t kMaximumPayload=1024ULL*1024ULL*1024ULL;
inline constexpr std::uint32_t kMaximumString=1024U*1024U;
inline constexpr std::size_t kHeaderSize=48;
using SourceFingerprint=std::uint64_t;
class CacheError : public std::runtime_error {public:using std::runtime_error::runtime_error;};

namespace detail {
inline void Require(bool ok,const char* message){if(!ok)throw CacheError(message);}
inline std::string Fold(std::string text){for(char& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return text;}
inline std::uint64_t ReadLE(const std::uint8_t* bytes,unsigned count){
    std::uint64_t result=0;for(unsigned i=0;i<count;++i)result|=std::uint64_t(bytes[i])<<(i*8U);return result;
}
inline void WriteLE(std::uint8_t* bytes,std::uint64_t value,unsigned count){
    for(unsigned i=0;i<count;++i){bytes[i]=static_cast<std::uint8_t>(value&255U);value>>=8U;}
}
// Stable streaming 64-bit checksum for accidental corruption, not authenticity.
// Eight-byte mixing keeps large cooked vertex/texture streams inexpensive.
class Hash64 {
    std::uint64_t state_=0x9e3779b185ebca87ULL,length_=0,tail_=0;
    unsigned tail_bytes_=0;
    static std::uint64_t Mix(std::uint64_t value){
        value^=value>>30U;value*=0xbf58476d1ce4e5b9ULL;
        value^=value>>27U;value*=0x94d049bb133111ebULL;return value^(value>>31U);
    }
    void Word(std::uint64_t word){state_=std::rotl(state_^Mix(word),27)*0x9e3779b185ebca87ULL+0x52dce729ULL;}
public:
    void Update(const void* data,std::size_t size){
        const auto* bytes=static_cast<const std::uint8_t*>(data);length_+=size;
        while(tail_bytes_&&size){
            tail_|=std::uint64_t(*bytes++)<<(tail_bytes_*8U);++tail_bytes_;--size;
            if(tail_bytes_==8){Word(tail_);tail_=0;tail_bytes_=0;}
        }
        while(size>=8){Word(ReadLE(bytes,8));bytes+=8;size-=8;}
        while(size){tail_|=std::uint64_t(*bytes++)<<(tail_bytes_*8U);++tail_bytes_;--size;}
    }
    void Integer(std::uint64_t value){std::array<std::uint8_t,8> bytes{};WriteLE(bytes.data(),value,8);Update(bytes.data(),bytes.size());}
    [[nodiscard]] std::uint64_t Finish() const {return Mix(state_^Mix(tail_^(std::uint64_t(tail_bytes_)<<56U))^Mix(length_));}
};
struct CloseFile {void operator()(FILE* file)const{if(file)std::fclose(file);}};
using File=std::unique_ptr<FILE,CloseFile>;
inline File OpenRead(const std::filesystem::path& path){
#ifdef _WIN32
    FILE* raw=nullptr;const auto error=_wfopen_s(&raw,path.c_str(),L"rb");
    Require(error==0&&raw,"Could not open cooked scene");return File(raw);
#else
    File file(std::fopen(path.c_str(),"rb"));Require(bool(file),"Could not open cooked scene");return file;
#endif
}
inline File OpenExclusive(const std::filesystem::path& path){
#ifdef _WIN32
    int descriptor=-1;
    if(_wsopen_s(&descriptor,path.c_str(),_O_CREAT|_O_EXCL|_O_BINARY|_O_RDWR,_SH_DENYRW,_S_IREAD|_S_IWRITE)!=0)return {};
    File file(_fdopen(descriptor,"w+b"));if(!file)_close(descriptor);return file;
#else
    const int descriptor=::open(path.c_str(),O_CREAT|O_EXCL|O_RDWR,0600);
    if(descriptor<0)return {};
    File file(fdopen(descriptor,"w+b"));if(!file)::close(descriptor);return file;
#endif
}
inline std::uint64_t ProcessId(){
#ifdef _WIN32
    return static_cast<std::uint64_t>(_getpid());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}
inline void DurableFile(FILE* file){
    Require(std::fflush(file)==0,"Could not flush cooked scene");
#ifdef _WIN32
    Require(_commit(_fileno(file))==0,"Could not sync cooked scene");
#else
    Require(fsync(fileno(file))==0,"Could not sync cooked scene");
#endif
}
inline void PublishNoReplace(const std::filesystem::path& temporary,const std::filesystem::path& final){
#ifdef _WIN32
    // The Windows CRT rename fails if the destination exists.
    Require(_wrename(temporary.c_str(),final.c_str())==0,"Could not publish cooked scene without replacing an existing file");
#else
#if defined(__linux__) && defined(SYS_renameat2)
    if(syscall(SYS_renameat2,AT_FDCWD,temporary.c_str(),AT_FDCWD,final.c_str(),1U)==0)return;
    if(errno!=ENOSYS&&errno!=EINVAL)throw CacheError("Could not publish cooked scene without replacing an existing file");
#endif
    // Portable no-clobber fallback: publish one hard link atomically, then
    // remove the private temporary name. Never fall back to replacing rename.
    Require(::link(temporary.c_str(),final.c_str())==0,"Filesystem cannot atomically publish a new cooked scene");
    (void)::unlink(temporary.c_str());
#endif
}
inline std::array<std::uint8_t,kHeaderSize> Header(std::uint32_t map,SourceFingerprint fingerprint,std::uint64_t size,std::uint64_t checksum){
    std::array<std::uint8_t,kHeaderSize> bytes{};
    constexpr char magic[]="HPVRSCN1";std::memcpy(bytes.data(),magic,8);
    WriteLE(bytes.data()+8,kSchema,4);WriteLE(bytes.data()+12,CookRevision(map),4);WriteLE(bytes.data()+16,map,4);
    WriteLE(bytes.data()+24,fingerprint,8);WriteLE(bytes.data()+32,size,8);WriteLE(bytes.data()+40,checksum,8);return bytes;
}
inline std::string RelativeKey(const std::filesystem::path& root,const std::filesystem::path& file){
    const auto canonical_root=std::filesystem::canonical(root),canonical_file=std::filesystem::canonical(file);
    const auto relative=canonical_file.lexically_relative(canonical_root);
    Require(!relative.empty()&&!relative.is_absolute(),"Cache source must be within game data root");
    for(const auto& component:relative)Require(component!="..","Cache source escapes game data root");
    return Fold(relative.generic_string());
}
inline std::filesystem::path ResolveRelative(const std::filesystem::path& root,std::string_view relative){
    auto path=root;
    for(const auto& component:std::filesystem::path(relative)){
        Require(component!=".."&&!component.is_absolute(),"Invalid relative cache source");
        std::filesystem::path found;
        for(const auto& entry:std::filesystem::directory_iterator(path))
            if(Fold(entry.path().filename().string())==Fold(component.string())){
                Require(found.empty(),"Ambiguous case-insensitive cache source");found=entry.path();
            }
        Require(!found.empty(),"Required original cache source is missing");path=std::move(found);
    }
    return path;
}
// Content, relative identity and length are hashed; timestamps and absolute
// installation paths deliberately are not. This survives PC-to-Quest copies.
inline SourceFingerprint FingerprintFiles(const std::filesystem::path& root,const std::vector<std::filesystem::path>& files,std::uint32_t map){
    Require(IsSupportedQuestMap(map),"Unsupported cooked scene map");
    std::map<std::string,std::filesystem::path> ordered;
    for(const auto& file:files){
        const auto key=RelativeKey(root,file);
        if(const auto previous=ordered.find(key);previous!=ordered.end())
            Require(std::filesystem::equivalent(previous->second,file),"Conflicting case-insensitive cache sources");
        else ordered.emplace(key,file);
    }
    Hash64 hash;constexpr char domain[]="HPVR-SOURCE-CONTENT-1";hash.Update(domain,sizeof(domain)-1);
    hash.Integer(map);hash.Integer(ordered.size());
    std::array<std::uint8_t,256U*1024U> buffer{};
    for(const auto& [key,path]:ordered){
        const auto size=std::filesystem::file_size(path);
        Require(size<=2ULL*kMaximumPayload,"Original package exceeds cache fingerprint safety limit");
        hash.Integer(key.size());hash.Update(key.data(),key.size());hash.Integer(size);
        auto file=OpenRead(path);std::uint64_t remaining=size;
        while(remaining){
            const auto chunk=static_cast<std::size_t>(std::min<std::uint64_t>(remaining,buffer.size()));
            Require(std::fread(buffer.data(),1,chunk,file.get())==chunk,"Original package changed or became unreadable during fingerprinting");
            hash.Update(buffer.data(),chunk);remaining-=chunk;
        }
        Require(std::fgetc(file.get())==EOF&&!std::ferror(file.get()),"Original package grew during fingerprinting");
    }
    return hash.Finish();
}
} // namespace detail

class Writer {
    std::filesystem::path final_,temporary_;
    detail::File file_;
    std::uint32_t map_;
    SourceFingerprint fingerprint_;
    std::uint64_t written_=0;
    detail::Hash64 checksum_;
    bool finished_=false;
    void Write(const void* data,std::uint64_t size){
        detail::Require(!finished_&&bool(file_),"Cooked scene writer is closed");
        detail::Require(size<=kMaximumPayload-written_,"Cooked scene payload exceeds 1 GiB");
        detail::Require(size==0||data!=nullptr,"Null cooked scene write buffer");
        if(size){const auto count=static_cast<std::size_t>(size);detail::Require(std::fwrite(data,1,count,file_.get())==count,"Could not write cooked scene payload");checksum_.Update(data,count);}
        written_+=size;
    }
    void Integer(std::uint64_t value,unsigned bytes){std::array<std::uint8_t,8> encoded{};detail::WriteLE(encoded.data(),value,bytes);Write(encoded.data(),bytes);}
public:
    Writer(const std::filesystem::path& final,std::uint32_t map,SourceFingerprint fingerprint):final_(final),map_(map),fingerprint_(fingerprint){
        detail::Require(IsSupportedQuestMap(map),"Unsupported cooked scene map");
        detail::Require(!final.filename().empty(),"Cooked scene path has no filename");
        std::error_code filesystem_error;
        if(!final.parent_path().empty())std::filesystem::create_directories(final.parent_path(),filesystem_error);
        detail::Require(!filesystem_error,"Could not create cooked scene directory");
        const bool exists=std::filesystem::exists(final,filesystem_error);
        detail::Require(!filesystem_error,"Could not inspect cooked scene destination");
        detail::Require(!exists,"Cooked scene already exists; refusing to replace it");
        static std::atomic<std::uint64_t> sequence{0};
        for(unsigned attempt=0;attempt<16&&!file_;++attempt){
            temporary_=final;temporary_+=".tmp-"+std::to_string(detail::ProcessId())+"-"+
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1));
            file_=detail::OpenExclusive(temporary_);
        }
        detail::Require(bool(file_),"Could not create private cooked scene temporary file");
        const auto header=detail::Header(map,fingerprint,0,0);
        if(std::fwrite(header.data(),1,header.size(),file_.get())!=header.size()){
            file_.reset();std::error_code ignored;std::filesystem::remove(temporary_,ignored);throw CacheError("Could not write cooked scene envelope");
        }
    }
    Writer(const Writer&)=delete;Writer& operator=(const Writer&)=delete;
    ~Writer(){file_.reset();if(!finished_&&!temporary_.empty()){std::error_code ignored;std::filesystem::remove(temporary_,ignored);}}
    void U32(std::uint32_t value){Integer(value,4);}
    void I32(std::int32_t value){U32(std::bit_cast<std::uint32_t>(value));}
    void U64(std::uint64_t value){Integer(value,8);}
    void F32(float value){detail::Require(std::isfinite(value),"Non-finite cooked scene float");U32(std::bit_cast<std::uint32_t>(value));}
    void Bool(bool value){const std::uint8_t byte=value?1U:0U;Write(&byte,1);}
    void String(std::string_view value,std::uint32_t maximum=kMaximumString){
        detail::Require(value.size()<=maximum&&value.size()<=std::numeric_limits<std::uint32_t>::max(),"Cooked scene string exceeds limit");
        U32(static_cast<std::uint32_t>(value.size()));Write(value.data(),value.size());
    }
    void Bytes(const std::vector<std::uint8_t>& value,std::uint64_t maximum=kMaximumPayload){
        detail::Require(value.size()<=maximum,"Cooked scene byte vector exceeds limit");U64(value.size());Write(value.data(),value.size());
    }
    // Caller must separately assert the exact trivial fixed-width record
    // layout. Raw is never appropriate for pointers, strings or STL objects.
    void Raw(const void* data,std::uint64_t size){detail::Require(std::endian::native==std::endian::little,"Raw cache records require little-endian host");Write(data,size);}
    [[nodiscard]] std::uint64_t Written()const{return written_;}
    void Finish(){
        detail::Require(!finished_&&bool(file_),"Cooked scene writer is closed");
        detail::Require(std::fseek(file_.get(),0,SEEK_SET)==0,"Could not seek cooked scene envelope");
        const auto header=detail::Header(map_,fingerprint_,written_,checksum_.Finish());
        detail::Require(std::fwrite(header.data(),1,header.size(),file_.get())==header.size(),"Could not finalize cooked scene envelope");
        detail::DurableFile(file_.get());detail::Require(std::fclose(file_.release())==0,"Could not close cooked scene");
        detail::PublishNoReplace(temporary_,final_);finished_=true;
    }
};

class Reader {
    detail::File file_;
    std::uint64_t remaining_=0,expected_checksum_=0;
    detail::Hash64 checksum_;
    bool finished_=false;
    void Read(void* data,std::uint64_t size){
        detail::Require(!finished_,"Cooked scene reader is finished");
        detail::Require(size<=remaining_,"Cooked scene payload is truncated or its count exceeds remaining bytes");
        detail::Require(size==0||data!=nullptr,"Null cooked scene read buffer");
        if(size){const auto count=static_cast<std::size_t>(size);detail::Require(std::fread(data,1,count,file_.get())==count,"Cooked scene payload read failed");checksum_.Update(data,count);}
        remaining_-=size;
    }
    std::uint64_t Integer(unsigned size){std::array<std::uint8_t,8> bytes{};Read(bytes.data(),size);return detail::ReadLE(bytes.data(),size);}
public:
    Reader(const std::filesystem::path& path,std::uint32_t map,SourceFingerprint fingerprint):file_(detail::OpenRead(path)){
        detail::Require(IsSupportedQuestMap(map),"Unsupported cooked scene map");
        std::error_code filesystem_error;const auto size=std::filesystem::file_size(path,filesystem_error);
        detail::Require(!filesystem_error&&size>=kHeaderSize&&size<=kHeaderSize+kMaximumPayload,"Cooked scene file size is invalid");
        std::array<std::uint8_t,kHeaderSize> header{};detail::Require(std::fread(header.data(),1,header.size(),file_.get())==header.size(),"Cooked scene envelope is truncated");
        detail::Require(std::memcmp(header.data(),"HPVRSCN1",8)==0,"Cooked scene magic mismatch");
        detail::Require(detail::ReadLE(header.data()+8,4)==kSchema,"Cooked scene schema mismatch");
        detail::Require(detail::ReadLE(header.data()+12,4)==CookRevision(map),"Cooked scene cook revision mismatch");
        detail::Require(detail::ReadLE(header.data()+16,4)==map&&detail::ReadLE(header.data()+20,4)==0,"Cooked scene map or reserved field mismatch");
        detail::Require(detail::ReadLE(header.data()+24,8)==fingerprint,"Cooked scene does not match original game content");
        remaining_=detail::ReadLE(header.data()+32,8);expected_checksum_=detail::ReadLE(header.data()+40,8);
        detail::Require(remaining_<=kMaximumPayload&&remaining_==size-kHeaderSize,"Cooked scene payload size mismatch");
    }
    Reader(const Reader&)=delete;Reader& operator=(const Reader&)=delete;
    std::uint32_t U32(){return static_cast<std::uint32_t>(Integer(4));}
    std::int32_t I32(){return std::bit_cast<std::int32_t>(U32());}
    std::uint64_t U64(){return Integer(8);}
    float F32(){const float value=std::bit_cast<float>(U32());detail::Require(std::isfinite(value),"Non-finite cooked scene float");return value;}
    bool Bool(){const auto value=Integer(1);detail::Require(value<=1,"Invalid cooked scene boolean");return value!=0;}
    [[nodiscard]] std::uint64_t Remaining()const{return remaining_;}
    void CheckCount(std::uint64_t count,std::uint64_t maximum,std::uint64_t minimum_wire_bytes=1)const{
        detail::Require(minimum_wire_bytes>0&&count<=maximum&&count<=remaining_/minimum_wire_bytes,"Cooked scene element count exceeds allocation or byte budget");
    }
    std::string String(std::uint32_t maximum=kMaximumString){const auto count=U32();CheckCount(count,maximum);std::string text(count,'\0');Read(text.data(),count);return text;}
    std::vector<std::uint8_t> Bytes(std::uint64_t maximum=kMaximumPayload){const auto count=U64();CheckCount(count,maximum);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count));Read(bytes.data(),count);return bytes;}
    void Raw(void* data,std::uint64_t size){detail::Require(std::endian::native==std::endian::little,"Raw cache records require little-endian host");Read(data,size);}
    // A serializer must decode into an isolated candidate and publish it only
    // after Finish succeeds. Corrupt input can never commit partial live state.
    void Finish(){
        detail::Require(!finished_&&remaining_==0,"Cooked scene was not completely consumed");
        detail::Require(checksum_.Finish()==expected_checksum_,"Cooked scene checksum mismatch");
        detail::Require(std::fgetc(file_.get())==EOF&&!std::ferror(file_.get()),"Cooked scene has trailing or unreadable bytes");finished_=true;
    }
};

inline SourceFingerprint ComputeSourceFingerprint(const std::filesystem::path& root,std::uint32_t map){
    try{
        const auto* descriptor=FindQuestMap(map);
        detail::Require(descriptor!=nullptr,"Unsupported cooked scene map");
        std::vector<std::filesystem::path> sources;
        const auto include_graph=[&](std::string_view relative){
            const auto entry=detail::ResolveRelative(root,relative);
            const auto graph=wand::resolve_hp1_package_graph(root,entry);
            if(graph.status!=wand::Hp1PackageGraphStatus::ok)throw CacheError("Could not fingerprint original dependency graph: "+graph.error);
            for(const auto& package:graph.packages)if(package.kind==wand::Hp1ResolvedPackageKind::data_package)sources.push_back(package.path);
        };
        include_graph("Maps/Lev_Tut1.unr");
        if(map!=kIntroductionMapId)include_graph(descriptor->package_path);
        // These are loaded by name rather than by the level's import table.
        // Derived PCM, cooked Cache files and player saves are not inputs.
        for(const auto name:{"system/HPMenu.u","system/HPBase.u","system/HPParticle.u","system/HProps.u","system/HPSounds.u",
                            "Textures/MenuArt.utx","Textures/StoryBookTest.utx","system/hpmenu.int","system/hpdialog.int"})
            sources.push_back(detail::ResolveRelative(root,name));
        return detail::FingerprintFiles(root,sources,map);
    }catch(const CacheError&){throw;}catch(const std::exception& error){throw CacheError(error.what());}
}
} // namespace hpvr::quest::cache
