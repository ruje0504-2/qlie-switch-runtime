// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"
#include <array>
namespace whj2 {
class Progress {
 std::array<bool,4> cleared{};
 std::set<std::string> labels;
public:
 bool capture(const Story&);
 void apply(Story&)const;
 bool extras()const{return cleared[0]||cleared[1]||cleared[2];}
 bool clearedRoute(unsigned route)const{return route<cleared.size()&&cleared[route];}
 bool visited(const std::string&key)const{return labels.count(key)!=0;}
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
