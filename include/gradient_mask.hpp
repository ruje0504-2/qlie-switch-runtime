// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace whj2 {
// Blue-channel threshold mask used by QLIE draw type 3. The high mode
// bit inverts the mask; the low nibble selects threshold sharpness.
uint8_t gradientMaskAlpha(uint8_t sourceAlpha,uint8_t maskBlue,uint8_t amount,uint8_t mode);
}
