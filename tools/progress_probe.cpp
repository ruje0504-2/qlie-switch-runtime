// SPDX-License-Identifier: GPL-2.0-or-later
#include "progress.hpp"
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){try{if(argc<2)return 2;qlie::Assets assets(argv[1]);whj2::Progress total;if(total.extras())throw std::runtime_error("Fresh progress unlocked");
 const char*choices[]={"000100","000000","000010","000001"};
 for(unsigned route=0;route<4;route++){whj2::Story story(assets);story.start();auto early=story.save();whj2::Progress progress;unsigned decision=0,events=0;
  for(;events<2000000;events++){auto event=story.next();progress.capture(story);if(event.type=="choice")story.choose(unsigned(choices[route][decision++]-'0'));if(event.type=="finish"||event.type=="finished")break;}
  if(events==2000000||!progress.clearedRoute(route)||!progress.extras())throw std::runtime_error("Original route did not set expected flag");
  whj2::Progress restored;restored.restore(progress.save());story.restore(early);restored.apply(story);restored.capture(story);if(!restored.clearedRoute(route))throw std::runtime_error("Loading early slot erased completion");
  whj2::Story fresh(assets);fresh.start();restored.apply(fresh);const char*keys[]={"#game_aclearflag","#game_bclearflag","#game_cclearflag","#game_dclearflag"};if(fresh.state.vars[keys[route]]!="1")throw std::runtime_error("Restart lost completion");restored.capture(fresh);total.capture(fresh);
  if(route==0&&argc>2){std::filesystem::create_directories(std::filesystem::path(argv[2]).parent_path());qlie::write_file(argv[2],progress.save());}
  std::cout<<"PASS original route="<<route<<" events="<<events<<" restart/old-slot/persistent flags\n";
 }
 whj2::Progress loaded;loaded.restore(total.save());for(unsigned i=0;i<4;i++)if(!loaded.clearedRoute(i))throw std::runtime_error("Merged completion lost route");return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
