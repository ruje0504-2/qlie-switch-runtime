#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <optional>
namespace qlie {
using Bytes=std::vector<uint8_t>;
uint32_t hash(const Bytes&);
Bytes decompress(const Bytes&);
std::string cp932(const Bytes&);
std::string canonical(std::string);
struct Entry { std::string name,rawname;uint64_t offset;uint32_t size,unpacked,packed,encrypted,hash; };
class Pack {
 std::ifstream fp;Bytes k1,k2,key_entry;bool key_ready=false;uint32_t key;
public:
 std::vector<Entry> entries;
 Pack(const std::string&,const Bytes&,const Bytes&);
 Bytes read(const Entry&);
};
struct ResourceLocation {std::string name,path;uint64_t offset=0,size=0;bool loose=false;};
class Assets {
 std::string root;
 std::vector<std::unique_ptr<Pack>> packs;
 std::map<std::string,std::pair<Pack*,Entry>> index;
 std::map<std::string,ResourceLocation> sources;
public:
 explicit Assets(const std::string&);
 Bytes read(const std::string&);
 bool exists(const std::string&) const;
 std::optional<ResourceLocation> locate(const std::string&) const;
 std::vector<std::string> names() const;
};
std::pair<Bytes,Bytes> game_keys(const std::string&);
Bytes file(const std::string&);
void write_file(const std::string&,const Bytes&);
}
