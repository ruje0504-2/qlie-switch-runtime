// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include "cg_catalog.hpp"
#include "profile.hpp"
#include <sstream>
#include <iostream>
namespace whj2 {
CgCatalog::CgCatalog(qlie::Assets&assets,bool extendedRoots){std::istringstream input(assets.text("script/albummode/list.txt"));std::string line;
 const std::vector<const char*> prefixes=game_profile::story.legacyAvg?std::vector<const char*>{"image/"}:extendedRoots?
  std::vector<const char*>{"image/ev/","image/movie/","image/cutin/"}:
  std::vector<const char*>{"image/ev/"};
 const std::vector<const char*> extensions=extendedRoots?
  std::vector<const char*>{"",".png",".ogv",".b",".jpg"}:
  std::vector<const char*>{"",".png",".b",".bmp",".jpg"};
 while(std::getline(input,line)){line=trim(line);if(line.empty())continue;auto row=split(line,',');CgEntry entry;entry.thumbnail=qlie::canonical("script/albummode/thumimage/"+row[0]+".png");
 for(size_t i=1;i<row.size();i++){
  if(row[i].empty())continue;
  bool found=false;
  for(auto prefix:prefixes){
   const auto base=qlie::canonical(prefix+row[i]);
   for(auto ext:extensions){
    auto path=base+ext;
    if(!assets.exists(path))continue;
    entry.files.push_back(std::move(path));found=true;break;
   }
   if(found)break;
  }
  if(!found&&!extendedRoots)QLIE_LOG_LINE(<<"CG-CATALOG missing authored entry "<<row[i]<<'\n');
 }
 if(entry.files.empty()||!assets.exists(entry.thumbnail)){throw std::runtime_error("Incomplete CG group: "+entry.thumbnail);}entries.push_back(std::move(entry));
 }}
bool CgCatalog::unlocked(size_t index,const Progress&progress)const{for(auto&path:entries.at(index).files)if(progress.seenFile(path))return true;return false;}
}
