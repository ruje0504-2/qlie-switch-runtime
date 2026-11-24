// SPDX-License-Identifier: GPL-2.0-or-later
#include "gradient_mask.hpp"
#include <algorithm>
namespace whj2 {
// PE d620c bypasses masking at amount255. c8a34 prepares the threshold;
// c8a80/c8ae8 apply the mask with signed 16-bit MMX multiplication.
uint8_t gradientMaskAlpha(uint8_t sourceAlpha,uint8_t maskBlue,uint8_t amount,uint8_t mode){
 if(amount==255)return sourceAlpha;
 unsigned power=1u<<std::min(unsigned(mode&15),7u),span=256/power;
 int threshold=(255-int(amount))*int(span+255)/255-int(span);
 int value=(mode&128)?255-int(maskBlue):int(maskBlue);
 int product=(value-threshold)*int(power);
 // pmullw wraps before packuswb saturates, including the sharpest edge.
 int wrapped=product&65535;if(wrapped>=32768)wrapped-=65536;
 unsigned coverage=unsigned(std::clamp(wrapped,0,255));
 return uint8_t(unsigned(sourceAlpha)*((coverage>>1)+(coverage>>7))>>7);
}
}
