// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include "scene_transition.hpp"
#include "asset_read_ahead.hpp"
#include "story.hpp"
#include "state_io.hpp"
#include "gradient_mask.hpp"
#include "render_queue.hpp"
#include "scene_images.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
namespace whj2 {
namespace {
constexpr unsigned Width=1280,Height=720;
constexpr size_t FrameBytes=size_t(Width)*Height*4;
constexpr auto AnimationLayerName="ui_transition";

std::string fadeAsset(const qlie::Assets&assets,const std::string&name){
 auto path=qlie::canonical(name);
 auto candidates=path.find('/')==path.npos?std::vector<std::string>{"fade/"+path}:std::vector<std::string>{"fade/"+path,path};
 for(auto&candidate:candidates){
  if(assets.exists(candidate))return candidate;
  for(auto extension:{".b",".png",".bmp",".jpg"})if(assets.exists(candidate+extension))return candidate+extension;
 }
 throw std::runtime_error("Missing transition asset "+name);
}

double fadeTime(const std::vector<std::string>&fields){
 if(fields.size()<=2||fields[2].empty())return 0;
 return fields[2][0]=='$'?double(std::stoul(fields[2].substr(1),nullptr,16)):std::stod(fields[2]);
}
bool hasFadeAsset(const std::vector<std::string>&fields){
 return fields.size()>3&&!fields[3].empty()&&fields[3]!="none";
}
std::shared_ptr<const Image> readRule(qlie::Assets&assets,const std::string&path){
 auto decoded=image(assets.read(path));
 if(!decoded.width||!decoded.height)throw std::runtime_error("Empty transition rule");
 decoded.version.publish();
 return std::make_shared<const Image>(std::move(decoded));
}

void mosaic(qlie::Bytes&out,const qlie::Bytes&old,double amount){
 // QLIE's fdi 4 composites the old picture first, then mosaics the result.
 const auto step=int(amount*255);
 const auto alpha=uint8_t(std::clamp((143-step)*255/32,0,255));
 const auto strength=unsigned(2*std::min(step,255-step));
 const auto scale=1+9*strength/255;
 const auto block=scale*scale;
 if(block==1){
  if(alpha==255)out=old;
  return;
 }
 for(unsigned y=0;y<Height;y+=block)for(unsigned x=0;x<Width;x+=block){
  unsigned right=std::min(Width,x+block),bottom=std::min(Height,y+block);
  const auto sampleX=std::min(Width-1,x+block/2);
  const auto sampleY=std::min(Height-1,y+block/2);
  size_t sample=(size_t(sampleY)*Width+sampleX)*4;
  std::array<uint8_t,4> color;
  for(unsigned c=0;c<4;c++)color[c]=blendChannel(0,old[sample+c],out[sample+c],alpha);
  for(unsigned row=y;row<bottom;row++)for(unsigned col=x;col<right;col++){
   auto at=(size_t(row)*Width+col)*4;
   std::copy(color.begin(),color.end(),out.begin()+at);
  }
 }
}
}
SceneTransition::SceneTransition(qlie::Assets&assets):assets(assets){std::istringstream in(assets.text("avg/imoscripter_template_sentence.txt"));std::string line;
 while(std::getline(in,line)){auto pos=line.find('=');if(pos!=line.npos)templates[trim(line.substr(0,pos))]=trim(line.substr(pos+1));}
}
SceneTransition::~SceneTransition()=default;
void SceneTransition::reset(){
 if(mode==Mode::Animation&&animation)animation->removeUiLayer(AnimationLayerName);
 previous={};incoming={};effectPath.clear();ruleMode=4;mode=Mode::Overlap;
 elapsed=duration=0;staged=active=caught=false;
}
std::string SceneTransition::ruleKey(const std::string&path){return "@rule:"+qlie::canonical(path);}
void SceneTransition::warmRule(const std::string&path,bool required){
 if(!readAhead)return;
 readAhead->request(ruleKey(path),[&a=assets,path]{
  WarmAsset prepared;
  prepared.bitmap=readRule(a,path);
  prepared.bytes=prepared.bitmap->pixels.size();
  return prepared;
 },required);
}
void SceneTransition::loadRule(const std::string&path){
 if(cachedRulePath==path)return;
 if(readAhead)if(auto prepared=readAhead->takeReady(ruleKey(path))){
  rule=std::move(prepared->bitmap);cachedRulePath=path;return;
 }
 rule=readRule(assets,path);cachedRulePath=path;
}
void SceneTransition::loadAnimation(const std::string&path){
 if(cachedAnimationPath!=path){
  auto members=abmp(assets.read(path));
  auto metadata=std::find_if(members.begin(),members.end(),[](const Member&m){return m.type.rfind("abdata",0)==0;});
  if(metadata==members.end())throw std::runtime_error("Missing transition animation metadata "+path);
  auto definition=whj2::animation(metadata->type,metadata->data);
  if(definition.bounds[0]!=int(Width)||definition.bounds[1]!=int(Height)||definition.motions.empty())
   throw std::runtime_error("Transition animation dimensions "+path);
  captureAnimationTarget=false;
  std::istringstream comment(definition.comment);
  for(std::string field;std::getline(comment,field);)if(trim(field)=="*CaptureFadein=1")captureAnimationTarget=true;
  // abdata15 stores the default action's playback end on the motion.
  // Earlier headers keep it in the bounds, with abdata14 placing start first.
  animationLength=metadata->type=="abdata15"?definition.motions.front().loopEnd:
   definition.bounds[definition.loopStartFirst?3:2];
  if(!animation)animation=std::make_unique<SceneImages>(assets);
  else animation->clear();
  cachedAnimationPath=path;
 }
 animation->command("^ui_transition,file:"+path+",x:0,y:0");
 if(!previous.pixels.empty())animation->uiSlot(AnimationLayerName,0,previous);
 if(captureAnimationTarget&&!incoming.pixels.empty())animation->uiSlot(AnimationLayerName,1,incoming);
}
void SceneTransition::start(Mode newMode,double milliseconds){
 mode=newMode;duration=milliseconds;caught=false;elapsed=0;staged=true;active=false;
}
void SceneTransition::startAnimation(const std::string&path,double milliseconds,const qlie::Bytes&scene){
 reset();
 if(milliseconds<=0)return;
 if(scene.size()!=FrameBytes)throw std::runtime_error("Transition scene dimensions");
 previous={Width,Height,scene};previous.version.publish();
 effectPath=path;loadAnimation(effectPath);start(Mode::Animation,milliseconds);present();
}
bool SceneTransition::needsTarget()const{return active&&mode==Mode::Animation&&captureAnimationTarget&&incoming.pixels.empty();}
bool SceneTransition::needsCanvas()const{return active&&mode==Mode::Mosaic;}
void SceneTransition::captureTarget(const qlie::Bytes&scene){
 if(!needsTarget())return;
 if(scene.size()!=FrameBytes)throw std::runtime_error("Transition target dimensions");
 incoming={Width,Height,scene};
 animation->uiSlot(AnimationLayerName,1,incoming);
}
bool SceneTransition::command(const std::string&line,const qlie::Bytes&scene,SentenceWait*wait){
 return commandWithCapture(line,[&scene]{return scene;},wait);
}
std::vector<std::vector<std::string>> SceneTransition::sentenceFields(const std::string&line)const{
 auto comma=line.find(',');if(comma==line.npos)return {};
 auto args=line.substr(comma+1);
 std::vector<std::pair<std::string,std::string>> values(templates.begin(),templates.end());std::sort(values.begin(),values.end(),[](auto&a,auto&b){return a.first.size()>b.first.size();});
 for(unsigned pass=0;pass<12;pass++){bool changed=false;for(auto&item:values){size_t pos=0;while((pos=args.find(item.first,pos))!=args.npos){args.replace(pos,item.first.size(),item.second);pos+=item.second.size();changed=true;}}if(!changed)break;if(pass==11)throw std::runtime_error("Sentence template recursion");}
 std::vector<std::vector<std::string>> fields;
 std::string fadeMode,fadeTimeField,fadeFile,fadePower,waitMode,waitTimeField,skipTime;
 for(const auto&arg:split(args,',')){
  auto property=split(arg,':');
  if(property.size()>1){
   const auto&key=property[0];
   auto&value=property[1];
   if(key=="fademode"){fadeMode=value;continue;}
   if(key=="fadetime"){fadeTimeField=value;continue;}
   if(key=="fadefile"){
    fadeFile=value;
    if(property.size()>2)fadePower=property[2];
    continue;
   }
   if(key=="fadepow"){fadePower=value;continue;}
   if(key=="waitmode"){waitMode=value;continue;}
   if(key=="waittime"){waitTimeField=value=="infinity"?"$FFFFFF":value;continue;}
   if(key=="skiptime"){skipTime=value;continue;}
  }
  fields.push_back(std::move(property));
 }
 if(!fadeMode.empty()){
  std::vector<std::string> fade{"fade",fadeMode,fadeTimeField,fadeFile};
  if(!fadePower.empty())fade.push_back(fadePower);
  fields.push_back(std::move(fade));
 }
 if(!waitMode.empty())fields.push_back({"wait",waitMode,waitTimeField,skipTime});
 return fields;
}
std::vector<std::string> SceneTransition::ruleAssets(const std::string&line)const{
 std::vector<std::string> paths;
 auto last=cachedRulePath;
 for(const auto&fields:sentenceFields(line)){
  if(!hasFadeAsset(fields)||fields[0]!="fade"||fields[1]!="rule"||fadeTime(fields)<=0)continue;
  auto path=fadeAsset(assets,fields[3]);
  if(path!=last&&std::find(paths.begin(),paths.end(),path)==paths.end())paths.push_back(path);
  last=std::move(path);
 }
 return paths;
}
bool SceneTransition::commandWithCapture(const std::string&line,const std::function<qlie::Bytes()>&capture,SentenceWait*wait,bool animate){
 if(line.rfind("^sentence",0)!=0)return false;
 if(line.find(',')==line.npos)return true;
 const auto fieldsList=sentenceFields(line);
 // A command can contain more than one fade/clear. Determine its snapshot
 // demand before mutating the old transition, then capture the original once.
 unsigned uses=0;bool captured=caught;
 auto timedMode=[](const std::string&name){return name=="overlap"||name=="rule"||name=="mosaic"||name=="anime";};
 if(animate)for(const auto&fields:fieldsList){
  if(fields.empty())continue;
  if(fields[0]=="clear"&&fields.size()>1&&fields[1]=="def")captured=false;
  if(fields[0]=="fade"){
   auto mode=fields.size()>1?fields[1]:"cut";
   if(mode=="catch"){++uses;captured=true;}
   else if(mode=="cut")captured=false;
   else if(timedMode(mode)){if(fadeTime(fields)>0&&!captured)++uses;captured=false;}
  }
 }
 qlie::Bytes scene;
 if(uses){scene=capture();if(scene.size()!=FrameBytes)throw std::runtime_error("Transition scene dimensions");}
 auto storeSnapshot=[&]{
  --uses;
  if(uses)previous={Width,Height,scene};else previous={Width,Height,std::move(scene)};
  previous.version.publish();
 };
 bool explicitWait=false,hasFade=false;double waitFadeTime=0;
 for(auto&fields:fieldsList){
  if(wait&&wait->property(fields)){if(fields[0]=="wait")explicitWait=true;continue;}
  if(fields[0]=="fade"){hasFade=true;
   if(!animate){reset();continue;}
   auto name=fields.size()>1?fields[1]:"cut";
   waitFadeTime=fadeTime(fields);
   if(name=="cut"){reset();continue;}
   if(name=="catch"){reset();storeSnapshot();caught=true;continue;}
   if(!timedMode(name)){QLIE_LOG_LINE(<<"PENDING-SENTENCE fade="<<name<<"\n");continue;}
   if(waitFadeTime<=0){reset();continue;}
   auto nextMode=Mode::Overlap;
   const bool hasAsset=hasFadeAsset(fields);
   if(mode==Mode::Animation&&animation)animation->removeUiLayer(AnimationLayerName);
   incoming={};effectPath.clear();ruleMode=4;
   if(name=="rule"&&hasAsset){
    nextMode=Mode::Rule;effectPath=fadeAsset(assets,fields[3]);
    loadRule(effectPath);
    if(fields.size()>4&&!fields[4].empty())ruleMode=unsigned(std::stoul(fields[4][0]=='$'?fields[4].substr(1):fields[4],nullptr,fields[4][0]=='$'?16:10));
    if(ruleMode>255)throw std::runtime_error("Transition rule mode range");
   }
   if(!caught)storeSnapshot();
   if(name=="mosaic")nextMode=Mode::Mosaic;
   if(name=="anime"){
    if(!hasAsset)throw std::runtime_error("Missing sentence animation name");
    effectPath=fadeAsset(assets,fields[3]);loadAnimation(effectPath);nextMode=Mode::Animation;
   }
   start(nextMode,waitFadeTime);
  }else if(fields[0]=="clear"&&fields.size()>1&&fields[1]=="def"){
   reset();
   if(wait)wait->resetProperties();
   explicitWait=true;
  }
  else if(fields[0]!="")QLIE_LOG_LINE(<<"PENDING-SENTENCE property="<<fields[0]<<"\n");
 }if(wait&&hasFade&&!explicitWait)wait->fromFade(waitFadeTime);return true;
}
void SceneTransition::present(){if(staged){staged=false;active=true;}}
void SceneTransition::update(double ms){
 if(!active)return;
 auto increment=std::min(duration-elapsed,std::max(0.,ms));
 elapsed+=increment;
 if(elapsed>=duration){reset();return;}
 if(mode==Mode::Animation)animation->update(increment*(animationLength>0?animationLength/duration:1));
}
bool SceneTransition::blend(qlie::Bytes&out)const{if(!active||previous.pixels.empty())return false;if(out.size()!=previous.pixels.size())throw std::runtime_error("Transition frame dimensions");
 double amount=duration>0?std::clamp(elapsed/duration,0.,1.):1;
 if(mode==Mode::Animation){
  if(needsTarget())out=previous.pixels;
  else return animation->draw(out);
  return false;
 }
 if(mode==Mode::Mosaic){mosaic(out,previous.pixels,amount);return false;}
 if(mode==Mode::Overlap){for(size_t i=0;i<out.size();i++)out[i]=uint8_t(previous.pixels[i]*(1-amount)+out[i]*amount);return false;}
 auto opacity=uint8_t(255*(1-amount));
 for(unsigned y=0;y<Height;y++)for(unsigned x=0;x<Width;x++){
  size_t at=(size_t(y)*Width+x)*4;unsigned mx=unsigned(uint64_t(x)*rule->width/Width),my=unsigned(uint64_t(y)*rule->height/Height);
  auto alpha=gradientMaskAlpha(255,rule->pixels[(size_t(my)*rule->width+mx)*4],opacity,uint8_t(ruleMode));
  for(unsigned c=0;c<4;c++)out[at+c]=blendChannel(0,previous.pixels[at+c],out[at+c],alpha);
 }
 return false;
}
void SceneTransition::blend(RenderTarget&target)const{
 if(!active||previous.pixels.empty())return;
 if(mode==Mode::Animation&&!needsTarget()){animation->draw(target);return;}
 if(needsCanvas())throw std::runtime_error("Mosaic transition requires the composited canvas");
 double amount=needsTarget()?0:std::clamp(elapsed/duration,0.,1.);
 target.transition(previous,mode==Mode::Rule?rule.get():nullptr,amount,ruleMode);
}
qlie::Bytes SceneTransition::save()const{
 const bool extended=mode==Mode::Mosaic||mode==Mode::Animation;
 StateWriter w;w.str(extended?"WHJ2-transition-3":"WHJ2-transition-2");
 w.n(uint64_t(elapsed*1000));w.n(uint64_t(duration*1000));w.n(staged);w.n(active);w.n(caught);
 w.str(effectPath);w.n(ruleMode);
 if(extended)w.n(unsigned(mode));
 auto writeFrame=[&](const Image&frame){
  w.n(frame.pixels.size());w.bytes.insert(w.bytes.end(),frame.pixels.begin(),frame.pixels.end());
 };
 writeFrame(previous);
 if(extended)writeFrame(incoming);
 return w.bytes;
}
void SceneTransition::restore(const qlie::Bytes&bytes){
 StateReader r{bytes};auto version=r.str();
 if(version!="WHJ2-transition-1"&&version!="WHJ2-transition-2"&&version!="WHJ2-transition-3")throw std::runtime_error("Transition state version");
 auto time=r.n()/1000.,span=r.n()/1000.;bool waiting=r.n()!=0,running=r.n()!=0,captured=r.n()!=0;
 std::string path;unsigned savedRuleMode=4;
 if(version!="WHJ2-transition-1"){
  path=r.str();auto flags=r.n();if(flags>255)throw std::runtime_error("Saved rule mode");savedRuleMode=unsigned(flags);
 }
 auto savedMode=path.empty()?Mode::Overlap:Mode::Rule;
 if(version=="WHJ2-transition-3"){
  auto id=r.n();if(id>unsigned(Mode::Animation))throw std::runtime_error("Saved transition mode");savedMode=Mode(id);
 }
 auto readFrame=[&]()->Image{
  auto size=r.n();
  if((size!=0&&size!=FrameBytes)||size>bytes.size()-r.p)throw std::runtime_error("Transition saved dimensions");
  if(!size)return {};
  Image frame{Width,Height,qlie::Bytes(bytes.begin()+r.p,bytes.begin()+r.p+size)};
  r.p+=size;frame.version.publish();return frame;
 };
 auto old=readFrame();Image target;
 if(version=="WHJ2-transition-3")target=readFrame();
 r.end();
 if(time>span)throw std::runtime_error("Transition state bounds");
 reset();previous=std::move(old);incoming=std::move(target);mode=savedMode;
 effectPath=std::move(path);ruleMode=savedRuleMode;elapsed=time;duration=span;
 staged=waiting;active=running;caught=captured;
 if(mode==Mode::Rule)loadRule(effectPath);
 if(mode==Mode::Animation){
  loadAnimation(effectPath);
  if(duration>0)animation->update(elapsed*(animationLength>0?animationLength/duration:1));
 }
}
}
