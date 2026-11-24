// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include "progress.hpp"
#include "state_io.hpp"
#include <cstdio>
namespace whj2 {
namespace {const char*keys[]={"#game_aclearflag","#game_bclearflag","#game_cclearflag","#game_dclearflag"};}
bool Progress::capture(const Story&story){bool changed=false;for(unsigned i=0;i<4;i++){auto at=story.state.vars.find(keys[i]);if(at!=story.state.vars.end()&&at->second=="1"&&!cleared[i]){cleared[i]=true;changed=true;}}for(auto&label:story.visited)if(labels.insert(label).second)changed=true;return changed;}
bool Progress::captureFiles(const std::set<std::string>&shown){bool changed=false;for(auto&path:shown)if(files.insert(qlie::canonical(path)).second)changed=true;return changed;}
void Progress::apply(Story&story)const{for(unsigned i=0;i<4;i++)if(cleared[i])story.state.vars[keys[i]]="1";}
qlie::Bytes Progress::save()const{StateWriter out;out.str("WHJ2-progress-3");unsigned mask=0;for(unsigned i=0;i<4;i++)if(cleared[i])mask|=1u<<i;out.n(mask);out.n(labels.size());for(auto&label:labels)out.str(label);out.n(files.size());for(auto&file:files)out.str(file);out.n(readPages.size());for(auto&key:readPages)out.str(key);return out.bytes;}
void Progress::restore(const qlie::Bytes&bytes){StateReader in{bytes};auto version=in.str();if(version!="WHJ2-progress-1"&&version!="WHJ2-progress-2"&&version!="WHJ2-progress-3")throw std::runtime_error("System progress version");auto mask=in.n();if(mask>15)throw std::runtime_error("System progress route flags");auto count=in.n();if(count>1000000)throw std::runtime_error("System progress label count");std::set<std::string> restored;while(count--)restored.insert(in.str());std::set<std::string> restoredFiles;if(version=="WHJ2-progress-2"||version=="WHJ2-progress-3"){count=in.n();if(count>1000000)throw std::runtime_error("System progress file count");while(count--)restoredFiles.insert(qlie::canonical(in.str()));}std::set<std::string> restoredRead;if(version=="WHJ2-progress-3"){count=in.n();if(count>1000000)throw std::runtime_error("Read page count");while(count--)restoredRead.insert(in.str());}in.end();for(unsigned i=0;i<4;i++)cleared[i]=(mask&(1u<<i))!=0;labels=std::move(restored);files=std::move(restoredFiles);readPages=std::move(restoredRead);}
ProgressWriter::ProgressWriter(std::string file):path(std::move(file)),worker([this]{run();}){}
ProgressWriter::~ProgressWriter(){
 {
  std::lock_guard lock(mutex);
  stopping=true;
 }
 changed.notify_all();
 worker.join();
}
void ProgressWriter::schedule(qlie::Bytes snapshot){
 {
  std::lock_guard lock(mutex);
  if(failure)std::rethrow_exception(failure);
  // Every snapshot includes all earlier changes. Keep only the most recent
  // one while an older snapshot is being written.
  pending=std::move(snapshot);
 }
 changed.notify_all();
}
void ProgressWriter::flush(){
 std::unique_lock lock(mutex);
 changed.wait(lock,[this]{return failure||(!pending&&!writing);});
 if(failure)std::rethrow_exception(failure);
}
void ProgressWriter::run(){
 std::unique_lock lock(mutex);
 for(;;){
  changed.wait(lock,[this]{return pending.has_value()||stopping;});
  if(!pending)return;
  auto snapshot=std::move(*pending);
  pending.reset();
  writing=true;
  lock.unlock();
  try{
   qlie::write_file(path,snapshot);
  }catch(...){
   QLIE_LOGF("PROGRESS write failed for %s\n",path.c_str());
   lock.lock();
   writing=false;
   pending.reset();
   failure=std::current_exception();
   changed.notify_all();
   return;
  }
  lock.lock();
  writing=false;
  changed.notify_all();
 }
}
}
