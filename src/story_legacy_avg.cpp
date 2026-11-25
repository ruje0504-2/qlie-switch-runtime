// SPDX-License-Identifier: GPL-2.0-or-later
#include "story.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace whj2 {
namespace {
std::string arg(const std::vector<std::string>& fields,size_t at){
 return at<fields.size()?fields[at]:std::string();
}
std::string background(std::string_view id){
 if(id=="bg1"||id=="bg2"||id=="bg3"||id=="bg4")return "bg0"+std::string(id.substr(2));
 if(id=="motion")return "ui_avg1_motion";
 return {};
}
unsigned priority(std::string_view layer){
 if(layer=="bg01")return 10;
 if(layer=="bg02")return 100;
 if(layer=="bg03")return 600;
 if(layer=="bg04")return 700;
 return 0;
}
std::string track(std::string_view id){
 if(id=="bgm1")return "music01";
 if(id=="se1")return "se01";
 if(id=="se2")return "se02";
 if(id=="env1")return "se03";
 if(id=="env2")return "sebgv";
 return {};
}
std::string firstFile(const qlie::Assets& assets,std::string base,const std::vector<const char*>& extensions){
 if(assets.exists(base))return base;
 for(const auto*ext:extensions)if(assets.exists(base+ext))return base+ext;
 return {};
}
std::string legacyPortrait(const qlie::Assets& assets,const std::string& id,const std::string& basename){
 const std::vector<const char*> extensions{".png",".argb",".bmp",".b",".jpg"};
 auto path=firstFile(assets,"charaimage/"+id+"/"+basename,extensions);
 if(path.empty())path=firstFile(assets,"charaimage/default/"+basename,extensions);
 if(path.empty())throw std::runtime_error("Missing AVG character picture "+basename);
 return path;
}
std::string characterId(const std::string& id){
 auto end=id.find('(');
 const auto wide=id.find("（");
 if(wide<end)end=wide;
 return id.substr(0,end);
}
std::string characterLayer(std::string_view slot){
 return "ui_avg1_chara"+std::string(slot);
}
std::string audioLeaf(const std::string& name){
 const auto canonical=qlie::canonical(name);
 const auto slash=canonical.rfind('/');
 return slash==canonical.npos?canonical:canonical.substr(slash+1);
}
void appendPictureReplacements(std::string& command,const std::string& files){
 const auto replacements=split(files,';');
 for(size_t i=0;i<std::min<size_t>(8,replacements.size());++i)if(!replacements[i].empty())
  command+=",rep"+std::to_string(i)+":image/"+qlie::canonical(replacements[i]);
}
}

std::string Story::legacyVideoPath(std::string path)const{
 path=qlie::canonical(std::move(path));
 if(path.find('/')==path.npos)path="movie/"+path;
 const auto& renamed=profile.legacyAvg->renamedMovie;
 if(path==renamed.first)return renamed.second;
 return path;
}

std::string Story::legacyAssetHint(const std::string& text,std::string& scope)const{
 const auto comma=text.find(',');
 if(comma==text.npos)return {};
 const auto cmd=std::string_view(text).substr(0,comma);
 if(cmd=="\\scp"){scope=qlie::canonical(arg(split(text,','),1));return {};}
 if(cmd=="\\l"&&(scope=="th"||scope=="th2")){
  const auto filename=arg(split(text,','),1);
  if(filename.size()<2||filename.front()!='"'||filename.back()!='"')return {};
  const auto path=legacyVideoPath(filename.substr(1,filename.size()-2));
  if(path==profile.legacyAvg->creditsFile)return "^movie,"+path;
  return "^ui_avg1_video_"+scope+",file:"+path;
 }
 if(cmd!="^iload"&&cmd!="^mload"&&cmd!="^sload"&&cmd!="^ffade")return {};
 const auto fields=split(text,',');
 const auto id=qlie::canonical(arg(fields,1));
 auto filename=arg(fields,2);
 if(filename.size()>=2&&filename.front()=='"'&&filename.back()=='"')
  filename=filename.substr(1,filename.size()-2);
 if(cmd=="^ffade"&&id=="blind"){
  auto duration=arg(fields,3);
  if(duration.empty())duration="500";
  return "^sentence,fade:rule:"+duration+":"+(filename.empty()?"blind":filename);
 }
 if(cmd=="^ffade"&&id=="ex1"&&!filename.empty()){
  auto duration=arg(fields,3);
  if(duration.empty())duration="500";
  return "^sentence,fade:anime:"+duration+":"+filename;
 }
 if(filename.empty()||filename=="@"||filename.find_first_of("#$+")!=filename.npos)return {};
 if(cmd=="^iload"){
  const auto layer=background(id);
  if(layer.empty())return {};
  auto command="^"+layer+",file:"+qlie::canonical(filename);
  appendPictureReplacements(command,arg(fields,3));
  return command;
 }
 if(cmd=="^mload"||cmd=="^sload"){
  const auto channel=track(id);
  if(!channel.empty())return "^"+channel+",file:"+audioLeaf(filename);
 }
 return {};
}

