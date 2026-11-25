// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>

namespace whj2 {
struct SettingsSliderLayout {
 float x,y,width,trackX,trackY,thumbY;
};
struct SettingsLayout {
 std::array<SettingsSliderLayout,9> sliders;
 std::array<float,5> testButtonY;
 bool classic=false;
};
inline constexpr SettingsLayout standardSettingsLayout{{{
 {357,195,241,-8,-10,0},{357,314,241,-8,-10,0},
 {664,163,461,-10,-11,0},{664,222,461,-10,-11,0},{664,279,461,-10,-11,0},
 {664,340,461,-10,-11,0},{664,399,461,-10,-11,0},{664,458,461,-10,-11,0},
 {357,373,241,-8,-10,0}
}}, {202,260,321,381,440}};
}
