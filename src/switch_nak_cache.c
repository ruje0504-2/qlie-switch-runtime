// SPDX-License-Identifier: MIT
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

struct nak_compiler;
uint64_t __real_nak_debug_flags(const struct nak_compiler *nak);

// In the locked SDK this external accessor is used only for NVK's pipeline
// UUID and disk-cache key. Rust's internal debug flags occupy bits 0..6.
uint64_t __wrap_nak_debug_flags(const struct nak_compiler *nak)
{
    static atomic_flag reported = ATOMIC_FLAG_INIT;
    if (!atomic_flag_test_and_set_explicit(&reported, memory_order_relaxed))
        fputs("VULKAN compiler=NAK GM20B scoped-interlock-v1\n", stderr);
    return __real_nak_debug_flags(nak) | (UINT64_C(1) << 15);
}
