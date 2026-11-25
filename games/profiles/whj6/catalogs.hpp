// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cg_catalog.hpp"
#include "replay_catalog.hpp"
#include "story.hpp"
#include <sstream>

namespace whj6::catalogs {
inline std::vector<std::vector<std::string>> rows(qlie::Assets& assets,const char* path){
 std::vector<std::vector<std::string>> result;
 std::istringstream input(assets.text(path));
 for(std::string line;std::getline(input,line);){
  line=whj2::trim(line);
  if(!line.empty()&&line.rfind("//",0)!=0)result.push_back(whj2::split(line,','));
 }
 return result;
}
inline std::vector<whj2::ReplayEntry> replays(qlie::Assets& assets){
 std::vector<whj2::ReplayEntry> entries;
 for(auto list:{"thumbnail/memory/mlist_01.txt","thumbnail/memory/mlist_02.txt"}){
  for(const auto& row:rows(assets,list)){
   if(row.size()<3)throw std::runtime_error("WHJ6 replay list fields");
   const auto script=qlie::canonical("scenario/本編/"+row[2]+".s");
   const auto label=qlie::canonical(row[1]);
   const auto thumb=qlie::canonical("thumbnail/memory/"+row[0]+".png");
   if(!assets.exists(script)||!assets.exists(thumb))throw std::runtime_error("WHJ6 replay resource missing: "+script);
   entries.push_back({thumb,script,label,script+":"+label});
  }
 }
 return entries;
}
inline std::vector<whj2::CgEntry> gallery(qlie::Assets& assets){
 std::map<std::string,std::string> movies;
 for(const auto& name:assets.names()){
  if(name.rfind("画像/movie/",0)!=0)continue;
  movies.emplace(name.substr(name.find_last_of('/')+1),"image/"+name.substr(std::string("画像/").size()));
 }
 std::vector<whj2::CgEntry> entries;
 for(auto list:{"thumbnail/album/list_c01.txt","thumbnail/album/list_c02.txt","thumbnail/album/list_c03.txt"}){
  for(const auto& row:rows(assets,list)){
   whj2::CgEntry entry{qlie::canonical("thumbnail/album/"+row[0]+".png"),{}};
   for(size_t i=1;i<row.size();++i){
    auto item=qlie::canonical(row[i]);
    if(item.empty())continue;
    const auto movie=movies.find(item);
    if(movie!=movies.end()){entry.files.push_back(movie->second);continue;}
    for(auto root:{"image/ev/","image/cutin/","image/"}){
     std::string match;
     for(auto extension:{".jpg",".png",".b",""}){
      auto path=root+item+extension;
      if(assets.exists(path)){match=std::move(path);break;}
     }
     if(!match.empty()){entry.files.push_back(std::move(match));break;}
    }
   }
   if(entry.files.empty()||!assets.exists(entry.thumbnail))throw std::runtime_error("Incomplete WHJ6 CG group: "+entry.thumbnail);
   entries.push_back(std::move(entry));
  }
 }
 return entries;
}
}
