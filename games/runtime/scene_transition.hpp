#pragma once
#include "image.hpp"
#include "sentence_wait.hpp"
#include <map>
#include <functional>
#include <memory>
namespace whj2 {
class SceneImages;
class AssetReadAhead;
class SceneTransition {
 enum class Mode {Overlap,Rule,Mosaic,Animation};
 qlie::Assets&assets;
 std::map<std::string,std::string> templates;
 std::shared_ptr<const Image> rule;
 std::string effectPath,cachedRulePath;
 AssetReadAhead*readAhead=nullptr;
 std::vector<std::vector<std::string>> sentenceFields(const std::string&)const;
 unsigned ruleMode=4;
 Mode mode=Mode::Overlap;
 void loadRule(const std::string&);
 void loadAnimation(const std::string&);
 void start(Mode,double);
 Image previous,incoming;
 std::unique_ptr<SceneImages> animation;
 std::string cachedAnimationPath;
 bool captureAnimationTarget=false;
 double animationLength=0;
 double elapsed=0,duration=0;
 bool staged=false,active=false,caught=false;
public:
 explicit SceneTransition(qlie::Assets&);
 ~SceneTransition();
 void setReadAhead(AssetReadAhead*loader){readAhead=loader;}
 static std::string ruleKey(const std::string&);
 std::vector<std::string> ruleAssets(const std::string&)const;
 void warmRule(const std::string&,bool required=false);
 bool command(const std::string&,const qlie::Bytes& scene,SentenceWait* wait=nullptr);
 bool commandWithCapture(const std::string&,const std::function<qlie::Bytes()>&,SentenceWait* wait=nullptr,bool animate=true);
 void startAnimation(const std::string&,double,const qlie::Bytes&);
 bool isActive()const{return active;}
 bool needsTarget()const;
 bool needsCanvas()const;
 void captureTarget(const qlie::Bytes&);
 void present();void update(double milliseconds);bool blend(qlie::Bytes&)const;void reset();
 void blend(class RenderTarget&)const;
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