std::string legacyAvgMessageWindow(const LegacyAvgLayout&layout){
 const auto size=std::to_string(layout.messageFontSize);
 return "^message,file:"+std::string(layout.messageImage)+",x:"+std::to_string(layout.messageX)+
  ",y:"+std::to_string(layout.messageY)+",pri:1010,textarea:"+std::to_string(layout.messageWidth)+
  ":"+std::to_string(layout.messageHeight)+",fontsize:"+size+":"+size;
}

bool Story::legacyCaret(const Op&o,const std::vector<std::string>& a){
 const auto cmd=qlie::canonical(a[0]);
 const auto field=[&](size_t i){return value(arg(a,i));};
 const auto emit=[&](std::string text){state.pending.push_back(event("command",std::move(text),o.line));};
 const auto optional=[&](size_t i,const char* fallback){auto v=field(i);return v.empty()?std::string(fallback):v;};
 if(cmd=="^savedate"||cmd=="^saveroute"||cmd=="^savescene"){
  state.vars["__avg1_"+cmd.substr(1)]=field(1);
  return true;
 }
 if(cmd=="^we"||cmd=="^wd"){
  emit(legacyAvgMessageWindow(*profile.legacyAvg)+",show:"+(cmd=="^we"?"true":"false"));
  return true;
 }
 if(cmd=="^facewindow"){
  state.vars["__avg1_facewindow"]=field(1);
  emit(std::string(profile.legacyAvg->faceWindow)+",show:"+(field(1)=="1"?"true":"false"));
  return true;
 }
 if(cmd=="^ffade"){
  const auto kind=qlie::canonical(field(1));
  const auto duration=optional(3,kind=="cut"?"0":"500");
  const auto mode=optional(4,"6");
  if(kind=="cut")emit("^sentence,fade:cut");
  else if(kind=="blind")emit("^sentence,fade:rule:"+duration+":"+optional(2,"blind")+":"+mode);
  else if(kind=="overlap")emit("^sentence,fade:overlap:"+duration);
  else if(kind=="ex1"){
   auto sentence="^sentence,fade:anime:"+duration+":"+field(2);
   // AVG starts the animated fade at the message wait, then runs a clickable wait.
   if(optional(5,"1")=="1")sentence+=",wait:click:"+duration+":0";
   emit(std::move(sentence));
  }
  else return false;
  if(state.vars.erase("__avg1_fadeout"))emit("^ui_avg1_fade,file:none");
  return true;
 }
 if(cmd=="^eeffect"){
  const auto id=qlie::canonical(field(1));
  const auto file=optional(2,id.c_str());
  if(id=="stop"||file=="@")emit("^ui_avg1_effect,file:none");
  else {
   const auto path=firstFile(assets,"effect/"+file,{".b"});
   if(path.empty())throw std::runtime_error("Missing AVG effect "+file);
   emit("^ui_avg1_effect,file:"+path+",x:0,y:0,pri:980,show:true");
  }
  return true;
 }
 if(cmd=="^iload"||cmd=="^iinit"){
  const auto layer=background(qlie::canonical(field(1)));
  if(layer.empty())return false;
  const auto file=field(2);
  std::string text="^"+layer;
  if(cmd=="^iinit"||file=="@")text+=",file:none";
  else if(!file.empty())text+=",file:"+qlie::canonical(file);
  const auto key="__avg1_layer_"+layer;
  if(cmd=="^iinit"||!state.vars.count(key)){
   text+=",x:640,y:360,scalex:100,scaley:100,pri:"+std::to_string(priority(layer))+",show:true";
   state.vars[key]="1";
  }
  appendPictureReplacements(text,field(3));
  emit(std::move(text));
  return true;
 }
 if(cmd=="^mload"||cmd=="^mvol"||cmd=="^sload"||cmd=="^svol"||cmd=="^sinit"){
  const auto id=qlie::canonical(field(1));
  const auto channel=track(id);
  if(channel.empty())return false;
  const bool loopByDefault=id=="bgm1"||id=="env1"||id=="env2";
  const auto defaultFade=loopByDefault?"500":"0";
  const auto defaultLoop=loopByDefault?"1":"0";
  const auto gainKey="__avg1_gain_"+id;
  const auto fadeKey="__avg1_volume_fade_"+id;
  const auto loopKey="__avg1_loop_"+id;
  const auto crossfadeKey="__avg1_crossfade_"+id;
  if(cmd=="^sinit"){
   for(const auto* key:{&gainKey,&fadeKey,&loopKey,&crossfadeKey})state.vars.erase(*key);
   emit("^"+channel+",clear:def");
   return true;
  }
  auto&gain=state.vars[gainKey];
  if(gain.empty())gain="100";
  auto&fade=state.vars[fadeKey];
  if(fade.empty())fade=defaultFade;
  if(cmd=="^svol"||cmd=="^mvol"){
   gain=optional(2,gain.c_str());
   fade=optional(3,fade.c_str());
   emit("^"+channel+",vol:"+gain+",oncetime:"+fade);
   return true;
  }
  auto&loop=state.vars[loopKey];
  if(loop.empty())loop=defaultLoop;
  loop=optional(3,loop.c_str());
  const auto name=field(2);
  auto playFade=fade;
  if(cmd=="^mload"){
   auto&crossfade=state.vars[crossfadeKey];
   if(crossfade.empty())crossfade="2000";
   crossfade=optional(4,crossfade.c_str());
   playFade=crossfade;
  }
  if(!name.empty())emit("^"+channel+",file:"+(name=="@"?"none":audioLeaf(name))+
                       ",vol:"+gain+",loop:"+(loop=="0"?"false":"true")+
                       ",oncetime:"+playFade+",voltype:"+(id=="bgm1"?"bgm":"se"));
  return true;
 }
 if(cmd=="^cadd"||cmd=="^cdel"||cmd=="^cinit"||cmd=="^ce"||cmd=="^cd"||cmd=="^cbase"||cmd=="^cface"){
  auto id=field(1);
  if(id.empty())id=state.vars["__avg1_previous_chara"];
  if(id.empty())throw std::runtime_error("Missing AVG character identifier");
  state.vars["__avg1_previous_chara"]=id;
  const auto key="__avg1_slot_"+id;
  auto found=state.vars.find(key);
  if(found==state.vars.end()){
   if(cmd=="^cdel"||cmd=="^cd")return true;
   if(cmd!="^cadd"&&cmd!="^cinit")throw std::runtime_error("Undeclared AVG character "+id);
   std::array<bool,16> occupied{};
   for(const auto&entry:state.vars)if(entry.first.rfind("__avg1_slot_",0)==0)occupied.at(std::stoul(entry.second))=true;
   auto free=std::find(occupied.begin(),occupied.end(),false);
   if(free==occupied.end())throw std::runtime_error("No free AVG character slots");
   found=state.vars.emplace(key,std::to_string(free-occupied.begin())).first;
   emit("^"+characterLayer(found->second)+",x:640,y:720,pri:520,align:center:bottom,show:true");
  }
  const auto layer=characterLayer(found->second);
  const auto storage="__avg1_actor_"+id+"/";
  auto& distance=state.vars[storage+"distance"];
  auto& base=state.vars[storage+"base"];
  auto& face=state.vars[storage+"face"];
  auto& shown=state.vars[storage+"show"];
  const auto actionKey=storage+"action";
  if(distance.empty())distance="中";
  if(shown.empty())shown="true";
  if(cmd=="^cadd")return true;
  if(cmd=="^cdel"||cmd=="^cinit"){
   emit("^"+layer+",file:none,show:false");
   if(cmd=="^cdel")state.vars.erase(found);
   for(const auto* fieldName:{"distance","base","face","show","action"})state.vars.erase(storage+fieldName);
   return true;
  }
  const auto catchCharacterFade=[&]{
   if(const auto duration=profile.legacyAvg->characterFadeMs)
    state.pending.push_back(event("legacy-character-fade","^sentence,fade:overlap:"+std::to_string(duration),o.line));
  };
  if(cmd=="^ce"||cmd=="^cd"){
   const std::string next=cmd=="^ce"?"true":"false";
   if(shown!=next&&!face.empty()&&face!="@")catchCharacterFade();
   shown=next;
   emit("^"+layer+",show:"+shown);
   return true;
  }
  if(cmd=="^cbase"){
   if(!field(2).empty())distance=field(2);
   if(!field(3).empty())base=field(3);
   return true;
  }
  const auto expression=field(2);
  const auto action=field(3);
  if(!expression.empty()){
   if((action.empty()||action=="@")&&shown=="true"&&face!=expression)catchCharacterFade();
   face=expression;
  }
  const auto actor=characterId(id);
  const auto sprite=face.empty()||face=="@"?std::string{}:[&]{
   std::string basename=actor+"_"+distance+"_";
   if(!base.empty()&&base!="@"){
    auto costume=base;
    std::replace(costume.begin(),costume.end(),';','_');
    basename+=costume+"_";
   }
   return legacyPortrait(assets,actor,basename+face);
  }();
  auto text="^"+layer+",id:"+actor+",show:"+shown;
  if(!action.empty()){
   const auto filename=(action=="@"?"default":action)+std::string(".b");
   const auto path=legacyPortrait(assets,actor,filename);
   state.vars[actionKey]=path;
   text+=",file:"+path+",motion:0";
  }
  if(state.vars.count(actionKey)){
   text+=",rep0:"+(sprite.empty()?"none":sprite);
  }else{
   text+=",file:"+(sprite.empty()?"none":sprite);
  }
  emit(std::move(text));
  return true;
 }
 return false;
}

