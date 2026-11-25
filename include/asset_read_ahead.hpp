// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "diagnostics.h"
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
struct PreparedEmbeddedOpening;
struct PreparedMovie;
struct PreparedAudioRestore;
struct PreparedSceneRestore;
struct DecodedPcm {
 std::shared_ptr<const int16_t> samples;
 size_t count=0;
 bool empty()const{return count==0;}
 size_t size()const{return count;}
 int16_t operator[](size_t i)const{return samples.get()[i];}
};
struct WarmAsset {
 std::shared_ptr<const Image> bitmap;
 std::map<std::string,std::shared_ptr<const Image>> uiBitmaps;
 std::shared_ptr<Composite> composite;
 std::shared_ptr<PreparedEmbeddedOpening> embeddedOpening;
 uint32_t identities=0;
 DecodedPcm pcm;
 std::shared_ptr<PreparedMovie> movie;
 std::shared_ptr<PreparedAudioRestore> audioRestore;
 std::shared_ptr<PreparedSceneRestore> sceneRestore;
 size_t bytes=0;
};

// Keys are canonical game paths. Each ready result is consumed just once.
class AssetReadAhead {
public:
 enum class Priority { Normal, Background };
 struct Stats {unsigned ready=0,waited=0,urgent=0,cold=0;};
private:
 Stats totals; // Only the foreground consumer samples or changes these counters.
 using Loader=std::function<WarmAsset()>;
 struct Job {std::string path;Loader load;WarmAsset asset;bool running=false,done=false,claimed=false;std::exception_ptr error;};
 std::deque<std::shared_ptr<Job>> jobs;
 std::deque<std::shared_ptr<void>> retired;
 std::mutex mutex;std::condition_variable changed;bool stopping=false;
 const size_t byteBudget;
 std::thread worker;

