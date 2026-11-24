// SPDX-License-Identifier: GPL-2.0-or-later
#import <AppKit/AppKit.h>
#include "mac_game_access.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static NSURL *gameDirectory;
int whj2_mac_can_read_game(const char *path){
 NSString *root=[NSString stringWithUTF8String:path];
 const char *key=[[root stringByAppendingPathComponent:@"DLL/key.fkey"] fileSystemRepresentation];
 FILE *file=std::fopen(key,"rb");if(!file)return 0;std::fclose(file);return 1;
}
char *whj2_mac_choose_game(const char *initial){
 @autoreleasepool {
  [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseFiles=NO;panel.canChooseDirectories=YES;panel.allowsMultipleSelection=NO;panel.canCreateDirectories=NO;
  panel.title=@"选择游戏目录";panel.message=@"请选择包含 GameData 和 DLL/key.fkey 的游戏目录，以允许读取游戏资源。";panel.prompt=@"选择游戏目录";
  panel.directoryURL=[NSURL fileURLWithPath:[NSString stringWithUTF8String:initial] isDirectory:YES];
  if([panel runModal]!=NSModalResponseOK)return nullptr;
  gameDirectory=panel.URL;[gameDirectory startAccessingSecurityScopedResource];
  const char *path=gameDirectory.fileSystemRepresentation;
  if(!whj2_mac_can_read_game(path)){
   NSAlert *alert=[[NSAlert alloc] init];alert.messageText=@"无法读取游戏文件";alert.informativeText=@"请选择含 DLL/key.fkey 的完整游戏目录。";[alert runModal];return nullptr;
  }
  return strdup(path);
 }
}
