// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"
#include <array>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
namespace whj2 {
class Progress {
 const char* extrasFlag=nullptr;
 std::array<bool,4> cleared{};
 std::set<std::string> labels,files,readPages;
public:
 Progress()=default;
 explicit Progress(const char*flag):extrasFlag(flag){}
 bool capture(const Story&);
 bool markRead(const std::string&key){return !key.empty()&&readPages.insert(key).second;}
 bool wasRead(const std::string&key)const{return !key.empty()&&readPages.count(key)!=0;}
 bool captureFiles(const std::set<std::string>&);
 bool seenFile(const std::string&path)const{return files.count(qlie::canonical(path))!=0;}
 void apply(Story&)const;
 bool extras()const{return cleared[0]||cleared[1]||cleared[2];}
 bool clearedRoute(unsigned route)const{return route<cleared.size()&&cleared[route];}
 bool visited(const std::string&key)const{return labels.count(key)!=0;}
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
class ProgressWriter {
 const std::string path;
 std::mutex mutex;
 std::condition_variable changed;
 std::optional<qlie::Bytes> pending;
 std::exception_ptr failure;
 bool writing=false,stopping=false;
 std::thread worker;
 void run();
public:
 explicit ProgressWriter(std::string);
 ~ProgressWriter();
 void schedule(qlie::Bytes);
 void flush();
};
}
