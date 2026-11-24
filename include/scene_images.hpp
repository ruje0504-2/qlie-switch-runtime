#pragma once
#include "image.hpp"
#include "particles.hpp"
#include "message_text.hpp"
#include <map>
#include <memory>
namespace whj2 {
struct Composite;
struct SceneSoundEvent {bool stop=false,loop=false;unsigned channel=0,category=2;float gain=1;std::string source;qlie::Bytes bytes;};
class SceneImages {
 struct Tween {float from=0,to=0,power=100;double elapsed=0,duration=0;unsigned mode=1;};
 struct Layer {std::map<std::string,Tween> tweens;std::map<std::string,std::string> params;std::string prefix,path;std::shared_ptr<Image> pixels;std::shared_ptr<Composite> composite;double milliseconds=0,audioMilliseconds=-1;bool audioStopPending=false;};
 unsigned windowAlpha=255;
 uint32_t particleState=particleIdentityInitialState();
 mutable bool faceWarning=false;
 std::map<std::string,std::string> faceRepId,faceRepFile;
 unsigned faceType=0;std::string faceDefaultFile;
 qlie::Assets&assets;std::map<std::string,Layer>layers;std::map<std::string,std::map<std::string,std::string>>templates;std::map<std::string,std::shared_ptr<Image>>cache;
 std::map<std::string,std::shared_ptr<Composite>>composites;
 static float value(const Layer&,const std::string&,float);
 void transition(Layer&,const Layer&,bool,bool);
 void render(qlie::Bytes&,unsigned pass)const;
 std::string expand(std::string,const std::string&);
public:
 explicit SceneImages(qlie::Assets&);
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
 std::vector<SceneSoundEvent> takeSoundEvents();
 void update(double milliseconds);bool command(const std::string&);void clear();void beginPage();void selectFace(const std::string&speaker,const std::string&voice);void draw(qlie::Bytes&)const;void drawScene(qlie::Bytes&)const;void drawFaces(qlie::Bytes&)const;void drawMessages(qlie::Bytes&)const;
 void setWindowAlpha(unsigned alpha){windowAlpha=alpha>255?255:alpha;}
 bool messageVisible()const;MessageStyle messageStyle()const;void setMessageImage(const Image&,bool show=false);
};
}
