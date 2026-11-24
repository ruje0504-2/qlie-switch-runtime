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
 // Verified command-only presentation boundaries in the original Japanese scripts.
 {{"scenario/本編/w00_op_01.s",{445,507}},
  {"scenario/本編/w01_01.s",{203,218,326,717,756,1115,1125,1127,1179,1261,1510,1529,1764,1847,2105}},
  {"scenario/本編/w01_02.s",{41,502,630,634,703,851,905}},
  {"scenario/本編/w02_01.s",{231,366,975,1136,1499,1657}},
  {"scenario/本編/w03_01.s",{145,476,503,708,710,939}},
  {"scenario/本編/w03_03.s",{207,299,379,388,493}},
  {"scenario/本編/w04_01.s",{13,16,299}},
  {"scenario/本編/w04_03.s",{193}},
  {"scenario/本編/w05_01.s",{351,654,662,666,670,1023,1146,1247,1396,1518}},
  {"scenario/本編/w06_01.s",{178,585}},
  {"scenario/本編/w06_03.s",{157}},
  {"scenario/本編/w07_01.s",{95,218,278,281,350}},
  {"scenario/本編/w07_03.s",{81,87,90,113}},
  {"scenario/本編/w08_01.s",{114,120,124}},
  {"scenario/本編/w08_03.s",{118,264}},
  {"scenario/本編/w09_01.s",{121,129,133,196}},
  {"scenario/本編/w10_01.s",{76,177,203,474,480,544}},
  {"scenario/本編/w10_03.s",{19,31,43,53,60,111,300,338,342,382,498}},
  {"scenario/本編/w11_01.s",{212,215,246,313,440}},
  {"scenario/本編/w12_01.s",{305}},
  {"scenario/本編/w12_03.s",{8,13,83,148}},
  {"scenario/本編/w13_03.s",{53,407,412,628}},
  {"scenario/本編/w14_01.s",{98,101}},
  {"scenario/本編/w14_03.s",{337,538}},
  {"scenario/本編/w14_04.s",{453,458,796,872}},
  {"scenario/本編/w14_06.s",{219,225,320,326,440}},
  {"scenario/本編/w15_01.s",{97,111,589}},
  {"scenario/本編/w15_02a.s",{13,17,44,168}},
  {"scenario/本編/w15_04a.s",{13,16,155,161,166,171,176,181,254,260,374}},
  {"scenario/本編/w15_04b.s",{125,159,163,192,329,334,339,344,439,441,446,567}},
  {"scenario/本編/w15_05b.s",{141,145,185,323,328,333,427,429,434,555}},
  {"scenario/本編/w16_01.s",{109,112,432,607,616,621,624,1040,1045,1048,1053,1058,1454}},
  {"scenario/本編/w16_01b.s",{111,114,434,609,618,623,626,1042,1047,1050,1055,1060,1485,1543,1576,1762,1769}},
  {"scenario/本編/w16_02a.s",{71,74,132,136,215,219,267,271,286,373,380,441,444}},
  {"scenario/本編/w16_02b.s",{71,130,134,213,217,265,280,284,313,405,409,501,508,569,572}},
  {"scenario/本編/w16_04.s",{287,291,293}},
  {"scenario/本編/w16_05a.s",{244,247,634,643,784,905,912,1129,1139,1141}},
  {"scenario/本編/w16_05b.s",{278,287,290}},
  {"scenario/本編/w17_01a.s",{362,366,619,623}},
  {"scenario/本編/w17_01b.s",{212}},
  {"scenario/本編/w17_01c.s",{12,15,197,201}},
  {"scenario/本編/w17_01d.s",{11,14,70,135,143,151,157,174,268}},
  {"scenario/本編/w17_03b.s",{65,140,144}},
  {"scenario/本編/w99_ed_01.s",{118,155,158,174,177,183,199,202}},
  {"scenario/本編/w@03_02.s",{701,1349}},
  {"scenario/本編/w@04_02.s",{1082}},
  {"scenario/本編/w@06_02.s",{588,1413}},
  {"scenario/本編/w@07_02.s",{1294}},
  {"scenario/本編/w@08_02.s",{560}},
  {"scenario/本編/w@08_04.s",{1380,1454,1471,1569}},
  {"scenario/本編/w@10_02.s",{785,954,1511}},
  {"scenario/本編/w@12_02.s",{1169,1251}},
  {"scenario/本編/w@13_02.s",{1334}},
  {"scenario/本編/w@14_02.s",{1132}},
  {"scenario/本編/w@14_05.s",{856}},
  {"scenario/本編/w@15_02b.s",{372,377,379}},
  {"scenario/本編/w@15_03a.s",{18,838,852,966}},
  {"scenario/本編/w@15_03b.s",{130,750,772,865}},
  {"scenario/本編/w@16_03.s",{913,915}}}
};
// Give the three standalone fades a full visible interval. The first
// white overlap uses the existing following wait.
inline void applySentenceTiming(Event& event){
 if(event.line==476&&event.file=="scenario/本編/w03_01.s"&&
    event.value=="^sentence,$overlap,wait:click")
  event.value="^sentence,fade:overlap:1000,wait:click:1000";
 if(event.file!="scenario/本編/w@16_03.s")return;
 if(event.line==913&&event.value=="^sentence,wait:click:1000")
  event.value="^sentence,wait:click:1500";
 if(event.line==915&&event.value=="^sentence,$overlap_long")
  event.value="^sentence,fade:overlap:2000,wait:click:2000";
}
inline constexpr char gameName[] = "美少女万華鏡２";
inline constexpr char sdRoot[] = "sdmc:/switch/whj2";
inline constexpr char sdGame[] = "sdmc:/switch/whj2/game";
inline constexpr char sdSaves[] = "sdmc:/switch/whj2/saves";
inline constexpr char sdRenderer[] = "sdmc:/switch/whj2/renderer.cfg";
}
