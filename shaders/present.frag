// SPDX-License-Identifier: GPL-2.0-or-later
#version 450
layout(set=0,binding=1,std430) readonly buffer Pixels{uint pixels[];};
layout(push_constant) uniform Params{vec4 p0,p1,p2,p3;ivec4 rect;uvec4 meta,colors;} p;
layout(location=0) out vec4 color;
void main(){uvec2 xy=min(uvec2(gl_FragCoord.xy*vec2(1280,720)/vec2(p.meta.xy)),uvec2(1279,719));uint c=pixels[xy.y*1280+xy.x];color=vec4((c>>16)&255u,(c>>8)&255u,c&255u,c>>24)/255.0;}