bool Story::legacyNative(const Op&o,const std::vector<std::string>& a){
 auto cmd=a[0];
 for(auto& ch:cmd)if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');
 const auto field=[&](size_t i){return value(arg(a,i));};
 const auto emit=[&](std::string text){state.pending.push_back(event("command",std::move(text),o.line));};
 const auto isVideo=state.scope=="th"||state.scope=="th2";
 const auto direct=state.scope=="snd[12]";
 const auto imageScope=state.scope.rfind("avg_image[",0)==0;
 const auto charaScope=state.scope=="avg_chara[resultint[0]]";
 const auto opImage=state.scope=="op_image";
 std::string layer;
 if(imageScope&&state.scope.size()==12){
  const auto index=state.scope[10];
  if(index>='0'&&index<='3')layer="bg0"+std::to_string(index-'0'+1);
 }else if(charaScope)layer=characterLayer(state.vars["resultint[0]"]);
 else if(opImage)layer="ui_avg1_op_image";
 else if(isVideo)layer="ui_avg1_video_"+state.scope;

 if(cmd=="\\sub"&&qlie::canonical(field(1))=="@@!avg_charagetno"){
  const auto at=state.vars.find("__avg1_slot_"+field(3));
  if(at==state.vars.end())throw std::runtime_error("Missing AVG character slot "+field(3));
  state.vars["resultint[0]"]=at->second;
  return true;
 }
 const auto clearVideo=[&](const std::string&name){
  emit("^ui_avg1_video_"+name+",file:none");
  state.vars.erase("__avg1_file_"+name);
 };
 if(cmd=="\\del"){
  const auto name=qlie::canonical(arg(a,1));
  if(name=="op_image"){
   emit("^ui_avg1_op_image,file:none");
   state.vars.erase(name);
   return true;
  }
  if(name=="th"||name=="th2"){
   clearVideo(name);
   state.vars.erase(name);
   state.vars.erase("__avg1_layer_ui_avg1_video_"+name);
   return true;
  }
 }
 if(isVideo&&cmd=="\\i"){clearVideo(state.scope);return true;}
 if(isVideo&&cmd=="\\l"){
  state.vars["__avg1_file_"+state.scope]=legacyVideoPath(field(1));
  return true;
 }
 if(isVideo&&cmd=="\\pp"){
  state.vars["__avg1_layer_"+layer]=",x:"+field(1)+",y:"+field(2)+",pri:"+field(3);
  return true;
 }
 if(isVideo&&cmd=="\\p"){
  const auto path=state.vars.at("__avg1_file_"+state.scope);
  if(field(1)=="0"&&path==profile.legacyAvg->creditsFile){
   state.vars["__avg1_credits_clock"]="1";
   emit("^movie,"+path);
  }else emit("^"+layer+",file:"+path+state.vars["__avg1_layer_"+layer]);
  return true;
 }
 if(isVideo&&(cmd=="\\ss"||cmd=="\\vtn"||cmd=="\\v"))return true;
 if(state.vars.count("__avg1_credits_clock")){
  // The next waits run concurrently with the source film. Movie already
  // holds story execution and applies the profile's skip guard.
  if(cmd=="\\wait"&&field(1)==std::to_string(profile.legacyAvg->creditsSkipDelay))return true;
  if(cmd=="\\clk"&&field(1)==std::to_string(profile.legacyAvg->creditsClickWait)){
   state.vars.erase("__avg1_credits_clock");
   return true;
  }
 }
 if(direct){
  if(cmd=="\\i"){emit("^se02,clear:def");return true;}
  if(cmd=="\\t"||cmd=="\\p")return true;
  if(cmd=="\\v"){
   emit("^se02,vol:"+std::to_string(std::stoul(field(1))*100/256)+",oncetime:"+(field(2).empty()?"0":field(2)));
   return true;
  }
  if(cmd=="\\l"){
   const auto path=qlie::canonical(field(1));
   emit("^se02,file:"+path+",voltype:voice");
   return true;
  }
 }
 if(state.scope=="eff"&&cmd=="\\fdo"){
  emit("^sentence,fade:overlap:"+field(2));
  emit("^ui_avg1_fade,file:image/0_black.png,x:0,y:0,pri:995,show:true");
  state.vars["__avg1_fadeout"]="1";
  return true;
 }
 if(!layer.empty()){
  if(cmd=="\\l"){
   emit("^"+layer+",file:"+qlie::canonical(field(1))+",show:false");
   return true;
  }
  if(cmd=="\\li"){
   emit("^"+layer+",rep"+field(1)+":"+qlie::canonical(field(2)));
   return true;
  }
  if(cmd=="\\e"||cmd=="\\d"){
   emit("^"+layer+",show:"+(cmd=="\\e"?"true":"false"));
   return true;
  }
  if(cmd=="\\m"){
   emit("^"+layer+",motion:"+field(1));
   return true;
  }
  if(cmd=="\\mm"){
   if(field(1)!="3")return false;
   emit("^"+layer+",move:square,movepow:"+field(2));
   return true;
  }
  if(cmd=="\\p"||cmd=="\\p2"){
   std::string text="^"+layer+",x:"+field(1)+",y:"+field(2)+",pri:"+field(3);
   if(cmd=="\\p2"){
    text+=",scalex:"+field(4)+",scaley:"+field(5);
    if(!field(6).empty())text+=",time:"+field(6);
   }
   emit(std::move(text));
   return true;
  }
  if(cmd=="\\exv"){
   const auto index=field(1);
   if(index.size()!=1||index.front()<'0'||index.front()>'9')
    throw std::runtime_error("Invalid AVG image external value index "+index);
   emit("^"+layer+",value"+index+":"+field(2));
   return true;
  }
 }
 return false;
}
}
