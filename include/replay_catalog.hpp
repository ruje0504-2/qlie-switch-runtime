// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "progress.hpp"
namespace whj2 {
struct ReplayEntry {std::string thumbnail,script,label,key;};
class ReplayCatalog {
 std::vector<ReplayEntry> entries;
public:
 explicit ReplayCatalog(qlie::Assets&);
 const std::vector<ReplayEntry>&items()const{return entries;}
 bool unlocked(size_t index,const Progress&progress)const{return progress.visited(entries.at(index).key);}
};
}
