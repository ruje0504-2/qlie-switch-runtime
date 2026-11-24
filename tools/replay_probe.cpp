// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_catalog.hpp"
#include <iostream>
struct Branch {qlie::Bytes state;std::set<std::string> visited;std::string path;unsigned pages=0,events=0;};
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1]);whj2::ReplayCatalog catalog(assets);unsigned complete=0,totalPaths=0;
 for(auto&entry:catalog.items()){whj2::Story initial(assets);initial.startReplay(entry.script);std::vector<Branch> pending{{initial.save(),initial.visited,"",0,0}};unsigned paths=0;
  while(!pending.empty()){auto branch=std::move(pending.back());pending.pop_back();whj2::Story story(assets);story.restore(branch.state);story.visited=branch.visited;bool terminal=false;
   for(;branch.events<100000;branch.events++){auto event=story.next();if(event.type=="page")branch.pages++;
    if(branch.events==37){auto saved=story.save();whj2::Story restored(assets);restored.restore(saved);if(!restored.state.replay)throw std::runtime_error("Replay save lost mode");auto a=story.next(),b=restored.next();if(a.type!=b.type||a.value!=b.value||a.file!=b.file||a.line!=b.line)throw std::runtime_error("Replay save continuation");story.restore(saved);}
    if(event.type=="choice"){if(story.state.choices.empty())throw std::runtime_error("Empty original replay choice");auto saved=story.save();
     for(unsigned i=0;i<story.state.choices.size();i++){whj2::Story selected(assets);selected.restore(saved);selected.choose(i);pending.push_back({selected.save(),story.visited,branch.path+std::to_string(i),branch.pages,branch.events+1});}
     terminal=true;break;
    }
    if(event.type=="finish"){if(event.value!="replay"||!branch.pages||!story.visited.count(entry.key))throw std::runtime_error("Replay escaped or range incomplete: "+entry.script);paths++;totalPaths++;terminal=true;
     std::cout<<"PASS replay "<<complete+1<<" path="<<(branch.path.empty()?"none":branch.path)<<" pages="<<branch.pages<<" events="<<branch.events<<" returned-to-caller\n";break;}
   }
   if(!terminal)throw std::runtime_error("Replay instruction budget exhausted: "+entry.script);
  }
  if(!paths)throw std::runtime_error("Replay did not finish");complete++;
 }
 std::cout<<"PASS replay entries="<<complete<<" complete choice paths="<<totalPaths<<"\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
