// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#if defined(QLIE_PROFILE_WHJ5)
#include "profiles/whj5.hpp"
namespace game_profile=whj5::profile;
#elif defined(QLIE_PROFILE_WHJ4)
#include "profiles/whj4.hpp"
namespace game_profile=whj4::profile;
#else
#include "profiles/whj2.hpp"
namespace game_profile=whj2::profile;
#endif
