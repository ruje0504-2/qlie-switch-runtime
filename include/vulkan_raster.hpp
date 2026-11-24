// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu_raster.hpp"
namespace whj2 {
class VulkanRaster final:public GpuRaster {
 struct Impl;
public:
 VulkanRaster();
};
}
