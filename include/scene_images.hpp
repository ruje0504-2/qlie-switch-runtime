#pragma once
#include "image.hpp"
#include "particles.hpp"
#include "message_text.hpp"
#include <map>
#include <memory>
#include <set>
#include <array>
namespace whj2 {
struct Composite;
class RenderTarget;
class AssetReadAhead;
struct SceneSoundEvent {bool stop=false,loop=false;unsigned channel=0,category=2;float gain=1;std::string source;qlie::Bytes bytes;};
class SceneImages {
 struct Tween {float from=0,to=0,power=100;double elapsed=0,duration=0;unsigned mode=1;};
 struct FilteredSlot {std::shared_ptr<const Image> source,result;std::string name;};
 struct Layer {mutable std::array<FilteredSlot,4> filtered;std::map<unsigned,std::shared_ptr<const Image>> uiSlots;std::map<std::string,Tween> tweens;std::map<std::string,std::string> params;std::string prefix,path;std::shared_ptr<const Image> pixels,scriptReplacement;std::shared_ptr<Composite> composite;double milliseconds=0,audioMilliseconds=-1;bool audioStopPending=false;};
 unsigned windowAlpha=255;
 uint32_t particleState=particleIdentityInitialState();
 mutable uint32_t waveformCursor=0;
 mutable bool faceWarning=false;
 mutable std::set<std::string> shownFiles;
 std::map<std::string,std::string> faceRepId,faceRepFile;
 unsigned faceType=0;std::string faceDefaultFile;
 qlie::Assets&assets;std::map<std::string,Layer>layers;std::map<std::string,std::map<std::string,std::string>>templates;std::map<std::string,std::shared_ptr<const Image>>cache;
 std::map<std::string,std::shared_ptr<Composite>>composites;
 AssetReadAhead*readAhead=nullptr;
 std::shared_ptr<const Image> loadBitmap(const std::string&);
 std::shared_ptr<Composite> loadComposite(const std::string&);
 static float value(const Layer&,const std::string&,float);
 void loadReplacement(Layer&,const std::string&);
 void transition(Layer&,const Layer&,bool,bool);
 void render(qlie::Bytes&,unsigned pass,RenderTarget*target=nullptr)const;
 std::string expand(std::string,const std::string&);
public:
 explicit SceneImages(qlie::Assets&);
 void setReadAhead(AssetReadAhead*loader){readAhead=loader;}
 std::vector<std::string> warmPaths(const std::string&)const;
 void warmFile(const std::string&);
 void draw(RenderTarget&)const;
 void drawScene(RenderTarget&)const;void drawFaces(RenderTarget&)const;void drawMessages(RenderTarget&)const;
 std::set<std::string> takeShownFiles(){std::set<std::string> out;out.swap(shownFiles);return out;}
 // UI slots are transient presentation state owned by title/choice screens.
 void uiSlot(const std::string&layer,unsigned slot,const std::string&asset);
 void uiSlot(const std::string&layer,unsigned slot,const Image&);
 bool uiHit(const std::string&layer,float x,float y)const;
 std::pair<unsigned,unsigned> dimensions(const std::string&layer)const;
 void uiExternal(const std::string&layer,unsigned index,float value);
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
 std::vector<SceneSoundEvent> takeSoundEvents();
 void update(double milliseconds);bool command(const std::string&);void clear();void beginPage();void selectFace(const std::string&speaker,const std::string&voice);void draw(qlie::Bytes&)const;void drawScene(qlie::Bytes&)const;void drawFaces(qlie::Bytes&)const;void drawMessages(qlie::Bytes&)const;
 void setWindowAlpha(unsigned alpha){windowAlpha=alpha>255?255:alpha;}
 bool messageVisible()const;MessageStyle messageStyle()const;void setMessageImage(const Image&,bool show=false);
};
}
