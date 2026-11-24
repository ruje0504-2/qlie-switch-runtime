// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "qlie.hpp"
#include <array>
#include <map>
namespace whj2 {
class Settings {
 std::map<std::string,int> values,defaults;
public:
 explicit Settings(qlie::Assets&);
 int get(const std::string&)const;
 void set(const std::string&,int);
 void reset(){values=defaults;}
 std::array<float,5> gains()const;
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
