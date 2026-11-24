// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_catalog.hpp"
#include <iostream>
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1]);whj2::ReplayCatalog catalog(assets);unsigned complete=0;
 for(auto&entry:catalog.items()){whj2::Story story(assets);story.startReplay(entry.script);unsigned pages=0,events=0;bool finished=false;
  for(;events<100000;events++){auto event=story.next();if(event.type=="choice")throw std::runtime_error("Unexpected choice in replay");if(event.type=="page")pages++;
   if(events==37){auto saved=story.save();whj2::Story restored(assets);restored.restore(saved);if(!restored.state.replay)throw std::runtime_error("Replay save lost mode");auto a=story.next(),b=restored.next();if(a.type!=b.type||a.value!=b.value||a.file!=b.file||a.line!=b.line)throw std::runtime_error("Replay save continuation");story.restore(saved);}
   if(event.type=="finish"){if(event.value!="replay")throw std::runtime_error("Replay escaped to normal title");finished=true;break;}
  }
  if(!finished||!pages||!story.visited.count(entry.key))throw std::runtime_error("Replay range or label incomplete: "+entry.script);complete++;std::cout<<"PASS replay "<<complete<<" pages="<<pages<<" events="<<events<<" returned-to-caller\n";
 }return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
