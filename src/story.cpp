// SPDX-License-Identifier: GPL-2.0-or-later
#include "story.hpp"
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <algorithm>
namespace whj2 {
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==s.npos)return {};return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
std::vector<std::string> split(const std::string&s,char delim){std::vector<std::string>r;bool quote=false;size_t start=0;for(size_t i=0;i<s.size();i++){if(s[i]==34)quote=!quote;if(s[i]==delim&&!quote){r.push_back(trim(s.substr(start,i-start)));start=i+1;}}r.push_back(trim(s.substr(start)));return r;}
static std::string lower(std::string s){for(auto&c:s)if(c>=65&&c<=90)c+=32;return s;}
static std::string stem(std::string s){auto p=s.find_last_of("/\\");if(p!=s.npos)s.erase(0,p+1);p=s.rfind(46);if(p!=s.npos)s.erase(p);return s;}
static std::vector<std::string> commands(const std::string&s){std::vector<std::string>r;bool quote=false;size_t start=0;for(size_t i=0;i<s.size();i++){if(s[i]==34)quote=!quote;if(s[i]==92&&!quote){if(i>start)r.push_back(trim(s.substr(start,i-start)));start=i;}}if(start<s.size())r.push_back(trim(s.substr(start)));return r;}
Story::Script&Story::script(const std::string&n){std::string name=qlie::canonical(n);auto it=scripts.find(name);if(it!=scripts.end())return it->second;Script sc;std::istringstream in(qlie::cp932(assets.read(name)));std::string line;unsigned ln=0;while(std::getline(in,line)){ln++;line=trim(line);if(line.empty()||line.rfind("//",0)==0)continue;if(line.rfind("@@@",0)==0){std::string inc=qlie::canonical(line.substr(3));if(!inc.empty()&&inc[0]==33)inc.erase(0,1);if(inc=="avg/header.s")sc.ops.push_back({"!create",ln});else if(inc=="avg/footer.s")sc.ops.push_back({"!free",ln});else throw std::runtime_error("Unsupported story include "+line+" in "+name);continue;}if(line.rfind("@@",0)==0){sc.labels[lower(line)]=sc.ops.size();continue;}if(line[0]==92){for(auto&s:commands(line))if(!s.empty())sc.ops.push_back({s,ln});}else sc.ops.push_back({line,ln});}
 if(sc.labels.count("@@main"))sc.labels["@@top"]=sc.labels["@@!top"]=0;
 std::vector<size_t>stack;
 for(size_t i=0;i<sc.ops.size();i++){auto&s=sc.ops[i].text;if(s.rfind("\\if,",0)==0||s.rfind("\\for",0)==0)stack.push_back(i);else if(s=="\\else"){if(stack.empty())throw std::runtime_error("Unmatched else");sc.ops[stack.back()].otherwise=int(i);}else if(s=="\\end"){if(stack.empty())throw std::runtime_error("Unmatched end in "+name);size_t start=stack.back();stack.pop_back();sc.ops[start].end=int(i);sc.ops[i].end=int(start);if(sc.ops[start].otherwise>=0)sc.ops[size_t(sc.ops[start].otherwise)].end=int(i);}}
 if(!stack.empty())throw std::runtime_error("Unclosed control block in "+name);
 return scripts.emplace(name,std::move(sc)).first->second;
}
std::string Story::value(std::string s)const{s=trim(s);if(s.size()>=2&&s.front()==34&&s.back()==34)return s.substr(1,s.size()-2);if(lower(s)=="selffilename")return state.at.file;if(s.empty())return {};auto it=state.vars.find(lower(s));return it==state.vars.end()?s:it->second;}
bool Story::condition(const std::string&s)const{for(const auto&op:std::vector<std::string>{"!=","==",">=","<=","<",">"}){auto p=s.find(op);if(p==s.npos)continue;auto a=value(s.substr(0,p)),b=value(s.substr(p+op.size()));if(op=="!=")return a!=b;if(op=="==")return a==b;long x=std::stol(a),y=std::stol(b);return op==">="?x>=y:op=="<="?x<=y:op=="<"?x<y:x>y;}throw std::runtime_error("Unsupported story condition: "+s);}
void Story::jump(std::string label,std::string file){label=lower(value(label));file=value(file);if(file.empty()||lower(file)=="self")file=state.at.file;else if(file.find(47)==file.npos&&file.find(92)==file.npos){auto p=state.at.file.rfind(47);file=state.at.file.substr(0,p+1)+file;}file=qlie::canonical(file);auto&sc=script(file);auto it=sc.labels.find(label);if(it==sc.labels.end())throw std::runtime_error("Missing label "+label+" in "+file);state.at={file,uint32_t(it->second)};visited.insert(file+":"+label);}
Event Story::event(std::string t,std::string v,unsigned ln)const{return {std::move(t),std::move(v),state.at.file,ln};}
void Story::start(){state={};state.vars["game_routeflag[0]"]="0";state.vars["_routefile"]="scenario/root.s";state.vars["_selectfile"]="scenario/select.s";state.vars["resultstr[0]"]="";state.vars["resultint[0]"]="0";state.at.file="scenario/root.s";jump("@@top",state.at.file);}
void Story::native(const Op&o){auto a=split(o.text,44);std::string cmd=lower(a[0]);auto arg=[&](size_t i){return i<a.size()?a[i]:std::string();};auto need=[&](bool ok){if(!ok)throw std::runtime_error("Unsupported native command: "+o.text);};
 if(cmd=="\\then")return;
 if(cmd=="\\if"){if(!condition(arg(1)))state.at.pc=uint32_t(o.otherwise>=0?o.otherwise+1:o.end+1);return;}
 if(cmd=="\\else"){state.at.pc=uint32_t(o.end+1);return;}
 if(cmd=="\\end"){auto&start=script(state.at.file).ops[size_t(o.end)];if(start.text.rfind("\\for",0)==0){auto v=split(start.text,44);need(v.size()==4);auto n=lower(v[3].substr(0,v[3].size()-2));state.vars[n]=std::to_string(std::stoi(value(n))+1);if(condition(v[2]))state.at.pc=uint32_t(o.end+1);}return;}
 if(cmd=="\\for"){need(a.size()==4);auto eq=a[1].find(61);need(eq!=a[1].npos);state.vars[lower(trim(a[1].substr(0,eq)))]=value(a[1].substr(eq+1));if(!condition(a[2]))state.at.pc=uint32_t(o.end+1);return;}
 if(cmd=="\\cal"){auto eq=arg(1).find(61);need(eq!=arg(1).npos);state.vars[lower(trim(arg(1).substr(0,eq)))]=value(arg(1).substr(eq+1));return;}
 if(cmd=="\\var"||cmd=="\\gvar"||cmd=="\\svar"){for(size_t i=2;i<a.size();i++)state.vars.emplace(lower(a[i]),a[1]=="str"?"":"0");return;}
 if(cmd=="\\vexs"){state.vars["resultint[0]"]=state.vars.count(lower(arg(1)))?"1":"0";return;}
 if(cmd=="\\init"){need(lower(arg(1))=="game_routeflag");state.vars["game_routeflag[0]"]="0";return;}
 if(cmd=="\\del"){state.vars.erase(lower(arg(1)));return;}
 if(cmd=="\\scp"){state.scope=lower(value(arg(1)));if(state.scope.rfind("dsnd[",0)==0){auto p=state.scope.find(93);state.scope="dsnd["+value(state.scope.substr(5,p-5))+"]";}return;}
 if(cmd=="\\?sr"){need(state.scope=="sys");state.vars["resultstr[0]"]=state.replay?"@@replayreturn":"";return;}
 if(cmd=="\\r"){need(state.scope=="sys");state.finished=true;state.pending.push_back(event("finish","title",o.line));return;}
 if(cmd=="\\i"){need(state.scope.rfind("dsnd[",0)==0);state.pending.push_back(event("audio-clear",state.scope,o.line));return;}
 if(cmd=="\\v"){need(state.scope=="snd[12]");state.pending.push_back(event("audio-volume",o.text,o.line));return;}
 if(cmd=="\\wait"||cmd=="\\clk"){state.pending.push_back(event("wait",value(arg(1)),o.line));return;}
 if(cmd=="\\ret"){need(!state.calls.empty());state.at=state.calls.back();state.calls.pop_back();return;}
 if(cmd=="\\sub"){
  auto target=lower(arg(1));
  if(target=="@@!gamevalueinit")return;
  if(target=="@@!gamevaluereset"){state.vars["game_routeflag[0]"]="0";return;}
  if(target=="@@!filejumpname"){state.home=state.at.file;state.vars["resultstr[0]"]="@@"+stem(state.at.file)+"_end";return;}
  if(target=="@@!avg_setrouteparam"){state.routeStr=value(arg(3));state.routeValue=state.routeStr.empty()?0:state.routeStr=="Route"?0:std::stoi(state.routeStr);if(a.size()>4)state.routeStr=value(arg(4));return;}
  state.calls.push_back(state.at);jump(arg(1),arg(2));return;
 }
 if(cmd=="\\jmp"||cmd=="\\go"){
  auto target=lower(arg(1));
  if(target=="@@!selectreturn"){std::string label=state.selectLabel+"_"+std::to_string(state.choice);jump(label,lower(state.routeStr)=="route"?"scenario/root.s":state.home);return;}
  if(target=="@@!branchreturn"){jump(state.branchLabel+"_"+std::to_string(state.routeValue),lower(state.routeStr)=="route"?"scenario/root.s":state.home);return;}
  if(value(arg(1))=="@@replayreturn"){state.finished=true;state.pending.push_back(event("finish","replay",o.line));return;}
  jump(arg(1),arg(2));return;
 }
 throw std::runtime_error("Unsupported native command: "+o.text);
}
void Story::caret(const Op&o){auto a=split(o.text,44);std::string cmd=lower(a[0].substr(1));
 if(cmd=="select"){state.choices.assign(a.begin()+1,a.end());state.awaiting=true;state.pending.push_back(event("choice",o.text,o.line));return;}
 if(cmd.rfind("selectset",0)==0){state.home=state.at.file;state.selectLabel="@@"+stem(state.at.file)+"_sel"+cmd.substr(9);return;}
 if(cmd=="selectjmp"){jump(state.selectLabel+"_"+std::to_string(state.choice),"scenario/select.s");return;}
 if(cmd=="branchjmp"){jump(state.branchLabel,"scenario/select.s");return;}
 if(cmd.rfind("branch",0)==0){state.home=state.at.file;state.branchLabel="@@"+stem(state.at.file)+"_branch"+cmd.substr(6);return;}
 if(cmd=="include"){if(a.size()!=2)throw std::runtime_error("Malformed include");std::istringstream in(qlie::cp932(assets.read("include/"+a[1]+".txt")));std::string line;while(std::getline(in,line)){line=trim(line);if(!line.empty()&&line.rfind("//",0)!=0)state.pending.push_back(event("command",line,o.line));}return;}
 static const std::set<std::string>supported={"sentence","format","message","face","bg01","bg02","bg03","bg04","chara01","chara02","chara03","chara04","effect","env","music01","se01","se02","savefilter","movie"};
 if(!supported.count(cmd))throw std::runtime_error("Unsupported story command: "+o.text);
 state.pending.push_back(event("command",o.text,o.line));
}
Event Story::next(){if(!state.pending.empty()){Event e=state.pending.front();state.pending.pop_front();return e;}if(state.awaiting)return event("await-choice","");if(state.finished)return event("finished","");for(unsigned count=0;count<10000;count++){auto&sc=script(state.at.file);if(state.at.pc>=sc.ops.size()){if(state.calls.empty())throw std::runtime_error("Unexpected script EOF: "+state.at.file);state.at=state.calls.back();state.calls.pop_back();continue;}Op o=sc.ops[state.at.pc++];operations++;try{if(o.text=="!create")return event("create",state.at.file,o.line);if(o.text=="!free")return event("free",state.at.file,o.line);if(o.text[0]==92)native(o);else if(o.text[0]==94)caret(o);else return event("text",o.text,o.line);}catch(const std::exception&e){throw std::runtime_error(state.at.file+":"+std::to_string(o.line)+" "+e.what());}if(!state.pending.empty()){Event e=state.pending.front();state.pending.pop_front();return e;}if(state.finished)return event("finish","title",o.line);}throw std::runtime_error("Story instruction budget exhausted at "+state.at.file);}
void Story::choose(unsigned i){if(!state.awaiting||i>=state.choices.size())throw std::runtime_error("Invalid choice");state.choice=int(i);state.awaiting=false;}
struct Writer {qlie::Bytes b;void n(uint32_t v){for(int i=0;i<4;i++)b.push_back(uint8_t(v>>(8*i)));}void s(const std::string&v){n(uint32_t(v.size()));b.insert(b.end(),v.begin(),v.end());}void f(const Frame&v){s(v.file);n(v.pc);}};
struct Reader {const qlie::Bytes&b;size_t p=0;uint32_t n(){if(p+4>b.size())throw std::runtime_error("Truncated story save");uint32_t v=0;for(int i=0;i<4;i++)v|=uint32_t(b[p++])<<(8*i);return v;}std::string s(){size_t size=n();if(size>b.size()-p)throw std::runtime_error("Truncated saved string");std::string v(b.begin()+p,b.begin()+p+size);p+=size;return v;}Frame f(){auto s0=s();return {s0,n()};}};
qlie::Bytes Story::save()const{Writer w;w.s("WHJ2-STORY-1");w.f(state.at);w.n(uint32_t(state.calls.size()));for(auto&x:state.calls)w.f(x);w.n(uint32_t(state.vars.size()));for(auto&x:state.vars){w.s(x.first);w.s(x.second);}for(auto&x:{state.scope,state.home,state.selectLabel,state.branchLabel,state.routeStr})w.s(x);w.n(uint32_t(state.routeValue));w.n(uint32_t(state.choice));w.n(state.awaiting);w.n(state.finished);w.n(state.replay);w.n(uint32_t(state.choices.size()));for(auto&s:state.choices)w.s(s);w.n(uint32_t(state.pending.size()));for(auto&e:state.pending){w.s(e.type);w.s(e.value);w.s(e.file);w.n(e.line);}w.n(qlie::hash(w.b));return w.b;}
void Story::restore(const qlie::Bytes&b){if(b.size()<12)throw std::runtime_error("Short story save");qlie::Bytes payload(b.begin(),b.end()-4);Reader tail{b,b.size()-4};if(tail.n()!=qlie::hash(payload))throw std::runtime_error("Story save checksum");Reader r{payload};if(r.s()!="WHJ2-STORY-1")throw std::runtime_error("Story save version");StoryState s;s.at=r.f();size_t n=r.n();if(n>256)throw std::runtime_error("Invalid call depth");while(n--)s.calls.push_back(r.f());n=r.n();if(n>10000)throw std::runtime_error("Invalid variable count");while(n--){auto k=r.s();s.vars[k]=r.s();}s.scope=r.s();s.home=r.s();s.selectLabel=r.s();s.branchLabel=r.s();s.routeStr=r.s();s.routeValue=int(r.n());s.choice=int(r.n());s.awaiting=r.n();s.finished=r.n();s.replay=r.n();n=r.n();if(n>64)throw std::runtime_error("Invalid choice count");while(n--)s.choices.push_back(r.s());n=r.n();if(n>10000)throw std::runtime_error("Invalid event count");while(n--){auto t=r.s(),v=r.s(),f=r.s();s.pending.push_back({t,v,f,r.n()});}if(r.p!=payload.size())throw std::runtime_error("Unexpected story save bytes");if(s.at.pc>script(s.at.file).ops.size())throw std::runtime_error("Saved position out of range");for(auto&f:s.calls)if(f.pc>script(f.file).ops.size())throw std::runtime_error("Saved return position out of range");state=std::move(s);}
}
