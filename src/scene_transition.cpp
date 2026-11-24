// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_transition.hpp"
#include "story.hpp"
#include "state_io.hpp"
#include "gradient_mask.hpp"
#include "render_queue.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
namespace whj2 {
SceneTransition::SceneTransition(qlie::Assets&assets):assets(assets){std::istringstream in(qlie::cp932(assets.read("avg/imoscripter_template_sentence.txt")));std::string line;
 while(std::getline(in,line)){auto pos=line.find('=');if(pos!=line.npos)templates[trim(line.substr(0,pos))]=trim(line.substr(pos+1));}
}
void SceneTransition::reset(){previous.clear();rule={};rulePath.clear();ruleMode=4;elapsed=duration=0;staged=active=caught=false;}
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
   if(mode!="overlap"&&mode!="rule"){std::cerr<<"PENDING-SENTENCE fade="<<mode<<"\n";continue;}
   rule={};rulePath.clear();ruleMode=4;
   if(mode=="rule"&&fields.size()>3&&!fields[3].empty()&&fields[3]!="none"){
    rulePath="fade/"+fields[3];if(!assets.exists(rulePath)&&assets.exists(rulePath+".png"))rulePath+=".png";
    rule=image(assets.read(rulePath));if(!rule.width||!rule.height)throw std::runtime_error("Empty transition rule");
    if(fields.size()>4&&!fields[4].empty())ruleMode=unsigned(std::stoul(fields[4][0]=='$'?fields[4].substr(1):fields[4],nullptr,fields[4][0]=='$'?16:10));
    if(ruleMode>255)throw std::runtime_error("Transition rule mode range");
   }
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
 if(rulePath.empty()){for(size_t i=0;i<out.size();i++)out[i]=uint8_t(previous[i]*(1-amount)+out[i]*amount);return;}
 auto opacity=uint8_t(255*(1-amount));
 for(unsigned y=0;y<720;y++)for(unsigned x=0;x<1280;x++){
  size_t at=(size_t(y)*1280+x)*4;unsigned mx=unsigned(uint64_t(x)*rule.width/1280),my=unsigned(uint64_t(y)*rule.height/720);
  auto alpha=gradientMaskAlpha(255,rule.pixels[(size_t(my)*rule.width+mx)*4],opacity,uint8_t(ruleMode));
  for(unsigned c=0;c<4;c++)out[at+c]=blendChannel(0,previous[at+c],out[at+c],alpha);
 }
}
qlie::Bytes SceneTransition::save()const{StateWriter w;w.str("WHJ2-transition-2");w.n(uint64_t(elapsed*1000));w.n(uint64_t(duration*1000));w.n(staged);w.n(active);w.n(caught);w.str(rulePath);w.n(ruleMode);w.n(previous.size());w.bytes.insert(w.bytes.end(),previous.begin(),previous.end());return w.bytes;}
void SceneTransition::restore(const qlie::Bytes&bytes){StateReader r{bytes};auto version=r.str();if(version!="WHJ2-transition-1"&&version!="WHJ2-transition-2")throw std::runtime_error("Transition state version");
 auto time=r.n()/1000.,span=r.n()/1000.;bool waiting=r.n()!=0,running=r.n()!=0,captured=r.n()!=0;std::string savedRule;unsigned savedMode=4;if(version=="WHJ2-transition-2"){savedRule=r.str();auto mode=r.n();if(mode>255)throw std::runtime_error("Saved rule mode");savedMode=unsigned(mode);}auto size=r.n();if(size!=0&&size!=1280*720*4)throw std::runtime_error("Transition saved dimensions");if(size!=bytes.size()-r.p||time>span)throw std::runtime_error("Transition state bounds");
 Image savedImage;if(!savedRule.empty()){savedImage=image(assets.read(savedRule));if(!savedImage.width||!savedImage.height)throw std::runtime_error("Empty saved rule");}rule=std::move(savedImage);rulePath=savedRule;ruleMode=savedMode;previous.assign(bytes.begin()+r.p,bytes.end());elapsed=time;duration=span;staged=waiting;active=running;caught=captured;
}
}
