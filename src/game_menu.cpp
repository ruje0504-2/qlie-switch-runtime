// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_menu.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
namespace whj2 {
const Image& GameMenu::resource(const std::string&path){auto it=cache.find(path);if(it==cache.end())it=cache.emplace(path,image(assets.read(path))).first;return it->second;}
void GameMenu::put(RenderQueue&q,const std::string&path,float x,float y,bool center){auto&im=resource(path);q.image(im,imagePlacement(x,y,1,1,0,center?.5f:0,center?.5f:0,im.width,im.height),0,255,0,0xffffff);}
void GameMenu::move(int dx,int dy){if(asking){if(dx||dy)focus=1-focus;return;}
 if(panel==Save||panel==Load){if(dx)focus=(focus+4)%8;if(dy)focus=(focus+dy+8)%8;}
 else if(panel==History)historyOffset=std::max(0,historyOffset-dy);
 else if(panel==Settings)focus=(focus+dx+dy+3)%3;
}
GameMenu::Action GameMenu::confirm()const{if(asking)return focus==0?Yes:No;if(panel==Save||panel==Load)return Slot;if(panel==Settings)return focus==0?Close:focus==1?Title:Quit;return Close;}
GameMenu::Action GameMenu::tap(float x,float y){if(asking){if(std::abs(y-325)<=20){if(std::abs(x-550)<=60){focus=0;return Yes;}if(std::abs(x-730)<=60){focus=1;return No;}}return Nothing;}
 if(panel==Save||panel==Load){if(y>=72&&y<=108&&x>=674){page=std::clamp(int((x-674)/40),0,14);focus=0;return Nothing;}if(y>=128&&y<=651){int col=x>=645?1:0;int row=int((y-128)/135);if(row<4){focus=col*4+row;return Slot;}}}
 if(y>=670){if(x>1145)return Close;if(panel==Settings){if(x>885)return Title;if(x>755)return Quit;}else if(panel==Save||panel==Load){if(x>1015)return Title;if(x>885)return Quit;}}
 return Nothing;
}
void GameMenu::draw(qlie::Bytes&pixels){RenderQueue q;
 if(panel==Save||panel==Load){std::string dir="script/saveload/";put(q,dir+"背景.png",0,0);put(q,dir+(panel==Save?"アッパーフレーム_セーブ.png":"アッパーフレーム_ロード.png"),0,0);put(q,dir+"アンダーフレーム.png",1,656);
 for(int i=0;i<15;i++){char n[8];std::snprintf(n,sizeof(n),"%02d",i+1);put(q,dir+"ページ_"+n+(i==page?"_on.png":"_off.png"),694+i*40,90,true);}
 for(int i=0;i<8;i++){float x=i<4?323:956,y=182+(i%4)*135;put(q,dir+(focus==i?"セーブファイル_on.png":"セーブファイル_off.png"),x,y,true);put(q,dir+"サムネイル_no_data.png",i<4?27:660,137+(i%4)*135);}
 put(q,dir+"戻る_off.png",1211,695,true);put(q,dir+"タイトルへ戻る_off.png",1080,695,true);put(q,dir+"ゲームの終了_off.png",950,695,true);
 }else if(panel==History){for(size_t i=0;i<pixels.size();i+=4)for(int c=0;c<3;c++)pixels[i+c]=uint8_t(pixels[i+c]*.3);put(q,"script/log/アッパーフレーム.png",0,0);put(q,"script/log/アンダーフレーム.png",0,658);put(q,"script/log/ゲームに戻る_off.png",1208,694,true);
 }else if(panel==Settings){put(q,"script/config/背景.png",0,0);put(q,"script/config/設定フレーム.png",139,89);put(q,"script/config/アッパーフレーム.png",0,0);put(q,"script/config/アンダーフレーム.png",0,656);
 const char*names[]={"戻る","タイトルへ戻る","ゲームの終了"};const int xs[]={1210,950,820};for(int i=0;i<3;i++)put(q,std::string("script/config/")+names[i]+(focus==i?"_on.png":"_off.png"),xs[i],695,true);
 }
 q.draw(pixels,1280,720);
 if(asking){for(size_t i=0;i<pixels.size();i+=4)for(int c=0;c<3;c++)pixels[i+c]=uint8_t(pixels[i+c]*.375);RenderQueue dialog;put(dialog,"script/config/確認_フレーム.png",640,300,true);put(dialog,std::string("script/config/確認_はい")+(focus==0?"_on.png":"_off.png"),550,325,true);put(dialog,std::string("script/config/確認_いいえ")+(focus==1?"_on.png":"_off.png"),730,325,true);dialog.draw(pixels,1280,720);}
}
}
