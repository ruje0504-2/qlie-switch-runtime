// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "image.hpp"
extern "C" {
#include "video.h"
}
#include <stdexcept>

namespace whj2 {
struct VideoSurfaceImage {
 KVideoSurface surface{};
 std::shared_ptr<AVFrame> frame;
 using Reader = int (*)(const AVFrame*,KImage*,char*,size_t);
 Reader readFrame=nullptr;
 std::string source;
 bool sepia=false;
 std::shared_ptr<const qlie::Bytes> encoded;
 uint64_t tick=0;
 mutable qlie::Bytes bgra;

 const qlie::Bytes& pixels()const{
  if(!bgra.empty())return bgra;
  qlie::Bytes output(size_t(surface.width)*surface.height*4);
  KImage target{0,0,surface.width,surface.height,size_t(surface.width)*4,output.data()};
  char error[160]{};
  if(readFrame(frame.get(),&target,error,sizeof(error))<0&&
     (!encoded||kvideo_read_software_picture(encoded->data(),encoded->size(),tick,&target)<0))
   throw std::runtime_error("Embedded video "+source+": "+error);
  if(sepia)applySepia(output);
  bgra=std::move(output);
  return bgra;
 }
};
}
