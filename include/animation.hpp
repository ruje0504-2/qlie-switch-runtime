#pragma once
#include "qlie.hpp"
#include <array>
namespace whj2 {
struct AnimationLayer {std::string name;unsigned version;uint8_t flags,mode;std::vector<qlie::Bytes> keys;};
struct Motion {std::string name;std::vector<AnimationLayer> layers;};
struct Animation {std::string name,comment;std::array<int32_t,4> bounds;uint32_t flags=0;std::vector<Motion> motions;};
Animation animation(const std::string&version,const qlie::Bytes&);
}
