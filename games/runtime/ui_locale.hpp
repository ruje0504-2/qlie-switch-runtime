// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "qlie.hpp"
#include <map>
#include <sstream>
#include <stdexcept>

namespace whj2 {
// Text supplied by the optional language resource layer. Resource filenames,
// script tokens, font family names and save schemas are never translated.
class UiLocale {
 std::map<std::string,std::string> messages;
public:
 static constexpr const char* resource="localization/zh-cn/ui.tsv";
 explicit UiLocale(qlie::Assets& assets){
  if(!assets.exists(resource))return;
  const auto bytes=assets.read(resource);
  std::istringstream input(std::string(bytes.begin(),bytes.end()));
  std::string row;
  if(!std::getline(input,row)||row!="WHJ2-UI-LOCALE-1\tzh-CN")
   throw std::runtime_error("Unsupported UI translation catalog");
  while(std::getline(input,row)){
   if(!row.empty()&&row.back()=='\r')row.pop_back();
   if(row.empty()||row.front()=='#')continue;
   const auto tab=row.find('\t');
   if(tab==std::string::npos||!tab||tab+1==row.size())
    throw std::runtime_error("Invalid UI translation row");
   auto inserted=messages.emplace(row.substr(0,tab),row.substr(tab+1));
   if(!inserted.second)throw std::runtime_error("Duplicate UI translation key");
  }
 }
 bool simplified()const{return !messages.empty();}
 const char* text(const char* japanese)const{
  auto found=messages.find(japanese);
  return found==messages.end()?japanese:found->second.c_str();
 }
};
}
