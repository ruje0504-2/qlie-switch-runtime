// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profiles/settings_layout.hpp"
#if defined(QLIE_PROFILE_WHJ1)
#include "profiles/whj1.hpp"
namespace game_profile=whj1::profile;
inline constexpr auto& gameSettingsLayout=whj1::profile::settingsLayout;
#elif defined(QLIE_PROFILE_WHJ6)
#include "profiles/whj6.hpp"
namespace game_profile=whj6::profile;
#elif defined(QLIE_PROFILE_WHJ5)
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
#if !defined(QLIE_PROFILE_WHJ1)
inline constexpr auto& gameSettingsLayout=whj2::standardSettingsLayout;
#endif
#if defined(QLIE_PROFILE_WHJ6)
inline constexpr auto gameSceneSize=game_profile::sceneSize;
#else
inline constexpr std::array<unsigned,2> gameSceneSize{1280,720};
#endif
