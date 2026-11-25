// SPDX-License-Identifier: GPL-2.0-or-later
#import <AppKit/AppKit.h>
#include "mac_game_access.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static NSURL *gameDirectory;
int whj2_mac_can_read_game(const char *path,const char *keyPath){
 NSString *root=[NSString stringWithUTF8String:path];
 NSString *relativeKey=[NSString stringWithUTF8String:keyPath];
 const char *key=[[root stringByAppendingPathComponent:relativeKey] fileSystemRepresentation];
 FILE *file=std::fopen(key,"rb");if(!file)return 0;std::fclose(file);return 1;
}
char *whj2_mac_choose_game(const char *initial,const char *keyPath){
 @autoreleasepool {
  [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseFiles=NO;panel.canChooseDirectories=YES;panel.allowsMultipleSelection=NO;panel.canCreateDirectories=NO;
  NSString *relativeKey=[NSString stringWithUTF8String:keyPath];
  panel.title=@"选择游戏目录";
  panel.message=[NSString stringWithFormat:@"请选择包含 GameData 和 %@ 的游戏目录。",relativeKey];
  panel.prompt=@"选择游戏目录";
  panel.directoryURL=[NSURL fileURLWithPath:[NSString stringWithUTF8String:initial] isDirectory:YES];
  if([panel runModal]!=NSModalResponseOK)return nullptr;
  gameDirectory=panel.URL;[gameDirectory startAccessingSecurityScopedResource];
  const char *path=gameDirectory.fileSystemRepresentation;
  if(!whj2_mac_can_read_game(path,keyPath)){
   NSAlert *alert=[[NSAlert alloc] init];alert.messageText=@"无法读取游戏文件";
   alert.informativeText=[NSString stringWithFormat:@"请选择含 %@ 的完整游戏目录。",relativeKey];
   [alert runModal];return nullptr;
  }
  return strdup(path);
 }
}
char *whj2_mac_choose_player_name(void){
 @autoreleasepool {
  NSAlert *alert=[[NSAlert alloc] init];
  alert.messageText=@"请输入您的名字";
  alert.informativeText=@"最多 10 个字";
  [alert addButtonWithTitle:@"确定"];
  [alert addButtonWithTitle:@"取消"];
  NSTextField *input=[[NSTextField alloc] initWithFrame:NSMakeRect(0,0,250,24)];
  alert.accessoryView=input;
  [alert.window setInitialFirstResponder:input];
  if([alert runModal]!=NSAlertFirstButtonReturn)return nullptr;
  return strdup(input.stringValue.UTF8String);
 }
}
