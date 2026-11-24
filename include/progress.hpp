// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"
#include <array>
namespace whj2 {
class Progress {
 std::array<bool,4> cleared{};
 std::set<std::string> labels,files,readPages;
public:
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
}
