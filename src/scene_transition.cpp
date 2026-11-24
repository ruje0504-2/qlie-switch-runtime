// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_transition.hpp"
#include "story.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
namespace whj2 {
SceneTransition::SceneTransition(qlie::Assets&assets){std::istringstream in(qlie::cp932(assets.read("avg/imoscripter_template_sentence.txt")));std::string line;
 while(std::getline(in,line)){auto pos=line.find('=');if(pos!=line.npos)templates[trim(line.substr(0,pos))]=trim(line.substr(pos+1));}
}
void SceneTransition::reset(){previous.clear();elapsed=duration=0;staged=active=caught=false;}
bool SceneTransition::command(const std::string&line,const qlie::Bytes&scene,SentenceWait*wait){if(line.rfind("^sentence",0)!=0)return false;
 auto comma=line.find(',');if(comma==line.npos)return true;auto args=line.substr(comma+1);
 std::vector<std::pair<std::string,std::string>> values(templates.begin(),templates.end());std::sort(values.begin(),values.end(),[](auto&a,auto&b){return a.first.size()>b.first.size();});
 for(unsigned pass=0;pass<12;pass++){bool changed=false;for(auto&item:values){size_t pos=0;while((pos=args.find(item.first,pos))!=args.npos){args.replace(pos,item.first.size(),item.second);pos+=item.second.size();changed=true;}}if(!changed)break;if(pass==11)throw std::runtime_error("Sentence template recursion");}
 bool explicitWait=false,hasFade=false;double waitFadeTime=0;
 for(auto&arg:split(args,',')){auto fields=split(arg,':');if(fields.empty())continue;
  if(wait&&wait->property(fields)){if(fields[0]=="wait")explicitWait=true;continue;}
  if(fields[0]=="fade"){hasFade=true;
   auto mode=fields.size()>1?fields[1]:"cut";
   waitFadeTime=fields.size()>2&&!fields[2].empty()?(fields[2][0]=='$'?double(std::stoul(fields[2].substr(1),nullptr,16)):std::stod(fields[2])):0;
   if(mode=="cut"){reset();continue;}
   if(mode=="catch"){previous=scene;staged=false;active=false;caught=true;continue;}
   if(mode!="overlap"){std::cerr<<"PENDING-SENTENCE fade="<<mode<<"\n";continue;}
   duration=waitFadeTime;
   if(duration<=0){reset();continue;}
   if(scene.size()!=1280*720*4)throw std::runtime_error("Transition scene dimensions");
   if(!caught){previous=scene;}
   caught=false;elapsed=0;staged=true;active=false;
  }else if(fields[0]=="clear"&&fields.size()>1&&fields[1]=="def")reset();
  else if(fields[0]!="")std::cerr<<"PENDING-SENTENCE property="<<fields[0]<<"\n";
 }if(wait&&hasFade&&!explicitWait)wait->fromFade(waitFadeTime);return true;
}
void SceneTransition::present(){if(staged){staged=false;active=true;}}
void SceneTransition::update(double ms){if(active){elapsed=std::min(duration,elapsed+std::max(0.,ms));if(elapsed>=duration){active=false;previous.clear();}}}
void SceneTransition::blend(qlie::Bytes&out)const{if(!active||previous.empty())return;if(out.size()!=previous.size())throw std::runtime_error("Transition frame dimensions");
 double amount=duration>0?std::clamp(elapsed/duration,0.,1.):1;
 for(size_t i=0;i<out.size();i++)out[i]=uint8_t(previous[i]*(1-amount)+out[i]*amount);
}
qlie::Bytes SceneTransition::save()const{StateWriter w;w.str("WHJ2-transition-1");w.n(uint64_t(elapsed*1000));w.n(uint64_t(duration*1000));w.n(staged);w.n(active);w.n(caught);w.n(previous.size());w.bytes.insert(w.bytes.end(),previous.begin(),previous.end());return w.bytes;}
void SceneTransition::restore(const qlie::Bytes&bytes){StateReader r{bytes};if(r.str()!="WHJ2-transition-1")throw std::runtime_error("Transition state version");
 auto time=r.n()/1000.,span=r.n()/1000.;bool waiting=r.n()!=0,running=r.n()!=0,captured=r.n()!=0;auto size=r.n();if(size!=0&&size!=1280*720*4)throw std::runtime_error("Transition saved dimensions");if(size!=bytes.size()-r.p||time>span)throw std::runtime_error("Transition state bounds");
 previous.assign(bytes.begin()+r.p,bytes.end());elapsed=time;duration=span;staged=waiting;active=running;caught=captured;
}
}
