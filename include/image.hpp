#pragma once
#include "qlie.hpp"
#include <atomic>
#include <memory>
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
struct VideoSurfaceImage;
struct ImagePatches;
struct Image {
 unsigned width=0,height=0;
 qlie::Bytes pixels; // BGRA8 straight alpha
 PixelVersion version{};
 std::shared_ptr<const VideoSurfaceImage> videoSurface{};
 std::shared_ptr<const ImagePatches> patches{};
};
// Optional nonoverlapping regions for pixel-aligned GPU draws. Their versions
// are independent, and the stamp prevents editable image copies using stale regions.
struct ImagePatch {unsigned x=0,y=0;Image image;};
struct ImagePatches {uint64_t sourceVersion=0;std::vector<ImagePatch> regions;};
struct Member {std::string type,name;qlie::Bytes data;size_t dataOffset=0,storedSize=0;};
std::vector<Member> abmp(const qlie::Bytes&);
Image image(const qlie::Bytes&);
void applySepia(qlie::Bytes&);
Image filterImage(const Image&,const std::string&);
}
