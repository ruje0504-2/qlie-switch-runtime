#pragma once
#include "qlie.hpp"
#include <array>
#include <deque>
#include <functional>
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
struct LegacyAvgLayout {
 const char* messageImage;
 unsigned messageX,messageY,messageWidth,messageHeight,messageFontSize;
 unsigned frameX,frameY;
 unsigned speakerX,speakerY,speakerSize,speakerColor;
 const char* faceWindow;
 const char* faceDirectory;
 const char* faceAnimation;
 unsigned characterFadeMs;
 const char* externalFontSlot;
 unsigned externalCodepoint,externalTileIndex;
 std::array<std::pair<const char*,const char*>,2> routeFlags;
 const char* makerLogo;
 std::pair<const char*,const char*> renamedMovie;
 const char* creditsFile;
 unsigned creditsSkipDelay,creditsClickWait;
};
std::string legacyAvgMessageWindow(const LegacyAvgLayout&);
struct StoryProfile {
 std::string root,selection,includeDirectory,header,replayHeader,footer,saveSignature;
 std::set<std::string> sceneCommands;
 std::string interludeImage;
 std::map<std::string,std::set<unsigned>> visualWaitParagraphs;
 std::string selectionPrelude{};
 bool presentCommandParagraphs=false;
 std::string nameEntryScript{}, alternateHeader{}, endingMovieScript{};
 unsigned historyJumpLimit=0;
 bool sceneSkip=false;
 std::string replayPrelude{};
 bool arithmeticFragments=false;
 std::set<std::string> ignoredCaretCommands{};
 const LegacyAvgLayout* legacyAvg=nullptr;
};
class Story {
 enum class After {None,Interlude,VisualWait};
 struct Op {std::string text;unsigned line;int end=-1,otherwise=-1;After after=After::None;};
 struct Script {std::vector<Op> ops;std::map<std::string,size_t> labels;std::map<size_t,std::vector<std::string>> labelsAt;};
 qlie::Assets&assets;
 const StoryProfile profile;
 std::map<std::string,Script> scripts;
 std::map<std::string,std::vector<std::string>> includes;
 Script&script(const std::string&);
 const std::vector<std::string>& includeCommands(const std::string&);
 std::string value(std::string)const;
 std::string legacyVideoPath(std::string)const;
 std::string legacyAssetHint(const std::string&,std::string& scope)const;
 std::string substitutePlayerName(std::string)const;
 bool condition(const std::string&)const;
 void resetState();
 void jump(std::string,std::string);
 void native(const Op&);
 void caret(const Op&);
 bool legacyCaret(const Op&,const std::vector<std::string>&);
 bool legacyNative(const Op&,const std::vector<std::string>&);
 Event event(std::string,std::string,unsigned=0)const;
public:
 StoryState state;uint64_t operations=0;std::set<std::string> visited;
 Story(qlie::Assets&a,const StoryProfile&settings):assets(a),profile(settings){}
 void start();void startReplay(const std::string&script,const std::string&label={});
 Event next();
 std::vector<std::string> upcomingAssetCommands(bool opening=false);
 bool beforeNextPresentation(bool hasBody,const std::function<bool(const Event&)>& match);
 void choose(unsigned);
 bool setPlayerName(std::string);
 bool historyJumpEnabled()const;
 qlie::Bytes save()const;
 StoryState decodeSave(const qlie::Bytes&);
 void restore(const qlie::Bytes&);
};
}
