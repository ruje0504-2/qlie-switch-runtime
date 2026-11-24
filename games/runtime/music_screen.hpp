// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene_images.hpp"
#include "audio.hpp"
#include "settings.hpp"
#include <optional>
namespace whj2 {
class MusicScreen {
 qlie::Assets& assets;Audio& audio;Settings& settings;SceneImages scene;MessageText labelText;KFont*font;
 struct Track {std::string title,path;};std::vector<Track> tracks;
 bool prepared=false,opened=false,loop=false,audible=false,dirty=false;int selected=0,playing=-1,volumeValue=100;std::string caption;
 float shownSeek=-1;
 int shownVolume=-1;
 std::optional<float> seekFraction;
 int64_t seekOffsetMs=0;
 std::map<std::string,std::string> motions;
 enum Control { Previous,Play,Next,Loop,Return,Seek,Volume,Album,Memory };
 int trackCount()const{return int(tracks.size());}
 int trackRows()const{return trackCount()==18?6:8;}
 int control(Control c)const{return trackCount()+c;}
 void setMotion(const std::string&,const char*);
 void highlight();void play(int);void text(const std::string&,unsigned,const std::string&,unsigned,unsigned,unsigned);void refresh();
 void clearSeek(){seekFraction.reset();seekOffsetMs=0;}
 void uiSound(const char*);
public:
 enum Destination {NoDestination,Title,CG,Replay};
 MusicScreen(qlie::Assets&,Audio&,Settings&,KFont*);
 void prepare();
 bool takeSettingsChanged(){bool value=dirty;dirty=false;return value;}
 void open();void close();bool visible()const{return opened;}
 void update(double);void draw(qlie::Bytes&out,const Affine2D*view=nullptr)const{scene.draw(out,false,view);}
 void draw(RenderTarget&out,const Affine2D*view=nullptr)const{scene.draw(out,false,view);}
 void move(int,int);Destination confirm();Destination back();Destination tap(float,float);bool drag(float,float);
 int selection()const{return selected;}int playingTrack()const{return playing;}
};
}
