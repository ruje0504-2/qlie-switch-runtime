// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifndef QLIE_DIAGNOSTICS
#define QLIE_DIAGNOSTICS 0
#endif

#ifdef __cplusplus
#include <cstdio>
namespace qlie { inline constexpr bool diagnostics = QLIE_DIAGNOSTICS != 0; }
#define QLIE_LOG_LINE(parts) do { if constexpr (qlie::diagnostics) std::cerr parts; } while (false)
#define QLIE_LOGF(...) do { if constexpr (qlie::diagnostics) std::fprintf(stderr, __VA_ARGS__); } while (false)
#endif
