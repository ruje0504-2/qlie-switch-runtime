// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"

namespace whj2::profile {
inline const qlie::ArchiveLayout archives{
 qlie::PackVersion::v30,16,"GameData/data","DLL/key.fkey","DLL/icon.key",
 "美少女万華鏡２.exe","voice/"
};
inline const StoryProfile story{
 "scenario/root.s","scenario/select.s","include",
 "avg/header.s","avg/header_replay.s","avg/footer.s","WHJ2-STORY-2",
 {"sentence","format","message","face","bg01","bg02","bg03","bg04",
  "chara01","chara02","chara03","chara04","effect","env",
  "music01","se01","se02","savefilter","movie"}
};
inline constexpr char gameName[] = "美少女万華鏡２";
inline constexpr char sdRoot[] = "sdmc:/switch/whj2";
inline constexpr char sdGame[] = "sdmc:/switch/whj2/game";
inline constexpr char sdSaves[] = "sdmc:/switch/whj2/saves";
inline constexpr char sdLog[] = "sdmc:/switch/whj2/runtime.log";
inline constexpr char sdRenderer[] = "sdmc:/switch/whj2/renderer.cfg";
}
