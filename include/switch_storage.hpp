// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <stdexcept>
#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif
namespace whj2 {
// Installed titles read their bundled data from RomFS. Homebrew keeps its SD paths.
class SwitchStorage {
 #ifdef __SWITCH__
 bool romfs=false,save=false;
 #endif
public:
 SwitchStorage()=default;
 SwitchStorage(const SwitchStorage&)=delete;
 SwitchStorage&operator=(const SwitchStorage&)=delete;
 void mount(std::string&game,std::string&saves){
#ifdef __SWITCH__
  if(R_FAILED(romfsMountSelf("romfs")))return;
  romfs=true;
  Result result=accountInitialize(AccountServiceType_Application);
  if(R_FAILED(result))throw std::runtime_error("Cannot initialize installed-title account");
  AccountUid uid{};result=accountGetPreselectedUser(&uid);accountExit();
  if(R_FAILED(result))throw std::runtime_error("Cannot obtain installed-title save user");
  result=fsdevMountSaveData("save",FS_SAVEDATA_CURRENT_APPLICATIONID,uid);
  if(R_FAILED(result))throw std::runtime_error("Cannot mount installed-title SaveData");
  save=true;game="romfs:";saves="save:";
#else
  (void)game;(void)saves;
#endif
 }
 ~SwitchStorage(){
#ifdef __SWITCH__
  if(save)fsdevUnmountDevice("save");
  if(romfs)romfsUnmount("romfs");
#endif
 }
};
inline void prepareSwitchLogDirectory(){
#ifdef __SWITCH__
 fsdevMountSdmc();mkdir("sdmc:/switch",0755);mkdir("sdmc:/switch/whj2",0755);
#endif
}
inline void commitSavePath(const std::string&path){
#ifdef __SWITCH__
 if(path.rfind("save:/",0)==0&&R_FAILED(fsdevCommitDevice("save")))
  throw std::runtime_error("Cannot commit installed-title SaveData");
#else
 (void)path;
#endif
}
}
