#pragma once
#include "image.hpp"
#include "particles.hpp"
#include "message_text.hpp"
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <array>
namespace whj2 {
struct Affine2D;
struct Composite;
struct EyecatchFilm;
class RenderTarget;
class RenderQueue;
class AssetReadAhead;
struct SceneSoundEvent {bool stop=false,loop=false;unsigned channel=0,category=2;float gain=1;std::string source;qlie::Bytes bytes;};
struct SpeakerStyle {int x=200,y=570;unsigned size=26,color=0xffffff;};
class SceneImages {
 struct Tween {float from=0,to=0,power=100;double elapsed=0,duration=0;unsigned mode=1;};
 struct FilteredSlot {std::shared_ptr<const Image> source,result;std::string name;};
 struct Layer {mutable std::array<FilteredSlot,4> filtered;std::map<unsigned,std::shared_ptr<const Image>> uiSlots;std::map<std::string,Tween> tweens;std::map<std::string,std::string> params;std::string prefix,path;std::shared_ptr<const Image> pixels,scriptReplacement;std::shared_ptr<Composite> composite,extension;std::array<float,16> external{};uint16_t externalMask=0;double milliseconds=0,audioMilliseconds=-1,waveMilliseconds=-1,extensionMilliseconds=0;bool audioStopPending=false;};
 unsigned windowAlpha=255;
 uint32_t particleState=particleIdentityInitialState();
 mutable uint32_t waveformCursor=0;
 mutable bool faceWarning=false;
 mutable std::set<std::string> shownFiles;
 std::map<std::string,std::string> faceRepId,faceRepFile,waveRecipes,extensionPositions;
 std::set<std::string> splitCharacters,imageObjects;
 bool hasCamera=false,hasNameText=false,alternateMessageWindow=false;
 unsigned faceType=0;std::string faceDefaultFile;
 qlie::Assets&assets;
 std::shared_ptr<EyecatchFilm> eyecatchFilm;
 std::map<std::string,Layer>layers;std::map<std::string,std::map<std::string,std::string>>templates;std::map<std::string,std::shared_ptr<const Image>>cache;
 std::map<std::string,std::shared_ptr<Composite>>composites;
 AssetReadAhead*readAhead=nullptr;
 bool prefetchNativeVideo=false;
 std::string panLayer;
 float panExtentX=0,panExtentY=0,panX=0,panY=0;
 struct RestoreTarget {};
 SceneImages(const SceneImages&,RestoreTarget);
 std::shared_ptr<const Image> loadBitmap(const std::string&);
 std::shared_ptr<Composite> sceneComposite(const std::string&);
 bool needsWarmFile(const std::string&,bool upcoming)const;
 void clearComposites();
 static float value(const Layer&,const std::string&,float);
 static std::string filterName(const std::map<std::string,std::string>&,unsigned);
 void loadCharacterExtension(Layer&,const std::string&);
 void loadReplacement(Layer&,const std::string&);
 void transition(Layer&,const Layer&,bool,bool);
 void setWave(Layer&,std::string);
 static std::array<float,3> sampleWave(const Layer&);
 bool cameraCommand(const std::string&);
 void render(qlie::Bytes&,unsigned pass,RenderTarget*target=nullptr,float maxPriority=999,bool liveVideo=false,const Affine2D* view=nullptr)const;
 void enqueue(RenderQueue&,unsigned pass,float maxPriority,bool liveVideo)const;
 std::string mappedFace(std::string)const;
 std::string faceResource(const std::string&,bool packedOnly)const;
 std::string expand(std::string,const std::string&)const;
public:
 explicit SceneImages(qlie::Assets&);
 void setReadAhead(AssetReadAhead*loader){readAhead=loader;}
 void setNativeVideoSurfaces(bool enable){prefetchNativeVideo=enable;}
 struct CharacterPrefixes {
  std::map<std::string,std::string> simple;
  std::map<std::string,std::map<std::string,std::string>> parts;
 };
 struct BackgroundBlur {
  std::string layer,name;
  std::shared_ptr<const Image> source;
  std::string key()const;
 };
 std::optional<BackgroundBlur> backgroundBlur(const std::string&)const;
 void warmBackgroundBlur(const BackgroundBlur&,bool required=false);
 void installBackgroundBlur(const BackgroundBlur&);
 std::vector<std::string> warmPaths(const std::string&,CharacterPrefixes* future=nullptr)const;
 bool warmFile(const std::string&,bool required=false,bool prepareVideo=true);
 void draw(RenderTarget&,bool liveVideo=false,const Affine2D* view=nullptr)const;
 void drawScene(RenderTarget&,bool liveVideo=false)const;void drawFaces(RenderTarget&)const;void drawMessages(RenderTarget&)const;
 RenderQueue captureQueue(bool shaderMasks,bool nativeVideo=false)const;
 std::set<std::string> takeShownFiles(){std::set<std::string> out;out.swap(shownFiles);return out;}
 // UI slots are transient presentation state owned by title/choice screens.
 void uiSlot(const std::string&layer,unsigned slot,const std::string&asset);
 void uiSlot(const std::string&layer,unsigned slot,const Image&);
 void prepareUiBitmap(const std::string&asset);
 void warmUiBitmaps(const std::string&key,std::vector<std::string> paths);
 void takeUiBitmaps(const std::string&key);
 void setUiMotion(const std::string&layer,const char*motion);
 void removeUiLayer(const std::string&layer,bool releaseAsset=false);
 void resetUiAnimations();
 void seekUiLayer(const std::string& layer,double milliseconds);
 bool uiHit(const std::string&layer,float x,float y)const;
 std::pair<unsigned,unsigned> dimensions(const std::string&layer)const;
 void uiExternal(const std::string&layer,unsigned index,float value);
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
 std::unique_ptr<SceneImages> makeRestoreTarget()const;
 std::vector<SceneSoundEvent> takeSoundEvents();
 void update(double milliseconds);bool command(const std::string&);void clear(bool keepBitmaps=false);void beginPage();
 std::optional<std::string> selectFace(const std::string&speaker,const std::string&voice,bool deferCold=false);
 void draw(qlie::Bytes&,bool liveVideo=false,const Affine2D* view=nullptr)const;void drawThumbnail(qlie::Bytes&,float maxPriority)const;void drawScene(qlie::Bytes&,bool liveVideo=false)const;void drawFaces(qlie::Bytes&)const;void drawMessages(qlie::Bytes&)const;
 void setViewPan(std::string layer,float horizontal,float vertical);
 void moveViewPan(float dx,float dy);
 void pointViewPan(float x,float y);
 bool hasViewPan()const{return !panLayer.empty();}
 void setAlternateMessageWindow(bool alternate){alternateMessageWindow=alternate;}
 void setWindowAlpha(unsigned alpha){windowAlpha=alpha>255?255:alpha;}
 bool messageVisible()const;MessageStyle messageStyle()const;void setMessageImage(Image,bool show=false);
 SpeakerStyle speakerStyle()const;
 void setMessageImage(std::shared_ptr<const Image>,bool show=false);
};
}
