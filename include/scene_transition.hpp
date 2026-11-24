#pragma once
#include "qlie.hpp"
#include "sentence_wait.hpp"
#include <map>
namespace whj2 {
class SceneTransition {
 std::map<std::string,std::string> templates;
 qlie::Bytes previous;
 double elapsed=0,duration=0;
 bool staged=false,active=false,caught=false;
public:
 explicit SceneTransition(qlie::Assets&);
 bool command(const std::string&,const qlie::Bytes& scene,SentenceWait* wait=nullptr);
 void present();void update(double milliseconds);void blend(qlie::Bytes&)const;void reset();
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
