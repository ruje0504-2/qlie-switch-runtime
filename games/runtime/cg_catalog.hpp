// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "progress.hpp"
namespace whj2 {
struct CgEntry {std::string thumbnail;std::vector<std::string> files;};
class CgCatalog {
 std::vector<CgEntry> entries;
public:
 explicit CgCatalog(qlie::Assets&,bool extendedRoots=false);
 const std::vector<CgEntry>&items()const{return entries;}
 bool unlocked(size_t index,const Progress&progress)const;
};
}
