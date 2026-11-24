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
void SceneTransition::reset(){previous={};rulePath.clear();ruleMode=4;elapsed=duration=0;staged=active=caught=false;}
void SceneTransition::loadRule(const std::string&path){
 if(cachedRulePath==path)return;
 auto decoded=image(assets.read(path));if(!decoded.width||!decoded.height)throw std::runtime_error("Empty transition rule");
 decoded.version.publish();rule=std::move(decoded);cachedRulePath=path;
}
bool SceneTransition::command(const std::string&line,const qlie::Bytes&scene,SentenceWait*wait){
 return commandWithCapture(line,[&scene]{return scene;},wait);
}
bool SceneTransition::commandWithCapture(const std::string&line,const std::function<qlie::Bytes()>&capture,SentenceWait*wait){
 if(line.rfind("^sentence",0)!=0)return false;
 auto comma=line.find(',');if(comma==line.npos)return true;auto args=line.substr(comma+1);
 std::vector<std::pair<std::string,std::string>> values(templates.begin(),templates.end());std::sort(values.begin(),values.end(),[](auto&a,auto&b){return a.first.size()>b.first.size();});
 for(unsigned pass=0;pass<12;pass++){bool changed=false;for(auto&item:values){size_t pos=0;while((pos=args.find(item.first,pos))!=args.npos){args.replace(pos,item.first.size(),item.second);pos+=item.second.size();changed=true;}}if(!changed)break;if(pass==11)throw std::runtime_error("Sentence template recursion");}
 // A command can contain more than one fade/clear. Determine its snapshot
 // demand before mutating the old transition, then capture the original once.
 std::vector<std::vector<std::string>> fieldsList;
 unsigned uses=0;bool captured=caught;
 auto fadeTime=[](const std::vector<std::string>&fields){return fields.size()>2&&!fields[2].empty()?(fields[2][0]=='$'?double(std::stoul(fields[2].substr(1),nullptr,16)):std::stod(fields[2])):0.;};
 for(auto&arg:split(args,',')){
  auto fields=split(arg,':');if(fields.empty())continue;
  if(fields[0]=="clear"&&fields.size()>1&&fields[1]=="def")captured=false;
  if(fields[0]=="fade"){
   auto mode=fields.size()>1?fields[1]:"cut";
   if(mode=="catch"){++uses;captured=true;}
   else if(mode=="cut")captured=false;
   else if(mode=="overlap"||mode=="rule"){if(fadeTime(fields)>0&&!captured)++uses;captured=false;}
  }
  fieldsList.push_back(std::move(fields));
 }
 qlie::Bytes scene;
 if(uses){scene=capture();if(scene.size()!=1280*720*4)throw std::runtime_error("Transition scene dimensions");}
 auto storeSnapshot=[&]{
  --uses;
  if(uses)previous={1280,720,scene};else previous={1280,720,std::move(scene)};
  previous.version.publish();
 };
 bool explicitWait=false,hasFade=false;double waitFadeTime=0;
 for(auto&fields:fieldsList){
  if(wait&&wait->property(fields)){if(fields[0]=="wait")explicitWait=true;continue;}
  if(fields[0]=="fade"){hasFade=true;
   auto mode=fields.size()>1?fields[1]:"cut";
   waitFadeTime=fadeTime(fields);
   if(mode=="cut"){reset();continue;}
   if(mode=="catch"){storeSnapshot();staged=false;active=false;caught=true;continue;}
   if(mode!="overlap"&&mode!="rule"){std::cerr<<"PENDING-SENTENCE fade="<<mode<<"\n";continue;}
   rulePath.clear();ruleMode=4;
   if(mode=="rule"&&fields.size()>3&&!fields[3].empty()&&fields[3]!="none"){
    rulePath="fade/"+fields[3];if(!assets.exists(rulePath)&&assets.exists(rulePath+".png"))rulePath+=".png";
    loadRule(rulePath);
    if(fields.size()>4&&!fields[4].empty())ruleMode=unsigned(std::stoul(fields[4][0]=='$'?fields[4].substr(1):fields[4],nullptr,fields[4][0]=='$'?16:10));
    if(ruleMode>255)throw std::runtime_error("Transition rule mode range");
   }
   duration=waitFadeTime;
   if(duration<=0){reset();continue;}
   if(!caught){storeSnapshot();}
   caught=false;elapsed=0;staged=true;active=false;
  }else if(fields[0]=="clear"&&fields.size()>1&&fields[1]=="def")reset();
  else if(fields[0]!="")std::cerr<<"PENDING-SENTENCE property="<<fields[0]<<"\n";
 }if(wait&&hasFade&&!explicitWait)wait->fromFade(waitFadeTime);return true;
}
void SceneTransition::present(){if(staged){staged=false;active=true;}}
void SceneTransition::update(double ms){if(active){elapsed=std::min(duration,elapsed+std::max(0.,ms));if(elapsed>=duration){active=false;previous={};}}}
void SceneTransition::blend(qlie::Bytes&out)const{if(!active||previous.pixels.empty())return;if(out.size()!=previous.pixels.size())throw std::runtime_error("Transition frame dimensions");
 double amount=duration>0?std::clamp(elapsed/duration,0.,1.):1;
 if(rulePath.empty()){for(size_t i=0;i<out.size();i++)out[i]=uint8_t(previous.pixels[i]*(1-amount)+out[i]*amount);return;}
 auto opacity=uint8_t(255*(1-amount));
 for(unsigned y=0;y<720;y++)for(unsigned x=0;x<1280;x++){
  size_t at=(size_t(y)*1280+x)*4;unsigned mx=unsigned(uint64_t(x)*rule.width/1280),my=unsigned(uint64_t(y)*rule.height/720);
  auto alpha=gradientMaskAlpha(255,rule.pixels[(size_t(my)*rule.width+mx)*4],opacity,uint8_t(ruleMode));
  for(unsigned c=0;c<4;c++)out[at+c]=blendChannel(0,previous.pixels[at+c],out[at+c],alpha);
 }
}
void SceneTransition::blend(RenderTarget&target)const{if(!active||previous.pixels.empty())return;double amount=duration>0?std::clamp(elapsed/duration,0.,1.):1;target.transition(previous,rulePath.empty()?nullptr:&rule,amount,ruleMode);}
qlie::Bytes SceneTransition::save()const{StateWriter w;w.str("WHJ2-transition-2");w.n(uint64_t(elapsed*1000));w.n(uint64_t(duration*1000));w.n(staged);w.n(active);w.n(caught);w.str(rulePath);w.n(ruleMode);w.n(previous.pixels.size());w.bytes.insert(w.bytes.end(),previous.pixels.begin(),previous.pixels.end());return w.bytes;}
void SceneTransition::restore(const qlie::Bytes&bytes){StateReader r{bytes};auto version=r.str();if(version!="WHJ2-transition-1"&&version!="WHJ2-transition-2")throw std::runtime_error("Transition state version");
 auto time=r.n()/1000.,span=r.n()/1000.;bool waiting=r.n()!=0,running=r.n()!=0,captured=r.n()!=0;std::string savedRule;unsigned savedMode=4;if(version=="WHJ2-transition-2"){savedRule=r.str();auto mode=r.n();if(mode>255)throw std::runtime_error("Saved rule mode");savedMode=unsigned(mode);}auto size=r.n();if(size!=0&&size!=1280*720*4)throw std::runtime_error("Transition saved dimensions");if(size!=bytes.size()-r.p||time>span)throw std::runtime_error("Transition state bounds");
 if(!savedRule.empty())loadRule(savedRule);
 rulePath=savedRule;ruleMode=savedMode;previous={size?1280u:0u,size?720u:0u,qlie::Bytes(bytes.begin()+r.p,bytes.end())};previous.version.publish();elapsed=time;duration=span;staged=waiting;active=running;caught=captured;
}
}
