// SPDX-License-Identifier: GPL-2.0-or-later
#include "story.hpp"
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <charconv>
namespace whj2 {
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==s.npos)return {};return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
std::vector<std::string> split(const std::string&s,char delim){std::vector<std::string>r;bool quote=false;size_t start=0;for(size_t i=0;i<s.size();i++){if(s[i]==34)quote=!quote;if(s[i]==delim&&!quote){r.push_back(trim(s.substr(start,i-start)));start=i+1;}}r.push_back(trim(s.substr(start)));return r;}
static std::string lower(std::string s){for(auto&c:s)if(c>=65&&c<=90)c+=32;return s;}
static std::string stem(std::string s){auto p=s.find_last_of("/\\");if(p!=s.npos)s.erase(0,p+1);p=s.rfind(46);if(p!=s.npos)s.erase(p);return s;}
static std::vector<std::string> commands(const std::string&s){std::vector<std::string>r;bool quote=false;size_t start=0;for(size_t i=0;i<s.size();i++){if(s[i]==34)quote=!quote;if(s[i]==92&&!quote){if(i>start)r.push_back(trim(s.substr(start,i-start)));start=i;}}if(start<s.size())r.push_back(trim(s.substr(start)));return r;}
Story::Script&Story::script(const std::string&n){
 std::string name=qlie::canonical(n);
 auto it=scripts.find(name);
 if(it!=scripts.end())return it->second;
 Script sc;
 std::istringstream in(assets.text(name));
 const auto visualWaits=profile.visualWaitParagraphs.find(name);
 bool pageContent=false,visualWait=false;
 unsigned ln=0;
 auto endParagraph=[&]{
  if(pageContent)sc.ops.push_back({"!page",ln});
  // A presentation boundary is metadata on the last command, so adding
  // it does not shift saved program counters or existing route labels.
  else if(visualWait&&!sc.ops.empty()&&sc.ops.back().text.front()=='^')
   sc.ops.back().after=After::VisualWait;
  pageContent=visualWait=false;
 };
 for(std::string line;std::getline(in,line);){
  ++ln;
  line=trim(line);
  if(line.empty()){endParagraph();continue;}
  if(line.rfind("//",0)==0)continue;
  if(line.rfind("@@@",0)==0){
   std::string inc=qlie::canonical(line.substr(3));
   if(!inc.empty()&&inc[0]==33)inc.erase(0,1);
   if(inc==profile.header||inc==profile.replayHeader)sc.ops.push_back({"!create",ln});
   else if(!profile.alternateHeader.empty()&&inc==profile.alternateHeader)sc.ops.push_back({"!create-alternate",ln});
   else if(inc==profile.footer)sc.ops.push_back({"!free",ln});
   // The direct-label form invokes the already active AVG for its choice.
   else if(inc==profile.selectionPrelude)continue;
   else if(!profile.replayPrelude.empty()&&inc==profile.replayPrelude){
    const auto lastSlash=name.rfind('/');
    const auto relative=lastSlash==name.npos?inc:name.substr(0,lastSlash+1)+inc;
    std::istringstream prelude(assets.text(relative));
    for(std::string command;std::getline(prelude,command);){
     command=trim(command);
     if(command.empty()||command.rfind("//",0)==0)continue;
     if(command.rfind("^include,",0)!=0)
      throw std::runtime_error("Unsupported command in replay prelude: "+relative);
     sc.ops.push_back({std::move(command),ln});
    }
   }
   else throw std::runtime_error("Unsupported story include "+line+" in "+name);
   continue;
  }
  if(line.rfind("@@",0)==0){
   sc.labels[lower(line)]=sc.ops.size();
   sc.labelsAt[sc.ops.size()].push_back(lower(line));
   continue;
  }
  if(line.front()==92){
   for(auto& command:commands(line))if(!command.empty())sc.ops.push_back({command,ln});
  }else{
   const auto inlineLabels=line.rfind("^select,",0)==0?line.find("^selectlabel,",8):line.npos;
   if(inlineLabels!=line.npos){
    sc.ops.push_back({line.substr(0,inlineLabels),ln});
    sc.ops.push_back({line.substr(inlineLabels),ln});
   }else sc.ops.push_back({line,ln});
   if(line.front()!=94)pageContent=true;
   else if(profile.presentCommandParagraphs){
    const auto command=lower(line.substr(1,line.find(',')-1));
    if(profile.sceneCommands.count(command)||command=="include")visualWait=true;
   }else if(visualWaits!=profile.visualWaitParagraphs.end()&&visualWaits->second.count(ln)&&
           line.rfind("^sentence,",0)==0)visualWait=true;
  }
 }
 endParagraph();
 if(!profile.interludeImage.empty()){
  const std::string fileField=",file:"+profile.interludeImage;
  auto usesInterlude=[&](const std::string& text){
   if(text.empty()||text[0]!='^')return false;
   auto field=text.find(fileField);
   if(field==text.npos)return false;
   auto end=field+fileField.size();
   return end==text.size()||text[end]==',';
  };
  auto setsWait=[](const std::string& text){return text.rfind("^sentence,wait:",0)==0;};
  // The opening uses wait then image. The chapter endings use image then
  // wait. Mark the second opcode so saved script and call positions stay valid.
  for(size_t i=1;i<sc.ops.size();++i){
   const auto& first=sc.ops[i-1].text;
   auto& second=sc.ops[i];
   if((usesInterlude(first)&&setsWait(second.text))||
      (setsWait(first)&&usesInterlude(second.text)))second.after=After::Interlude;
  }
 }
 if(sc.labels.count("@@main"))sc.labels["@@top"]=sc.labels["@@!top"]=0;
 std::vector<size_t>stack;
 for(size_t i=0;i<sc.ops.size();i++){auto&s=sc.ops[i].text;if(s.rfind("\\if,",0)==0||s.rfind("\\for",0)==0||(profile.legacyAvg&&s.rfind("\\case,",0)==0))stack.push_back(i);else if(s=="\\else"){if(stack.empty())throw std::runtime_error("Unmatched else");sc.ops[stack.back()].otherwise=int(i);}else if(s=="\\end"){if(stack.empty())throw std::runtime_error("Unmatched end in "+name);size_t start=stack.back();stack.pop_back();sc.ops[start].end=int(i);sc.ops[i].end=int(start);if(sc.ops[start].otherwise>=0)sc.ops[size_t(sc.ops[start].otherwise)].end=int(i);}}
 if(!stack.empty())throw std::runtime_error("Unclosed control block in "+name);
 return scripts.emplace(name,std::move(sc)).first->second;
}
static std::string legacyVariable(std::string name){
 if(name=="#1_route_a_f")return "#game_aclearflag";
 if(name=="#1_route_b_f")return "#game_bclearflag";
 return name;
}
std::string Story::value(std::string s)const{
 s=trim(s);
 if(profile.legacyAvg){
  const auto terms=split(s,'+');
  if(terms.size()>1){
   std::string text;
   long sum=0;
   bool numeric=true;
   for(const auto&term:terms){
    const auto resolved=value(term);
    text+=resolved;
    long number=0;
    const auto result=std::from_chars(resolved.data(),resolved.data()+resolved.size(),number);
    if(result.ec!=std::errc{}||result.ptr!=resolved.data()+resolved.size())numeric=false;
    else if(numeric)sum+=number;
   }
   return numeric?std::to_string(sum):text;
  }
 }
 if(s.size()>=2&&s.front()==34&&s.back()==34)return s.substr(1,s.size()-2);
 const auto name=profile.legacyAvg?legacyVariable(lower(s)):lower(s);
 if(name=="selffilename")return state.at.file;
 auto it=state.vars.find(name);
 if(it!=state.vars.end())return it->second;
 if(name=="__rootfile")return profile.root;
 if(profile.legacyAvg&&!s.empty()&&s.front()==35)return "0";
 return s;
}
bool Story::setPlayerName(std::string name){
 if(profile.nameEntryScript.empty())return false;
 name=trim(std::move(name));
 std::string fullWidth;
 unsigned length=0;
 for(size_t i=0;i<name.size();){
  const auto first=static_cast<unsigned char>(name[i]);
  if(++length>10||first<32||first==127)return false;
  if(first<128){
   const unsigned cp=first==32?0x3000:0xff01+first-33;
   fullWidth+=char(0xe0|(cp>>12));
   fullWidth+=char(0x80|((cp>>6)&0x3f));
   fullWidth+=char(0x80|(cp&0x3f));
   ++i;
   continue;
  }
  const unsigned bytes=first>=0xf0?4:first>=0xe0?3:2;
  if(first<0xc2||first>0xf4||i+bytes>name.size())return false;
  for(unsigned j=1;j<bytes;++j)if((name[i+j]&0xc0)!=0x80)return false;
  fullWidth.append(name,i,bytes);
  i+=bytes;
 }
 if(fullWidth.empty())return false;
 state.vars["_avg_username"]=std::move(fullWidth);
 return true;
}
std::string Story::substitutePlayerName(std::string line)const{
 const auto current=state.vars.find("_avg_username");
 if(profile.nameEntryScript.empty()||current==state.vars.end())return line;
 const std::string marker="＄ユーザー＄";
 for(size_t pos=0;(pos=line.find(marker,pos))!=line.npos;pos+=current->second.size())
  line.replace(pos,marker.size(),current->second);
 return line;
}
bool Story::condition(const std::string&s)const{for(const auto&op:std::vector<std::string>{"!=","==",">=","<=","<",">"}){auto p=s.find(op);if(p==s.npos)continue;auto a=value(s.substr(0,p)),b=value(s.substr(p+op.size()));if(op=="!=")return a!=b;if(op=="==")return a==b;long x=std::stol(a),y=std::stol(b);return op==">="?x>=y:op=="<="?x<=y:op=="<"?x<y:x>y;}throw std::runtime_error("Unsupported story condition: "+s);}
void Story::jump(std::string label,std::string file){label=lower(value(label));file=value(file);if(file.empty()||lower(file)=="self")file=state.at.file;else if(file.find(47)==file.npos&&file.find(92)==file.npos){auto p=state.at.file.rfind(47);file=state.at.file.substr(0,p+1)+file;}file=qlie::canonical(file);auto&sc=script(file);auto it=sc.labels.find(label);if(it==sc.labels.end())throw std::runtime_error("Missing label "+label+" in "+file);state.at={file,uint32_t(it->second)};visited.insert(file+":"+label);}
Event Story::event(std::string t,std::string v,unsigned ln)const{return {std::move(t),std::move(v),state.at.file,ln};}
void Story::resetState(){state={};state.vars["game_routeflag[0]"]="0";state.vars["_routefile"]=profile.root;state.vars["_selectfile"]=profile.selection;state.vars["resultstr[0]"]="";state.vars["resultint[0]"]="0";}
const std::vector<std::string>& Story::includeCommands(const std::string&name){
 auto found=includes.find(name);
 if(found!=includes.end())return found->second;
 std::istringstream in(assets.text(profile.includeDirectory+"/"+name+".txt"));
 std::vector<std::string> lines;
 std::string line;
 while(std::getline(in,line)){
  line=trim(std::move(line));
  if(!line.empty()&&line.rfind("//",0)!=0)lines.push_back(std::move(line));
 }
 return includes.emplace(name,std::move(lines)).first->second;
}
static std::string endingMovieCommand(const std::string&name){
 return "^movie,movie/"+qlie::canonical(name);
}
std::vector<std::string> Story::upcomingAssetCommands(bool opening){
 std::string file=opening?profile.root:state.at.file;
 std::vector<std::string> out;
 if(file.empty())return out;
 const Script*source=&script(file);
 size_t pc=opening?0:state.at.pc;
 if(opening){
  // The root dispatches into the first chapter via a literal script jump.
  // Resolve that path as a read hint without executing VM instructions.
  for(const auto&op:source->ops){
   if(op.text.rfind("\\jmp,",0)!=0)continue;
   auto args=split(op.text,',');
   if(args.size()<3||args[2].size()<3||args[2].front()!='"'||args[2].back()!='"')return out;
   auto next=qlie::canonical(args[2].substr(1,args[2].size()-2));
   if(!assets.exists(next))return out;
   source=&script(next);pc=0;break;
  }
 }
 const bool ending=!profile.endingMovieScript.empty()&&file==profile.endingMovieScript;
 auto collect=[&](const std::string&text){
  if(ending&&text.rfind("^movie,",0)==0){
   const auto media=state.vars.find("__ending_movie");
   if(media!=state.vars.end()&&!media->second.empty())out.push_back(endingMovieCommand(media->second));
   return;
  }
  const bool updatesPortrait=text.rfind("^chara",0)==0&&
    (text.find(",clear:")!=text.npos||text.find(",imgfilter")!=text.npos||
     text.find(",show:")!=text.npos||text.find(",hide:")!=text.npos||text.find(",rep")!=text.npos);
  const bool filtersBackground=text.rfind("^bg",0)==0&&text.find(",imgfilter")!=text.npos&&text.find("blur")!=text.npos;
  const bool resourceCommand=!text.empty()&&text.front()=='^'&&
    (text.find(",file")!=text.npos||text.find(",$")!=text.npos||text.find(",base:")!=text.npos);
  const bool rule=text.rfind("^sentence,",0)==0&&
    (text.find("fade:rule:")!=text.npos||text.find("fademode:rule")!=text.npos);
  if(text.rfind("^movie,",0)==0||updatesPortrait||filtersBackground||resourceCommand||rule||text.rfind("％",0)==0)
   out.push_back(text);
 };
 size_t end=std::min(source->ops.size(),pc+96);
 for(;pc<end;++pc){
  const auto&text=source->ops[pc].text;
  if(text.rfind("\\jmp,",0)==0||text.rfind("\\if,",0)==0)break;
  if(text.rfind("\\sub,",0)==0)break;
  if(text.rfind("^include,",0)==0){
   auto args=split(text,',');
   if(args.size()==2)for(const auto&line:includeCommands(args[1]))collect(line);
  }else collect(text);
 }
 return out;
}
bool Story::beforeNextPresentation(bool hasBody,const std::function<bool(const Event&)>&match){
 struct RestorePreview {
  Story& story;
  StoryState state;
  std::set<std::string> visited;
  uint64_t operations;
  ~RestorePreview(){
   story.state=std::move(state);
   story.visited=std::move(visited);
   story.operations=operations;
  }
 } restore{*this,state,std::move(visited),operations};
 visited.clear();
 for(;;){
  auto event=next();
  if(event.type=="command"){
   if(event.value.rfind("^movie,",0)==0)return false;
   if(match(event))return true;
  }else if(event.type=="text"){
   if(event.value.rfind("【",0)!=0&&event.value.rfind("％",0)!=0)hasBody=true;
  }else if(event.type=="page"){
   if(hasBody)return false;
  }else if(event.type=="visual-wait"||event.type=="interlude"||event.type=="wait"||
           event.type=="click-wait"||event.type=="choice"||event.type=="await-choice"||
           event.type=="finish"||event.type=="finished"||event.type=="create"||event.type=="free")return false;
 }
}
bool Story::historyJumpEnabled()const{
 const auto locked=state.vars.find("__log_jump_locked");
 return profile.historyJumpLimit&&(locked==state.vars.end()||locked->second=="0");
}
void Story::start(){
 resetState();
 if(profile.historyJumpLimit)state.vars["__log_jump_locked"]="1";
 state.at.file=profile.root;
 jump("@@top",state.at.file);
}
void Story::startReplay(const std::string&file,const std::string&label){
 resetState();state.replay=true;state.at.file=qlie::canonical(file);
 if(profile.legacyAvg&&!label.empty()){
  jump(label,state.at.file);
  state.vars["__avg1_created"]="1";
  state.pending.push_back(event("create",state.at.file));
 }else jump("@@top",state.at.file);
}
void Story::native(const Op&o){auto a=split(o.text,44);std::string cmd=lower(a[0]);auto arg=[&](size_t i){return i<a.size()?a[i]:std::string();};auto need=[&](bool ok){if(!ok)throw std::runtime_error("Unsupported native command: "+o.text);};
 if(profile.legacyAvg&&legacyNative(o,a))return;
 if(cmd=="\\then"||(profile.legacyAvg&&cmd=="\\lk"&&state.scope=="sys"))return;
 if(profile.legacyAvg&&cmd=="\\case"){
  need(o.end>=0);
  const auto selected=value(arg(1));
  const auto&ops=script(state.at.file).ops;
  auto top=state.at.pc;
  auto stop=o.otherwise>=0?unsigned(o.otherwise):unsigned(o.end);
  for(;top<stop;++top){
   const auto&branch=ops[top];
   if(branch.end>int(top)&&branch.end<o.end){top=unsigned(branch.end);continue;}
   if(branch.text.rfind("\\ans,",0)==0&&value(branch.text.substr(5))==selected){
    state.at.pc=top+1;return;
   }
  }
  state.at.pc=uint32_t(o.otherwise>=0?o.otherwise+1:o.end+1);
  return;
 }
 if(cmd=="\\if"){if(!condition(arg(1)))state.at.pc=uint32_t(o.otherwise>=0?o.otherwise+1:o.end+1);return;}
 if(cmd=="\\else"){state.at.pc=uint32_t(o.end+1);return;}
 if(cmd=="\\end"){auto&start=script(state.at.file).ops[size_t(o.end)];if(start.text.rfind("\\for",0)==0){auto v=split(start.text,44);need(v.size()==4);auto n=lower(v[3].substr(0,v[3].size()-2));state.vars[n]=std::to_string(std::stoi(value(n))+1);if(condition(v[2]))state.at.pc=uint32_t(o.end+1);}return;}
 if(cmd=="\\for"){need(a.size()==4);auto eq=a[1].find(61);need(eq!=a[1].npos);state.vars[lower(trim(a[1].substr(0,eq)))]=value(a[1].substr(eq+1));if(!condition(a[2]))state.at.pc=uint32_t(o.end+1);return;}
 if(cmd=="\\cal"){auto eq=arg(1).find(61);need(eq!=arg(1).npos);auto name=lower(trim(arg(1).substr(0,eq)));state.vars[profile.legacyAvg?legacyVariable(std::move(name)):name]=value(arg(1).substr(eq+1));return;}
 if(cmd=="\\var"||cmd=="\\gvar"||cmd=="\\svar"){for(size_t i=2;i<a.size();i++)state.vars.emplace(lower(a[i]),a[1]=="str"?"":"0");return;}
 if(cmd=="\\vexs"){state.vars["resultint[0]"]=state.vars.count(lower(arg(1)))?"1":"0";return;}
 if(cmd=="\\init"){need(lower(arg(1))=="game_routeflag");state.vars["game_routeflag[0]"]="0";return;}
 if(cmd=="\\del"){state.vars.erase(lower(arg(1)));return;}
 if(cmd=="\\scp"){state.scope=lower(profile.legacyAvg?arg(1):value(arg(1)));if(state.scope.rfind("dsnd[",0)==0){auto p=state.scope.find(93);state.scope="dsnd["+value(state.scope.substr(5,p-5))+"]";}return;}
 if(cmd=="\\?sr"){need(state.scope=="sys");state.vars["resultstr[0]"]=state.replay?"@@replayreturn":"";return;}
 if(cmd=="\\r"){need(state.scope=="sys");state.finished=true;state.pending.push_back(event("finish","title",o.line));return;}
 if(cmd=="\\i"){need(state.scope.rfind("dsnd[",0)==0);state.pending.push_back(event("audio-clear",state.scope,o.line));return;}
 if(cmd=="\\v"){need(state.scope=="snd[12]");state.pending.push_back(event("audio-volume",o.text,o.line));return;}
 if(cmd=="\\wait"||cmd=="\\clk"){state.pending.push_back(event(cmd=="\\clk"?"click-wait":"wait",value(arg(1)),o.line));return;}
 if(cmd=="\\ret"){if(state.replay&&state.calls.empty()){state.finished=true;state.pending.push_back(event("finish","replay",o.line));return;}need(!state.calls.empty());state.at=state.calls.back();state.calls.pop_back();return;}
 if(cmd=="\\sub"){
  auto target=lower(arg(1));
  if(!profile.nameEntryScript.empty()&&target=="@@top"&&
     qlie::canonical(value(arg(2)))==profile.nameEntryScript){
   state.pending.push_back(event("name-entry","",o.line));
   return;
  }
  if(target=="@@!kroutejmp"&&qlie::canonical(value(arg(2)))==profile.root){
   const auto file=value(arg(3));
   const auto route=state.vars.find("#flag:KRroute");
   const auto label="@@"+stem(qlie::canonical(file))+"_"+value(arg(4))+"_"+(route==state.vars.end()?"0":route->second);
   state.vars["#flag:"+label]="1";
   state.vars["resultstr[0]"]=label;
   jump(label,file);
   return;
  }
  if(!profile.alternateHeader.empty()&&target=="@@!view_setupimagepos"){
   need(a.size()==6&&lower(arg(2))=="self");
   auto pan=value(arg(3))+","+value(arg(4))+","+value(arg(5));
   state.vars["__view_pan"]=pan;
   state.pending.push_back(event("view-pan",pan,o.line));
   return;
  }
  if(!profile.alternateHeader.empty()&&(target=="@@!avg_create"||target=="@@!avg_free")){
   state.vars.erase("__view_pan");
   if(profile.historyJumpLimit)state.vars["__log_jump_locked"]=target=="@@!avg_create"&&state.replay?"0":"1";
   state.pending.push_back(event(target=="@@!avg_create"?"create":"free",state.at.file,o.line));
   return;
  }
  if(target=="@@!gamevalueinit")return;
  if(target=="@@!gamevaluereset"){state.vars["game_routeflag[0]"]="0";return;}
  if(target=="@@!filejumpname"){state.home=state.at.file;state.vars["resultstr[0]"]="@@"+stem(state.at.file)+"_end";return;}
  if(target=="@@!avg_setrouteparam"){state.routeStr=value(arg(3));state.routeValue=state.routeStr.empty()?0:state.routeStr=="Route"?0:std::stoi(state.routeStr);if(a.size()>4)state.routeStr=value(arg(4));return;}
  if(!profile.endingMovieScript.empty()&&qlie::canonical(value(arg(2)))==profile.endingMovieScript){
   need(target=="@@top"&&a.size()==4);
   state.vars["__ending_movie"]=value(arg(3));
  }
  state.calls.push_back(state.at);jump(arg(1),arg(2));return;
 }
 if(cmd=="\\jmp"||cmd=="\\go"){
  auto target=lower(arg(1));
  if(state.replay&&lower(arg(2))=="__rootfile"){
   state.finished=true;state.pending.push_back(event("finish","replay",o.line));return;
  }
  if(target=="@@!selectreturn"){std::string label=state.selectLabel+"_"+std::to_string(state.choice);jump(label,lower(state.routeStr)=="route"?profile.root:state.home);return;}
  if(target=="@@!branchreturn"){jump(state.branchLabel+"_"+std::to_string(state.routeValue),lower(state.routeStr)=="route"?profile.root:state.home);return;}
  if(value(arg(1))=="@@replayreturn"){state.finished=true;state.pending.push_back(event("finish","replay",o.line));return;}
  jump(arg(1),arg(2));return;
 }
 throw std::runtime_error("Unsupported native command: "+o.text);
}
void Story::caret(const Op&o){auto a=split(o.text,44);std::string cmd=lower(a[0].substr(1));
 if(cmd=="logjumppoint"&&profile.historyJumpLimit){
  const auto enabled=a.size()<2?"":value(a[1]);
  if(enabled!=""&&enabled!="0"&&enabled!="1")throw std::runtime_error("Unsupported log jump flag");
  state.vars["__log_jump_locked"]=enabled=="0"?"1":"0";
  return;
 }
 if(cmd=="select"){state.choices.assign(a.begin()+1,a.end());for(auto& choice:state.choices)choice=substitutePlayerName(std::move(choice));state.awaiting=true;state.pending.push_back(event("choice",o.text,o.line));return;}
 if(cmd=="selectlabel"&&!profile.selectionPrelude.empty()){
  if(state.choice<0||size_t(state.choice)+1>=a.size())throw std::runtime_error("Missing label for selected option");
  const auto label=value(a[size_t(state.choice)+1]);
  state.vars["avg_selectcurlabel"]=label;
  state.vars["#flag:"+label]="1";
  return;
 }
 if(cmd=="frag"&&a.size()==4){
  auto& flag=state.vars["#flag:"+value(a[2])];
  if(a[1]=="=")flag=value(a[3]);
  else if(a[1]=="+"||a[1]=="-"){
   const int previous=flag.empty()?0:std::stoi(flag);
   const int amount=std::stoi(value(a[3]));
   flag=std::to_string(previous+(a[1]=="+"?amount:-amount));
  }else throw std::runtime_error("Unsupported route fragment assignment");
  return;
 }
 if(cmd=="frag"&&profile.arithmeticFragments&&a.size()==4&&(a[1]=="+"||a[1]=="-")){
  const auto key="#flag:"+value(a[2]);
  const auto found=state.vars.find(key);
  const int64_t prior=found==state.vars.end()?0:std::stoi(found->second);
  const int64_t delta=std::stoi(value(a[3]));
  state.vars[key]=std::to_string(prior+(a[1]=="+"?delta:-delta));
  return;
 }
 if(cmd=="fragcal"&&a.size()==2){
  const auto expression=value(a[1]);
  const auto bracket=expression.find(']');
  if(expression.rfind("([",0)!=0||bracket==expression.npos||bracket+1>=expression.size()||expression.back()!=')')
   throw std::runtime_error("Unsupported route fragment expression: "+expression);
  const auto flag=state.vars.find("#flag:"+expression.substr(2,bracket-2));
  const auto current=flag==state.vars.end()?"0":flag->second;
  state.vars["fragresultint"]=condition(current+expression.substr(bracket+1,expression.size()-bracket-2))?"1":"0";
  return;
 }
 if(cmd=="fragtrack"&&a.size()==2){
  const auto label=value(a[1]);
  state.vars["#flag:"+label]="1";
  state.vars["resultstr[0]"]=label;
  return;
 }
 if(cmd.rfind("selectset",0)==0){state.home=state.at.file;state.selectLabel="@@"+stem(state.at.file)+"_sel"+cmd.substr(9);return;}
 if(cmd=="selectjmp"){jump(state.selectLabel+"_"+std::to_string(state.choice),profile.selection);return;}
 if(cmd=="branchjmp"){jump(state.branchLabel,profile.selection);return;}
 if(cmd.rfind("branch",0)==0){state.home=state.at.file;state.branchLabel="@@"+stem(state.at.file)+"_branch"+cmd.substr(6);return;}
 if(cmd=="include"){
  if(a.size()!=2)throw std::runtime_error("Malformed include");
  for(const auto&line:includeCommands(a[1]))state.pending.push_back(event("command",line,o.line));
  return;
 }
 if(cmd=="movie"&&!profile.endingMovieScript.empty()&&state.at.file==profile.endingMovieScript){
  const auto media=state.vars.find("__ending_movie");
  if(a.size()!=2||a[1].find("ParamStr[0]")==std::string::npos||media==state.vars.end()||media->second.empty())
   throw std::runtime_error("Missing ending movie argument");
  state.pending.push_back(event("command",endingMovieCommand(media->second),o.line));
  return;
 }
 if(profile.legacyAvg&&legacyCaret(o,a))return;
 if(profile.ignoredCaretCommands.count(cmd))return;
 if(!profile.sceneCommands.count(cmd))throw std::runtime_error("Unsupported story command: "+o.text);
 state.pending.push_back(event("command",o.text,o.line));
}
Event Story::next(){if(!state.pending.empty()){Event e=state.pending.front();state.pending.pop_front();return e;}if(state.awaiting)return event("await-choice","");if(state.finished)return event("finished","");for(unsigned count=0;count<10000;count++){auto&sc=script(state.at.file);auto reached=sc.labelsAt.find(state.at.pc);if(reached!=sc.labelsAt.end())for(const auto&label:reached->second)visited.insert(state.at.file+":"+label);if(state.at.pc>=sc.ops.size()){if(state.calls.empty())throw std::runtime_error("Unexpected script EOF: "+state.at.file);state.at=state.calls.back();state.calls.pop_back();continue;}Op o=sc.ops[state.at.pc++];operations++;try{if(o.text=="!page")return event("page","",o.line);if(o.text=="!create"||o.text=="!create-alternate"){
 if(profile.legacyAvg){
  if(state.vars.count("__avg1_created"))continue;
  state.vars["__avg1_created"]="1";
 }
 state.vars.erase("__view_pan");
 if(profile.historyJumpLimit)state.vars["__log_jump_locked"]="0";
 if(o.text=="!create-alternate")state.vars["__avg2_active"]="1";
 else state.vars.erase("__avg2_active");
 return event("create",state.at.file,o.line);
}if(o.text=="!free"){
 if(profile.legacyAvg){
  state.vars.erase("__avg1_created");
  for(auto it=state.vars.begin();it!=state.vars.end();){
   if(it->first.rfind("__avg1_slot_",0)==0||it->first.rfind("__avg1_actor_",0)==0||
      it->first.rfind("__avg1_layer_",0)==0||it->first=="__avg1_previous_chara")it=state.vars.erase(it);
   else ++it;
  }
 }
 if(profile.historyJumpLimit)state.vars["__log_jump_locked"]="1";
 return event("free",state.at.file,o.line);
}if(o.text[0]==92)native(o);else if(o.text[0]==94){caret(o);if(o.after!=After::None)state.pending.push_back(event(o.after==After::Interlude?"interlude":"visual-wait","",o.line));}else return event("text",substitutePlayerName(o.text),o.line);}catch(const std::exception&e){throw std::runtime_error(state.at.file+":"+std::to_string(o.line)+" "+e.what());}if(!state.pending.empty()){Event e=state.pending.front();state.pending.pop_front();return e;}if(state.finished)return event("finish","title",o.line);}throw std::runtime_error("Story instruction budget exhausted at "+state.at.file);}
void Story::choose(unsigned i){
 if(!state.awaiting||i>=state.choices.size())throw std::runtime_error("Invalid choice");
 state.choice=int(i);state.awaiting=false;
 if(profile.legacyAvg)state.vars["resultbtnint[0]"]=std::to_string(i);
}
struct Writer {qlie::Bytes b;void n(uint32_t v){for(int i=0;i<4;i++)b.push_back(uint8_t(v>>(8*i)));}void s(const std::string&v){n(uint32_t(v.size()));b.insert(b.end(),v.begin(),v.end());}void f(const Frame&v){s(v.file);n(v.pc);}};
struct Reader {const qlie::Bytes&b;size_t p=0;uint32_t n(){if(p+4>b.size())throw std::runtime_error("Truncated story save");uint32_t v=0;for(int i=0;i<4;i++)v|=uint32_t(b[p++])<<(8*i);return v;}std::string s(){size_t size=n();if(size>b.size()-p)throw std::runtime_error("Truncated saved string");std::string v(b.begin()+p,b.begin()+p+size);p+=size;return v;}Frame f(){auto s0=s();return {s0,n()};}};
qlie::Bytes Story::save()const{Writer w;w.s(profile.saveSignature);w.f(state.at);w.n(uint32_t(state.calls.size()));for(auto&x:state.calls)w.f(x);w.n(uint32_t(state.vars.size()));for(auto&x:state.vars){w.s(x.first);w.s(x.second);}for(auto&x:{state.scope,state.home,state.selectLabel,state.branchLabel,state.routeStr})w.s(x);w.n(uint32_t(state.routeValue));w.n(uint32_t(state.choice));w.n(state.awaiting);w.n(state.finished);w.n(state.replay);w.n(uint32_t(state.choices.size()));for(auto&s:state.choices)w.s(s);w.n(uint32_t(state.pending.size()));for(auto&e:state.pending){w.s(e.type);w.s(e.value);w.s(e.file);w.n(e.line);}w.n(qlie::hash(w.b));return w.b;}
StoryState Story::decodeSave(const qlie::Bytes&b){if(b.size()<12)throw std::runtime_error("Short story save");qlie::Bytes payload(b.begin(),b.end()-4);Reader tail{b,b.size()-4};if(tail.n()!=qlie::hash(payload))throw std::runtime_error("Story save checksum");Reader r{payload};if(r.s()!=profile.saveSignature)throw std::runtime_error("Story save version");StoryState s;s.at=r.f();size_t n=r.n();if(n>256)throw std::runtime_error("Invalid call depth");while(n--)s.calls.push_back(r.f());n=r.n();if(n>10000)throw std::runtime_error("Invalid variable count");while(n--){auto k=r.s();s.vars[k]=r.s();}s.scope=r.s();s.home=r.s();s.selectLabel=r.s();s.branchLabel=r.s();s.routeStr=r.s();s.routeValue=int(r.n());s.choice=int(r.n());s.awaiting=r.n();s.finished=r.n();s.replay=r.n();n=r.n();if(n>64)throw std::runtime_error("Invalid choice count");while(n--)s.choices.push_back(r.s());n=r.n();if(n>10000)throw std::runtime_error("Invalid event count");while(n--){auto t=r.s(),v=r.s(),f=r.s();s.pending.push_back({t,v,f,r.n()});}if(r.p!=payload.size())throw std::runtime_error("Unexpected story save bytes");if(s.at.pc>script(s.at.file).ops.size())throw std::runtime_error("Saved position out of range");for(auto&f:s.calls)if(f.pc>script(f.file).ops.size())throw std::runtime_error("Saved return position out of range");return s;}
void Story::restore(const qlie::Bytes&b){state=decodeSave(b);}
}
