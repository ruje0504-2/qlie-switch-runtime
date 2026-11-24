#pragma once
#include "image.hpp"
#include "sentence_wait.hpp"
#include <map>
namespace whj2 {
class SceneTransition {
 qlie::Assets&assets;
 std::map<std::string,std::string> templates;
 Image rule;std::string rulePath;unsigned ruleMode=4;
 Image previous;
 double elapsed=0,duration=0;
 bool staged=false,active=false,caught=false;
public:
 explicit SceneTransition(qlie::Assets&);
 bool command(const std::string&,const qlie::Bytes& scene,SentenceWait* wait=nullptr);
 void present();void update(double milliseconds);void blend(qlie::Bytes&)const;void reset();
 void blend(class RenderTarget&)const;
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