 // Only completed jobs can own large media buffers or open decoders.
 // Let the worker release them without holding the queue lock.
 auto eraseJob(decltype(jobs)::iterator it){
  if((*it)->done)retired.push_back(std::move(*it));
  return jobs.erase(it);
 }
 void run(unsigned core,const char*role,Priority priority){
#ifdef __SWITCH__
  logSwitchThread(role,bindSwitchThread(core));
  if(priority==Priority::Background){
   constexpr unsigned backgroundPriority=59;
   const Result result=svcSetThreadPriority(CUR_THREAD_HANDLE,backgroundPriority);
   QLIE_LOGF("CPU role=%s requested_priority=%u priority_set_rc=%x\n",role,backgroundPriority,unsigned(result));
  }
#else
  (void)core;(void)role;(void)priority;
#endif
  std::unique_lock<std::mutex> lock(mutex);
  for(;;){
   std::shared_ptr<Job> job;
   changed.wait(lock,[&]{
    if(stopping)return true;
    for(auto&j:jobs)if(!j->done&&!j->running){
     // Required jobs are moved ahead of hints by request(). A ready
     // page should not wait for dead resources to be destroyed.
     if(j->claimed||retired.empty())job=j;
     break;
    }
    return bool(job)||!retired.empty();
   });
   if(stopping)return;
   if(!job){
    auto removed=std::move(retired.front());
    retired.pop_front();
    lock.unlock();
    removed.reset();
    lock.lock();
    continue;
   }
   job->running=true;lock.unlock();
   try{job->asset=job->load();}catch(...){job->error=std::current_exception();}
   lock.lock();job->load={};job->running=false;job->done=true;
   size_t bytes=0;
   for(auto&j:jobs)if(j->done)bytes+=j->asset.bytes;
   // Later hints are further from the current script position.
   for(size_t i=jobs.size();i&&bytes>byteBudget;){
    auto it=jobs.begin()+--i;
    if((*it)->done&&!(*it)->claimed){bytes-=(*it)->asset.bytes;eraseJob(it);}
   }
   changed.notify_all();
   lock.unlock();job.reset();lock.lock();
  }
 }
public:
 explicit AssetReadAhead(unsigned core=1,const char*role="asset-read-ahead",size_t budget=48u*1024*1024,
                         Priority priority=Priority::Normal)
  :byteBudget(budget),worker([this,core,role,priority]{run(core,role,priority);}){}
 ~AssetReadAhead(){
  {std::lock_guard<std::mutex> lock(mutex);stopping=true;}
  changed.notify_all();worker.join();
 }
 void request(const std::string&path,Loader load,bool required=false){
  std::lock_guard<std::mutex> lock(mutex);
  auto found=std::find_if(jobs.begin(),jobs.end(),[&](const auto&job){return job->path==path;});
  if(found!=jobs.end()){
   if(required){(*found)->claimed=true;std::rotate(jobs.begin(),found,std::next(found));}
   return;
  }
  if(!required&&jobs.size()>=6)return;
  auto job=std::make_shared<Job>();job->path=path;job->load=std::move(load);job->claimed=required;
  if(required)jobs.push_front(std::move(job));else jobs.push_back(std::move(job));
  changed.notify_one();
 }
 void retain(const std::vector<std::string>&wanted){
  std::lock_guard<std::mutex> lock(mutex);
  // The worker still owns obsolete running jobs, but their results no
  // longer belong to this cache or count against its current budget.
  for(auto it=jobs.begin();it!=jobs.end();){
   if(!(*it)->claimed&&std::find(wanted.begin(),wanted.end(),(*it)->path)==wanted.end())it=eraseJob(it);else ++it;
  }
  if(!retired.empty())changed.notify_all();
 }
 void retire(std::shared_ptr<void> resource){
  std::lock_guard<std::mutex> lock(mutex);
  retired.push_back(std::move(resource));
  changed.notify_one();
 }
 Stats stats()const{return totals;}
 bool ready(const std::string&path){
  std::lock_guard<std::mutex> lock(mutex);
  return std::any_of(jobs.begin(),jobs.end(),[&](const auto&job){return job->path==path&&job->done;});
 }
 void release(const std::string&path){
  std::lock_guard<std::mutex> lock(mutex);
  auto it=std::find_if(jobs.begin(),jobs.end(),[&](const auto&job){return job->path==path;});
  if(it==jobs.end())return;
  if(!(*it)->running&&!(*it)->done)jobs.erase(it);
  else (*it)->claimed=false;
 }
 void discard(const std::string&path){
  std::lock_guard<std::mutex> lock(mutex);
  auto it=std::find_if(jobs.begin(),jobs.end(),[&](const auto&job){return job->path==path;});
  if(it==jobs.end())return;
  eraseJob(it);
  if(!retired.empty())changed.notify_all();
 }
 std::optional<WarmAsset> takeReady(const std::string&path){
  std::unique_lock<std::mutex> lock(mutex);
  auto it=std::find_if(jobs.begin(),jobs.end(),[&](const auto&job){return job->path==path;});
  if(it==jobs.end()||!(*it)->done)return {};
  auto job=*it;jobs.erase(it);lock.unlock();
  if constexpr (qlie::diagnostics) ++totals.ready;
  if(job->error)std::rethrow_exception(job->error);
  return std::move(job->asset);
 }
 std::optional<WarmAsset> take(const std::string&path){
  std::unique_lock<std::mutex> lock(mutex);
  auto it=std::find_if(jobs.begin(),jobs.end(),[&](const auto&j){return j->path==path;});
  if(it==jobs.end()){if constexpr (qlie::diagnostics) ++totals.cold;return {};}
  auto job=*it;const bool required=job->claimed;job->claimed=true;
  // Claim queued work immediately. A decoder already in flight is shared.
  if(!required&&!job->done&&!job->running){if constexpr (qlie::diagnostics) ++totals.urgent;jobs.erase(it);lock.unlock();return job->load();}
  if(job->done){if constexpr (qlie::diagnostics) ++totals.ready;}
  else{if constexpr (qlie::diagnostics) ++totals.waited;changed.wait(lock,[&]{return job->done;});}
  it=std::find(jobs.begin(),jobs.end(),job);if(it!=jobs.end())jobs.erase(it);
  if(job->error){
   if(required)std::rethrow_exception(job->error);
   return {}; // The normal resource loader retries speculative I/O at the script command.
  }
  return std::move(job->asset);
 }
};
}
