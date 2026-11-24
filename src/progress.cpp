// SPDX-License-Identifier: GPL-2.0-or-later
#include "progress.hpp"
#include "state_io.hpp"
namespace whj2 {
namespace {const char*keys[]={"#game_aclearflag","#game_bclearflag","#game_cclearflag","#game_dclearflag"};}
bool Progress::capture(const Story&story){bool changed=false;for(unsigned i=0;i<4;i++){auto at=story.state.vars.find(keys[i]);if(at!=story.state.vars.end()&&at->second=="1"&&!cleared[i]){cleared[i]=true;changed=true;}}for(auto&label:story.visited)if(labels.insert(label).second)changed=true;return changed;}
void Progress::apply(Story&story)const{for(unsigned i=0;i<4;i++)if(cleared[i])story.state.vars[keys[i]]="1";}
qlie::Bytes Progress::save()const{StateWriter out;out.str("WHJ2-progress-1");unsigned mask=0;for(unsigned i=0;i<4;i++)if(cleared[i])mask|=1u<<i;out.n(mask);out.n(labels.size());for(auto&label:labels)out.str(label);return out.bytes;}
void Progress::restore(const qlie::Bytes&bytes){StateReader in{bytes};if(in.str()!="WHJ2-progress-1")throw std::runtime_error("System progress version");auto mask=in.n();if(mask>15)throw std::runtime_error("System progress route flags");auto count=in.n();if(count>1000000)throw std::runtime_error("System progress label count");std::set<std::string> restored;while(count--)restored.insert(in.str());in.end();for(unsigned i=0;i<4;i++)cleared[i]=(mask&(1u<<i))!=0;labels=std::move(restored);}
}
