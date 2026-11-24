// SPDX-License-Identifier: GPL-2.0-or-later
// Resource/control-flow diagnostic only; this is not the graphical game.
#include "whj2/profile.hpp"
#include <switch.h>
#include "story.hpp"
#include <cstdio>
int main(int argc,char**argv){consoleInit(nullptr);padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);int result=0;try{std::string root=argc>1?argv[1]:whj2::profile::sdGame;printf("WHJ2 native diagnostic\nGame: %s\n",root.c_str());qlie::Assets assets(root,whj2::profile::archives);printf("Archive index: %zu effective resources\n",assets.names().size());whj2::Story story(assets,whj2::profile::story);story.start();unsigned text=0;for(unsigned i=0;i<100000;i++){auto e=story.next();if(e.type=="text")text++;if(e.type=="choice"){printf("First choice reached: %s:%u\nText events: %u\n",e.file.c_str(),e.line,text);break;}if(e.type=="finish")break;}printf("Diagnostic finished. Graphical runtime remains under development.\n");}catch(const std::exception&e){printf("FAIL: %s\n",e.what());result=1;}printf("Press + to exit.\n");while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;consoleUpdate(nullptr);}consoleExit(nullptr);return result;}
