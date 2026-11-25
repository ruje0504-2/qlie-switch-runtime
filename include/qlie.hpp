#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <optional>
#include <mutex>
namespace qlie {
using Bytes=std::vector<uint8_t>;
uint32_t hash(const Bytes&);
Bytes decompress(const Bytes&);
std::string cp932(const Bytes&);
std::string utf16le(const Bytes&,size_t offset=0);
std::string canonical(std::string);
enum class PackVersion { v30, v31 };
struct ArchiveLayout {
 PackVersion version;
 unsigned slotCount;
 // An empty fileKey selects the key embedded in each 3.1 archive.
 std::string archivePrefix, fileKey, iconKey, hostExecutable, looseVoicePrefix, patchFile{};
 std::map<std::string,std::string> resourceAliases{};
};
struct Entry { std::string name,rawname;uint64_t offset;uint32_t size,unpacked,packed,encrypted,hash; };
class Pack {
 std::mutex fileCursor;
 std::ifstream fp;Bytes k1,k2,key_entry;bool key_ready=false;uint32_t key;
 PackVersion version;
public:
 std::vector<Entry> entries;
 Pack(const std::string&,const Bytes&,const Bytes&,PackVersion expected=PackVersion::v30,
      bool useEmbeddedFileKey=false);
 uint32_t checksum(const Bytes&) const;
 Bytes read(const Entry&);
};
struct ResourceLocation {std::string name,path;uint64_t offset=0,size=0;bool loose=false;};
class Assets {
 std::string root,looseVoicePrefix,patchPath;
 std::map<std::string,std::string> resourceAliases;
 std::vector<std::unique_ptr<Pack>> packs;
 std::map<std::string,std::pair<Pack*,Entry>> index;
 std::map<std::string,ResourceLocation> sources;
 std::string alternateName(const std::string&) const;
public:
 Assets(const std::string&,const ArchiveLayout&);
 Bytes read(const std::string&);
 std::string text(const std::string&);
 bool exists(const std::string&) const;
 bool inPack(const std::string&) const;
 std::optional<ResourceLocation> locate(const std::string&) const;
 std::vector<std::string> names() const;
};
std::pair<Bytes,Bytes> game_keys(const std::string&,const ArchiveLayout&);
Bytes file(const std::string&);
void write_uncommitted_file(const std::string&,const Bytes&);
void write_file(const std::string&,const Bytes&);
}
