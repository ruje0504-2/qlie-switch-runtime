#pragma once
#include "qlie.hpp"
#include <atomic>
#include <utility>
namespace whj2 {
// A producer publishes a version after completing all writes. An editable
// copy starts untracked; a move transfers the version with the pixel buffer.
class PixelVersion {
 uint64_t serial=0;
public:
 PixelVersion()=default;
 PixelVersion(const PixelVersion&){}
 PixelVersion& operator=(const PixelVersion&){serial=0;return *this;}
 PixelVersion(PixelVersion&&v)noexcept:serial(std::exchange(v.serial,0)){}
 PixelVersion& operator=(PixelVersion&&v)noexcept{serial=std::exchange(v.serial,0);return *this;}
 uint64_t get()const{return serial;}
 void publish(){static std::atomic<uint64_t>next{1};serial=next.fetch_add(1,std::memory_order_relaxed);}
};
struct Image {unsigned width=0,height=0;qlie::Bytes pixels;PixelVersion version{};}; // BGRA8 straight alpha
struct Member {std::string type,name;qlie::Bytes data;};
std::vector<Member> abmp(const qlie::Bytes&);
Image image(const qlie::Bytes&);
Image filterImage(const Image&,const std::string&);
}
