// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "image.hpp"
#include "switch_threads.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace whj2 {
struct Composite;
struct DecodedPcm {
 std::shared_ptr<const int16_t> samples;
 size_t count=0;
 bool empty()const{return count==0;}
 size_t size()const{return count;}
 int16_t operator[](size_t i)const{return samples.get()[i];}
};
struct WarmAsset {
 std::shared_ptr<const Image> bitmap;
 std::shared_ptr<Composite> composite;
 uint32_t identities=0;
 DecodedPcm pcm;
 size_t bytes=0;
};

// Keys are canonical game paths. Each ready result is consumed just once.
class AssetReadAhead {
public:
 struct Stats {unsigned ready=0,waited=0,urgent=0,cold=0;};
private:
 Stats totals; // Only the foreground consumer samples or changes these counters.
 using Loader=std::function<WarmAsset()>;
 struct Job {std::string path;Loader load;WarmAsset asset;bool running=false,done=false,claimed=false;std::exception_ptr error;};
 std::deque<std::shared_ptr<Job>> jobs;
 std::mutex mutex;std::condition_variable changed;bool stopping=false;
 std::thread worker;

 void run(){
#ifdef __SWITCH__
  logSwitchThread("asset-read-ahead",bindSwitchThread(1));
#endif
  std::unique_lock<std::mutex> lock(mutex);
  for(;;){
   std::shared_ptr<Job> job;
   changed.wait(lock,[&]{
    if(stopping)return true;
    for(auto&j:jobs)if(!j->done&&!j->running){job=j;return true;}
    return false;
   });
   if(stopping)return;
   job->running=true;lock.unlock();
   try{job->asset=job->load();}catch(...){job->error=std::current_exception();}
   lock.lock();job->load={};job->running=false;job->done=true;
   size_t bytes=0;
   for(auto&j:jobs)if(j->done)bytes+=j->asset.bytes;
   // Later hints are further from the current script position.
   for(size_t i=jobs.size();i&&bytes>48*1024*1024;){
    auto it=jobs.begin()+--i;
    if((*it)->done&&!(*it)->claimed){bytes-=(*it)->asset.bytes;jobs.erase(it);}
   }
   changed.notify_all();
  }
 }
public:
 AssetReadAhead():worker([this]{run();}){}
 ~AssetReadAhead(){
  {std::lock_guard<std::mutex> lock(mutex);stopping=true;}
  changed.notify_all();worker.join();
 }
 void request(const std::string&path,Loader load){
  std::lock_guard<std::mutex> lock(mutex);
  for(auto&job:jobs)if(job->path==path)return;
  if(jobs.size()>=6)return;
  auto job=std::make_shared<Job>();job->path=path;job->load=std::move(load);jobs.push_back(std::move(job));changed.notify_one();
 }
 void retain(const std::vector<std::string>&wanted){
  std::lock_guard<std::mutex> lock(mutex);
  for(auto it=jobs.begin();it!=jobs.end();){
   if(!(*it)->running&&std::find(wanted.begin(),wanted.end(),(*it)->path)==wanted.end())it=jobs.erase(it);else ++it;
  }
 }
 Stats stats()const{return totals;}
 std::optional<WarmAsset> take(const std::string&path){
  std::unique_lock<std::mutex> lock(mutex);
  auto it=std::find_if(jobs.begin(),jobs.end(),[&](const auto&j){return j->path==path;});
  if(it==jobs.end()){++totals.cold;return {};}
  auto job=*it;job->claimed=true;
  // Claim queued work immediately. A decoder already in flight is shared.
  if(!job->done&&!job->running){++totals.urgent;jobs.erase(it);lock.unlock();return job->load();}
  if(job->done)++totals.ready;
  else{++totals.waited;changed.wait(lock,[&]{return job->done;});}
  it=std::find(jobs.begin(),jobs.end(),job);if(it!=jobs.end())jobs.erase(it);
  if(job->error)return {}; // The normal resource loader retries at the actual script command.
  return std::move(job->asset);
 }
};
}
