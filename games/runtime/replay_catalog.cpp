// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_catalog.hpp"
#include "profile.hpp"
#include <sstream>
#include <stdexcept>
namespace whj2 {
ReplayCatalog::ReplayCatalog(qlie::Assets&assets){std::istringstream input(assets.text("script/memorymode/list.txt"));std::string line;
 while(std::getline(input,line)){line=trim(line);if(line.empty())continue;auto row=split(line,',');if(row.size()!=3)throw std::runtime_error("Replay list fields");
  auto base=qlie::canonical(std::string(game_profile::story.legacyAvg?"scenario/":"scenario/本編/")+row[1]);ReplayEntry entry;
  const bool directLabel=row[2].rfind("@@",0)==0;
  entry.thumbnail="script/memorymode/thumimage/"+row[0]+".png";
  entry.script=directLabel?base+".s":base+"_"+qlie::canonical(row[2])+".s";
  entry.label=qlie::canonical(directLabel?row[2]:"@@"+row[2]);entry.key=base+".s:"+entry.label;
  if(!assets.exists(entry.script)||!assets.exists(entry.thumbnail))throw std::runtime_error("Replay catalog resource missing: "+entry.script);
  entries.push_back(std::move(entry));
 }
}
}
