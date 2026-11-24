// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "image.hpp"
namespace whj2 {
class RenderTarget;
class FpsOverlay {
 bool started=false;uint64_t sampleStart=0,frames=0;
 unsigned value=0,renderedValue=~0u;Image label;
 void rebuild();
public:
 void presented(uint64_t milliseconds);
 unsigned fps()const{return value;}
 void draw(RenderTarget&);
 void draw(qlie::Bytes&);
};
}
