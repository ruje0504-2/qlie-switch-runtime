// SPDX-License-Identifier: GPL-2.0-or-later
#include "whj2/profile.hpp"
#include "sentence_wait.hpp"
#include "scene_transition.hpp"
#include "story.hpp"
#include "state_io.hpp"
#include <iostream>
#include <stdexcept>
void require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1],whj2::profile::archives);whj2::SceneTransition fade(assets);whj2::SentenceWait wait;qlie::Bytes frame(1280*720*4);
 fade.command("^sentence,autowait:true",frame,&wait);fade.command("^sentence,$overlap",frame,&wait);wait.page();require(wait.tick(499)==wait.None,"Auto wait ended early");require(wait.confirm()==wait.None,"Click prewait advanced page");require(wait.confirm()==wait.Next,"Message click missing");
 wait.property({"wait","waitonly","1000"});wait.page();require(wait.tick(999)==wait.None&&wait.confirm()==wait.None,"Fixed wait was skipped");auto state=wait.save();whj2::SentenceWait restored;restored.restore(state);require(restored.tick(1)==wait.Next,"Saved remaining wait");
 wait.property({"wait","wait","1000"});wait.page();require(wait.tick(1000)==wait.None&&wait.confirm()==wait.Next,"Wait then message");
 wait.property({"wait","clickonly","1000"});wait.page();require(wait.confirm()==wait.Next,"Click-only wait");
 fade.command("^sentence,$overlap_long,wait:click:50",frame,&wait);wait.page();require(wait.tick(49)==wait.None,"Explicit wait replaced by auto fade time");require(wait.tick(1)==wait.None&&wait.confirm()==wait.Next,"Explicit click wait boundary");
 fade.command("^sentence,$overlap",frame,&wait);fade.present();wait.page();
 wait.elapseSkip(499);fade.update(499);
 require(wait.skip()==wait.None,"Fast-forward advanced before the overlap completed");
 auto fading=frame;fade.blend(fading);
 wait.elapseSkip(1);fade.update(1);
 require(wait.skip()==wait.Next,"Fast-forward stalled at the completed overlap");
 fade.command("^sentence,wait:clickonly:7000:0",frame,&wait);wait.page();
 require(wait.skip()==wait.Next,"Explicit zero skip time delayed the opening");
 wait.property({"wait","click","1000","$64"});wait.page();wait.elapseSkip(70);
 auto skipState=wait.save();restored.restore(skipState);
 restored.elapseSkip(29);require(restored.skip()==wait.None,"Saved skip delay ended early");
 restored.elapseSkip(1);require(restored.skip()==wait.Next,"Saved skip delay did not finish");
 auto legacy=skipState;legacy[8+std::string("WHJ2-wait-1").size()-1]='1';legacy.resize(legacy.size()-16);
 restored.restore(legacy);require(restored.skip()==wait.Next,"Legacy wait state did not restore");
 wait.page();require(wait.skip()==wait.Next,"Skip duration persisted into the next unconfigured page");
 fade.command("^sentence,wait:clickonly:7000:0",frame,&wait);wait.page();
 require(wait.confirm()==wait.Next,"Original click-only wait did not consume its click");
 wait.page();
 require(wait.waitingForMessage()&&wait.tick(7000)==wait.None&&wait.confirm()==wait.Next,
         "Unconfigured page inherited the previous click-only wait");
 whj2::StateWriter oldPage;
 oldPage.str("WHJ2-wait-1");oldPage.str("clickonly");oldPage.f(7000);oldPage.f(2000);
 for(auto v:{1,0,1,0,0,0})oldPage.n(v);
 restored.restore(oldPage.bytes);
 require(restored.tick(1999)==wait.None&&restored.tick(1)==wait.Next,"Legacy active wait lost its remaining delay");
 restored.page();require(restored.waitingForMessage(),"Legacy page parameters leaked into its successor");
 wait.property({"wait","waitonly","300"});wait.native(true,20);
 require(wait.confirm()==wait.Resume,"Native wait did not resume");
 wait.page();require(wait.tick(299)==wait.None&&wait.tick(1)==wait.Next,
                     "Native continuation lost the pending sentence wait");
 wait.property({"wait","skip"});wait.page();require(wait.tick(0)==wait.Next,"Skip did not continue");
 wait.native(false,1000);require(wait.confirm()==wait.None&&wait.tick(1000)==wait.Resume,"Native fixed continuation");wait.native(true,1000);require(wait.confirm()==wait.Resume,"Native click continuation");
 wait.property({"wait","voice","5000"});wait.page(true,false);require(wait.tick(249)==wait.None&&wait.tick(1)==wait.Next,"Missing voice fallback");
 fade.command("^sentence,autowait:true,wait:clickonly:7000:1500",frame,&wait);
 fade.command("^sentence,clear:def,fade:cut",frame,&wait);wait.page();
 require(wait.waitingForMessage()&&wait.skip()==wait.Next,"Sentence reset kept a pending wait");
 fade.command("^sentence,$overlap",frame,&wait);wait.page();
 require(wait.waitingForMessage(),"Sentence reset kept automatic fade wait enabled");
 fade.command("^sentence,clear:def,fade:cut,wait:click:50:20",frame,&wait);wait.page();
 wait.elapseSkip(19);require(wait.skip()==wait.None,"Wait after sentence reset was lost");
 wait.elapseSkip(1);require(wait.skip()==wait.Next,"Wait after reset did not complete");
 whj2::Story story(assets,whj2::profile::story);story.state.at={"scenario/ed_movie.s",0};bool fixed=false,click=false;for(unsigned i=0;i<20&&!(fixed&&click);++i){auto event=story.next();if(event.type=="wait"&&event.value=="2000")fixed=true;if(event.type=="click-wait"&&event.value=="1000")click=true;}require(fixed&&click,"Original wait/clk distinction");
 std::cout<<"PASS original template auto/explicit wait, click consumption, fixed timing, remaining-state restore and native wait/clk continuation, overlap skip timing, explicit zero, and legacy save\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
