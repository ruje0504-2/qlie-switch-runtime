// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace whj2 {
struct ParticleParameters {
 float count=0,edge=0,divisions=1,scaleOffset=0;
 std::array<float,3> phase{},extent{};
 int32_t timeBegin=0,timeEnd=0;
};
struct ParticlePoint {
 float x=0,y=0,z=0,scale=1;
 unsigned alpha=0,motion=0;
 int32_t milliseconds=0;
};
uint32_t particleRandomNext(uint32_t& state);
uint32_t particleIdentityInitialState();
struct ParticleProjection {float x,y,scale,depth;bool visible;};
ParticleProjection projectParticle(const ParticlePoint&,const ParticleParameters&,float x,float y,float anchorX,float anchorY,float angle,unsigned width,unsigned height,unsigned textureWidth=0,unsigned textureHeight=0);
// PE a118c/a1760 and RTL 2b7c. The original default is fixed, not wall time.
std::array<uint32_t,1024> particleRandomTable(uint32_t seed=0x3f1754e7);
// Local particle coordinates before the parent's camera/projection transform.
// The caller supplies the evaluated layer seed (ec78c/ec7a2).
std::vector<ParticlePoint> particlePoints(const ParticleParameters&,uint32_t layerSeed,unsigned motionCount=1);
}
