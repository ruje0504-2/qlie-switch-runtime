// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles/whj2.hpp"
#include "progress.hpp"
#include "replay_catalog.hpp"
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){try{if(argc<2)return 2;qlie::Assets assets(argv[1],whj2::profile::archives);whj2::ReplayCatalog catalog(assets);whj2::Progress total;for(size_t i=0;i<catalog.items().size();i++)if(catalog.unlocked(i,total))throw std::runtime_error("Fresh replay catalog unlocked");if(total.extras())throw std::runtime_error("Fresh progress unlocked");
 const char*choices[]={"000100","000000","000010","000001"};
 for(unsigned route=0;route<4;route++){whj2::Story story(assets,whj2::profile::story);story.start();auto early=story.save();whj2::Progress progress;if(story.visited.count("scenario/本編/w@03_02.s:@@replay01"))throw std::runtime_error("Replay label recorded before execution");unsigned decision=0,events=0;
  for(;events<2000000;events++){auto event=story.next();progress.capture(story);if(event.type=="choice")story.choose(unsigned(choices[route][decision++]-'0'));if(event.type=="finish"||event.type=="finished")break;}
  if(route==0&&progress.visited("scenario/本編/w17_01d.s:@@top"))throw std::runtime_error("Unchosen ending pre-unlocked");
  if(route==0&&!progress.visited("scenario/本編/w@03_02.s:@@replay01"))throw std::runtime_error("Naturally reached replay label not recorded");
  if(events==2000000||!progress.clearedRoute(route)||!progress.extras())throw std::runtime_error("Original route did not set expected flag");
  whj2::Progress restored;restored.restore(progress.save());unsigned available=0;for(size_t i=0;i<catalog.items().size();i++)if(catalog.unlocked(i,restored))available++;if(!available)throw std::runtime_error("Reached replay labels did not unlock catalog");if(route==0&&available==catalog.items().size())throw std::runtime_error("Unplayed replay entries unlocked");std::cout<<"PASS replay catalog route="<<route<<" unlocked="<<available<<" total="<<catalog.items().size()<<"\n";story.restore(early);restored.apply(story);restored.capture(story);if(!restored.clearedRoute(route))throw std::runtime_error("Loading early slot erased completion");
  whj2::Story fresh(assets,whj2::profile::story);fresh.start();restored.apply(fresh);const char*keys[]={"#game_aclearflag","#game_bclearflag","#game_cclearflag","#game_dclearflag"};if(fresh.state.vars[keys[route]]!="1")throw std::runtime_error("Restart lost completion");restored.capture(fresh);total.capture(fresh);
  if(route==0&&argc>2){std::filesystem::create_directories(std::filesystem::path(argv[2]).parent_path());qlie::write_file(argv[2],progress.save());}
  std::cout<<"PASS original route="<<route<<" events="<<events<<" restart/old-slot/persistent flags\n";
 }
 whj2::Progress loaded;loaded.restore(total.save());for(unsigned i=0;i<4;i++)if(!loaded.clearedRoute(i))throw std::runtime_error("Merged completion lost route");return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
