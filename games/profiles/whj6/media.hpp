// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "qlie.hpp"
#include <cstring>
#include <utility>

namespace whj6::media {
// VSD1 carries a small game-local header before the Matroska stream.
// The scene VM and animation keys still refer to the original Ogg dimensions.
inline std::pair<unsigned,unsigned> authoredSize(const qlie::Bytes& bytes){
 constexpr unsigned headerSize=20;
 if(bytes.size()<headerSize||std::memcmp(bytes.data(),"VSD1\x0c\0\0\0W6CG",12)!=0)return {};
 const auto dimension=[&](unsigned offset){
  return unsigned(bytes[offset])|(unsigned(bytes[offset+1])<<8)|
         (unsigned(bytes[offset+2])<<16)|(unsigned(bytes[offset+3])<<24);
 };
 return {dimension(12),dimension(16)};
}
}
