// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene_images.hpp"
#include "audio.hpp"
#include "settings.hpp"
namespace whj2 {
class MusicScreen {
 qlie::Assets& assets;Audio& audio;Settings& settings;SceneImages scene;KFont*font;
 struct Track {std::string title,path;};std::vector<Track> tracks;
 bool opened=false,loop=false,audible=false,dirty=false;int selected=0,playing=-1,volumeValue=100;std::string caption;
 void highlight();void play(int);void text(const std::string&,unsigned,const std::string&,unsigned,unsigned,unsigned);void refresh();
public:
 enum Destination {NoDestination,CG,Replay};
 Destination destination=NoDestination;
 Destination takeDestination(){auto value=destination;destination=NoDestination;return value;}
 MusicScreen(qlie::Assets&,Audio&,Settings&,KFont*);
 bool takeSettingsChanged(){bool value=dirty;dirty=false;return value;}
 void open();void close();bool visible()const{return opened;}
 void update(double);void draw(qlie::Bytes&out)const{scene.draw(out);}
 void move(int,int);void confirm();bool back();bool tap(float,float);bool drag(float,float);
 int selection()const{return selected;}int playingTrack()const{return playing;}
};
}
