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
  "music01","se01","se02","savefilter","movie"},
 "cut/eyecatch01",
 // Original Japanese sentence lines for the cloud/mask, volleyball and flash sequences.
 {{"scenario/本編/w01_02.s",{630,634}},
  {"scenario/本編/w10_03.s",{19,31,43,53,60}},
  {"scenario/本編/w16_05a.s",{905,912}}}
};
inline constexpr char gameName[] = "美少女万華鏡２";
inline constexpr char sdRoot[] = "sdmc:/switch/whj2";
inline constexpr char sdGame[] = "sdmc:/switch/whj2/game";
inline constexpr char sdSaves[] = "sdmc:/switch/whj2/saves";
inline constexpr char sdRenderer[] = "sdmc:/switch/whj2/renderer.cfg";
}
