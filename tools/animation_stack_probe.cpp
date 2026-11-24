// SPDX-License-Identifier: GPL-2.0-or-later
#include "animation.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace whj2;
void check(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
AnimationLayer layer(unsigned mode,int x,int sx,unsigned kind=4){
 AnimationLayer l{"test",18,1,uint8_t(mode),{qlie::Bytes(162)}};
 auto&k=l.keys[0];k[4]=kind;
 auto put=[&](size_t o,int v){k[o]=v&255;k[o+1]=(v>>8)&255;};
 put(0x18,x);put(0x1c,sx);put(0x1e,100);put(0x35,255);
 k[0x28]=k[0x29]=k[0x2a]=255;return l;
}
int main(){try{
 auto zero=layer(0,0,100,1);zero.keys[0][0x1c]=0xd7;zero.keys[0][0x1d]=0x7f;zero.keys[0][0x1e]=0xd8;zero.keys[0][0x1f]=0x7f;
 auto zeroSample=sampleAnimation(zero,0,AnimationContext{0,0,0,0});check(zeroSample.scaleX==0&&zeroSample.scaleY==0&&zeroSample.unsupported.empty(),"zero-sized context is a value");
 auto randomLayer=layer(0,0,100);auto&rk=randomLayer.keys[0];
 rk[0x51]=5;rk[0x52]=13;rk[0x55]=0xf9;rk[0x56]=0xff;rk[0x57]=9;rk[0x59]=10;
 AnimationContext randomContext;randomContext.randomSeed=1023;
 auto randomAt0=sampleAnimation(randomLayer,0,randomContext),randomAt9=sampleAnimation(randomLayer,9,randomContext),randomAt10=sampleAnimation(randomLayer,10,randomContext);
 auto table=particleRandomTable();
 float expected=-7+16*float(double(table[1023]&0xffffff)/16777215.);
 check(std::abs(randomAt0.x-expected)<.0001f&&randomAt0.x==randomAt9.x,"random5 stable within quotient interval");
 expected=-7+16*float(double(table[0]&0xffffff)/16777215.);
 check(std::abs(randomAt10.x-expected)<.0001f&&randomAt10.unsupported.empty(),"random5 quotient and table wrapping");
 Animation looping;looping.bounds={1280,720,1000,200};
 check(nestedAnimationTime(looping,1000)==200&&nestedAnimationTime(looping,1500)==700,"child loop wrapping");
 check(nestedAnimationTime(looping,100)==100&&nestedAnimationTime(looping,200)==200,"child loop intro");
 looping.bounds[2]=0;check(nestedAnimationTime(looping,1500)==1500,"unbounded child timeline");

 Motion m{"nested",{layer(1,10,200),layer(1,3,50),layer(0,7,100),layer(2,2,100),layer(0,7,100),layer(2,0,100),layer(0,7,100)}};
 auto s=sampleMotion(m,0);
 check(s.size()==7,"sample count");
 check(s[0].x==20&&s[0].scaleX==400,"push then apply self");
 check(s[1].x==16&&s[1].scaleX==50,"nested push");
 check(s[2].x==20&&s[2].scaleX==100,"nested child");
 check(s[3].x==15,"pop applies current group first");
 check(s[4].x==17&&s[4].scaleX==200,"parent group after pop");
 check(s[6].x==7&&s[6].scaleX==100,"stack empty after second pop");
 auto hidden=layer(1,100,200,0);m.layers={hidden,layer(2,3,100),layer(0,4,100)};
 s=sampleMotion(m,0);check(s[1].x==103&&s[2].x==4,"zero primitive still controls transform group");
 m.layers[0].flags=0;s=sampleMotion(m,0);check(s[1].x==3&&s[2].x==4,"disabled group and empty pop");
 m.layers={layer(1,0,100),layer(0,0,100)};
 for(auto&l:m.layers){l.keys[0][0x24]=200;l.keys[0][0x28]=128;l.keys[0][0x35]=128;}
 s=sampleMotion(m,0);check((s[1].addColor&255)==255,"saturating add");
 check((s[1].mulColor&255)==64&&s[1].alpha==64,"integer tint and opacity");
 m.layers={layer(1,0,200),layer(0,0,50,3)};
 s=sampleMotion(m,0);check(s[1].scaleX==50,"primitive3 scale exception");
 auto near=[](float a,float b){return std::abs(a-b)<0.002f;};
 auto z=layer(0,0,100);z.keys[0][0x22]=90;z.keys[0][0x4a]=12;
 auto single=sampleAnimation(z,0);check(single.rotationZ==90,"Z comes from0x22, not Y0x4a");
 check(!single.unsupported.empty(),"3D rotation stays explicit");
 m.layers={layer(0,10,200,16),layer(0,0,100),layer(0,3,50,16),layer(0,0,100),layer(0,0,100,17),layer(0,0,100),layer(0,0,100,17),layer(0,0,100)};
 s=sampleMotion(m,0,100,80);
 check(near(s[1].screen.a,2)&&near(s[1].screen.x,-30),"render translate then center-scale");
 check(near(s[3].screen.a,1)&&near(s[3].screen.x,13),"render nested composition");
 check(near(s[5].screen.a,2)&&near(s[5].screen.x,-30),"render pop restores parent");
 check(near(s[7].screen.a,1)&&near(s[7].screen.x,0),"render second pop restores identity");
 m.layers={layer(0,0,100,16),layer(0,0,100)};
 m.layers[0].keys[0][0x22]=90;s=sampleMotion(m,0,100,80);
 check(near(s[1].screen.a,0)&&near(s[1].screen.b,1)&&near(s[1].screen.c,-1),"render rotation orientation");
 check(near(s[1].screen.x,10)&&near(s[1].screen.y,90),"render rotation screen center");
 m.layers[0].flags=0;s=sampleMotion(m,0);check(s[1].screen.a==1&&s[1].screen.x==0,"disabled render-control layer");
 auto wave=layer(0,0,100,16);
 auto set16=[&](size_t o,int value){wave.keys[0][o]=value&255;wave.keys[0][o+1]=(value>>8)&255;};
 wave.keys[0][0]=232;wave.keys[0][1]=3; // key starts at1000ms
 wave.keys[0][0x51]=7;wave.keys[0][0x52]=13;
 set16(0x55,-4);set16(0x57,4);set16(0x59,80);
 check(near(sampleAnimation(wave,1020).x,4),"sine uses key-local phase");
 check(near(sampleAnimation(wave,1060).x,-4),"sine negative quarter");
 wave.keys[0][0x51]=8;check(near(sampleAnimation(wave,1000).x,4),"cosine phase");
 wave.keys[0][0x51]=9;check(near(sampleAnimation(wave,1040).x,4),"triangle peak");
 wave.keys[0][0x51]=10;check(near(sampleAnimation(wave,1039).x,-4)&&near(sampleAnimation(wave,1040).x,4),"square edge");
 wave.keys[0][0x51]=4;check(near(sampleAnimation(wave,1020).x,-4),"constant track");
 wave.keys[0][0x51]=1;check(!sampleAnimation(wave,1020).unsupported.empty(),"unimplemented random stays visible");
 auto symbolic=layer(0,0,100,1);
 auto putSymbol=[&](size_t at,int v){symbolic.keys[0][at]=v&255;symbolic.keys[0][at+1]=(v>>8)&255;};
 putSymbol(0x1c,32727);putSymbol(0x1e,32728);
 auto resolved=sampleAnimation(symbolic,0,AnimationContext{320,180,1280,720});
 check(resolved.scaleX==320&&resolved.scaleY==180,"symbols refer to image bounds");
 putSymbol(0x1c,32729);putSymbol(0x1e,32730);
 resolved=sampleAnimation(symbolic,0,AnimationContext{320,180,1920,1080});
 check(resolved.scaleX==1920&&resolved.scaleY==1080,"target symbols use target bounds");
 check(!sampleAnimation(symbolic,0).unsupported.empty(),"missing context is explicit");
 std::cout<<"PASS nested push/apply/pop, inactive keys, empty pop, tint, alpha, primitive3\n";
 return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
