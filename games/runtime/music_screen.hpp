// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene_images.hpp"
#include "audio.hpp"
#include "settings.hpp"
#include <optional>
#include <algorithm>
#ifdef QLIE_PROFILE_WHJ6
#include <random>
#endif
namespace whj2 {
class MusicScreen {
 qlie::Assets& assets;Audio& audio;Settings& settings;SceneImages scene;MessageText labelText;KFont*font;
 struct Track {std::string title,path,button{},caption{};};std::vector<Track> tracks;
 bool prepared=false,opened=false,loop=false,audible=false,dirty=false;int selected=0,playing=-1,volumeValue=100,page=0;std::string caption;
 float shownSeek=-1;
 int shownVolume=-1;
 std::optional<float> seekFraction;
 int64_t seekOffsetMs=0;
 std::map<std::string,std::string> motions;
 std::optional<Image> legacyCaption;
 enum Control { Previous,Play,Next,Loop,Return,Seek,Volume,Album,Memory,
#ifdef QLIE_PROFILE_WHJ6
  Stop,RepeatAll,Shuffle,
#endif
  Page };
 int trackCount()const{return int(tracks.size());}
 bool paged()const{return trackCount()==37;}
 int trackRows()const{
#ifdef QLIE_PROFILE_WHJ6
  return 7;
#else
  return paged()?7:trackCount()==18?6:8;
#endif
 }
 int perPage()const{return paged()?21:trackCount();}
 int pageCount()const{return (trackCount()+perPage()-1)/perPage();}
 int lastTrackOnPage()const{return std::min(trackCount(),(page+1)*perPage())-1;}
 int seekY()const{return paged()?607:92;}
 void showPage(int);
 int control(Control c)const{return trackCount()+c;}
 void setMotion(const std::string&,const char*);
 void highlight();void play(int);void text(const std::string&,unsigned,const std::string&,unsigned,unsigned,unsigned);void refresh();
 void clearSeek(){seekFraction.reset();seekOffsetMs=0;}
 void uiSound(const char*);
#ifdef QLIE_PROFILE_WHJ6
 enum class Repeat {Off,One,All,Random};
 Repeat repeat=Repeat::Off;
 std::mt19937 shuffle{static_cast<unsigned>(SDL_GetPerformanceCounter())};
 void stopWhj6();
 void prepareWhj6();
 bool moveWhj6(int,int);
 int nextWhj6(bool automatic);
#endif
public:
 enum Destination {NoDestination,Title,CG,Replay};
 MusicScreen(qlie::Assets&,Audio&,Settings&,KFont*,const MessageText&,int expectedTracks=0);
 void prepare();
 bool takeSettingsChanged(){bool value=dirty;dirty=false;return value;}
 void open();void close();bool visible()const{return opened;}
 void update(double);void draw(qlie::Bytes&out,const Affine2D*view=nullptr)const;
 void draw(RenderTarget&out,const Affine2D*view=nullptr)const;
 void move(int,int);Destination confirm();Destination back();Destination tap(float,float);bool drag(float,float);
 int selection()const{return selected;}int playingTrack()const{return playing;}
};
}
