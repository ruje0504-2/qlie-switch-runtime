#pragma once
#include "qlie.hpp"
#include <deque>
#include <set>
namespace whj2 {
std::string trim(std::string);
std::vector<std::string> split(const std::string&,char delimiter);
struct Event {std::string type,value,file;unsigned line=0;};
struct Frame {std::string file;uint32_t pc=0;};
struct StoryState {
 Frame at;std::vector<Frame> calls;std::map<std::string,std::string> vars;
 std::string scope,home,selectLabel,branchLabel,routeStr;int routeValue=0,choice=0;
 bool awaiting=false,finished=false,replay=false;
 std::vector<std::string> choices;std::deque<Event> pending;
};
class Story {
 struct Op {std::string text;unsigned line;int end=-1,otherwise=-1;};
 struct Script {std::vector<Op> ops;std::map<std::string,size_t> labels;std::map<size_t,std::vector<std::string>> labelsAt;};
 qlie::Assets&assets;std::map<std::string,Script> scripts;
 Script&script(const std::string&);
 std::string value(std::string)const;
 bool condition(const std::string&)const;
 void resetState();
 void jump(std::string,std::string);
 void native(const Op&);
 void caret(const Op&);
 Event event(std::string,std::string,unsigned=0)const;
public:
 StoryState state;uint64_t operations=0;std::set<std::string> visited;
 explicit Story(qlie::Assets&a):assets(a){}
 void start();void startReplay(const std::string&script);
 Event next();
 void choose(unsigned);
 qlie::Bytes save()const;
 void restore(const qlie::Bytes&);
};
}
