// SPDX-License-Identifier: GPL-2.0-or-later
#include "cg_catalog.hpp"
#include <sstream>
#include <iostream>
namespace whj2 {
CgCatalog::CgCatalog(qlie::Assets&assets){std::istringstream input(qlie::cp932(assets.read("script/albummode/list.txt")));std::string line;
 while(std::getline(input,line)){line=trim(line);if(line.empty())continue;auto row=split(line,',');CgEntry entry;entry.thumbnail=qlie::canonical("script/albummode/thumimage/"+row[0]+".png");
 for(size_t i=1;i<row.size();i++){auto base=qlie::canonical("image/ev/"+row[i]);bool found=false;for(auto ext:{"",".png",".b",".bmp",".jpg"}){auto path=base+ext;if(assets.exists(path)){entry.files.push_back(path);found=true;break;}}if(!found)std::cerr<<"CG-CATALOG missing authored entry "<<base<<"\n";}
 if(entry.files.empty()||!assets.exists(entry.thumbnail)){throw std::runtime_error("Incomplete CG group: "+entry.thumbnail);}entries.push_back(std::move(entry));
 }}
bool CgCatalog::unlocked(size_t index,const Progress&progress)const{for(auto&path:entries.at(index).files)if(progress.seenFile(path))return true;return false;}
}
