#import <AppKit/AppKit.h>
#include "platform.hpp"
#include "depth_bridge.hpp"
#include "runtime_config.hpp"
#include <codecvt>
#include <locale>

namespace s3d::premiere {
std::wstring wide(const std::string& s) {return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(s);}
std::string utf8(const std::wstring& s) {return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(s);}
void launch_worker() {
  @autoreleasepool {
    const auto root=s3d::project_root();
    auto python=root/".venv310/bin/python";
    if(const auto* override_path=std::getenv("S3D_PYTHON"))python=override_path;
    NSTask* task=[[NSTask alloc] init];
    task.executableURL=[NSURL fileURLWithPath:[NSString stringWithUTF8String:python.c_str()]];
    task.currentDirectoryURL=[NSURL fileURLWithPath:[NSString stringWithUTF8String:root.c_str()]];
    task.arguments=@[@"-m", @"stereodepth_worker.server", @"--connection",
      [NSString stringWithUTF8String:(root/"runtime/connection.json").c_str()], @"--models",
      [NSString stringWithUTF8String:(root/"models").c_str()]];
    task.standardOutput=[NSFileHandle fileHandleWithNullDevice];
    task.standardError=[NSFileHandle fileHandleWithNullDevice];
    NSError* error=nil;
    if(![task launchAndReturnError:&error])
      throw std::runtime_error("Cannot start depth worker. Run scripts/start-worker.sh: "+std::string(error.localizedDescription.UTF8String));
  }
}
int browse_depth_source(std::string& name) {
  // Premiere invokes parameter UI callbacks on its UI thread. AppKit dialogs
  // must never be opened on a background render thread.
  if(![NSThread isMainThread])throw std::runtime_error("Open the depth picker from Premiere Effect Controls");
  @autoreleasepool {
    NSOpenPanel* panel=[NSOpenPanel openPanel];
    panel.title=@"Choose depth video or first image in depth sequence";
    panel.canChooseDirectories=NO;panel.allowsMultipleSelection=NO;
    if([panel runModal]!=NSModalResponseOK)return 0;
    NSString* path=panel.URL.path;
    NSString* ext=path.pathExtension.lowercaseString;
    std::string mode="auto";
    if([@[@"png", @"exr", @"tif", @"tiff", @"pfm"] containsObject:ext]) {
      NSAlert* alert=[[NSAlert alloc] init];
      alert.messageText=@"Import external depth";
      alert.informativeText=@"Use the following numbered images as a sequence, or use this image as a still depth pass.";
      [alert addButtonWithTitle:@"Image Sequence"];
      [alert addButtonWithTitle:@"Still Image"];
      [alert addButtonWithTitle:@"Cancel"];
      auto response=[alert runModal];
      if(response==NSAlertThirdButtonReturn)return 0;
      mode=response==NSAlertFirstButtonReturn?"sequence":"still";
    }
    return register_depth_source(wide(path.UTF8String),name,mode);
  }
}
}
