// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#if defined(QLIE_PROFILE_WHJ5)
#include "profiles/whj5.hpp"
namespace game_profile=whj5::profile;
#elif defined(QLIE_PROFILE_WHJ3)
#include "profiles/whj3.hpp"
namespace game_profile=whj3::profile;
#elif defined(QLIE_PROFILE_WHJ25)
#include "profiles/whj25.hpp"
namespace game_profile=whj25::profile;
#elif defined(QLIE_PROFILE_WHJ4)
#include "profiles/whj4.hpp"
namespace game_profile=whj4::profile;
#else
#include "profiles/whj2.hpp"
namespace game_profile=whj2::profile;
#endif
