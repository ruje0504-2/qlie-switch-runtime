#pragma once
#include "qlie.hpp"
namespace whj2 {
struct Image {unsigned width=0,height=0;qlie::Bytes pixels;}; // BGRA8 straight alpha
struct Member {std::string type,name;qlie::Bytes data;};
std::vector<Member> abmp(const qlie::Bytes&);
Image image(const qlie::Bytes&);
}
