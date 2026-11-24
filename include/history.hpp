// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "state_io.hpp"
#include <utility>

namespace whj2 {
struct HistoryEntry {
 std::string speaker,text,voice;
};

inline qlie::Bytes saveHistory(const std::vector<HistoryEntry>&entries){
 StateWriter writer;
 writer.str("WHJ2-history-2");
 writer.n(entries.size());
 for(const auto&entry:entries){
  writer.str(entry.speaker);
  writer.str(entry.text);
  writer.str(entry.voice);
 }
 return writer.bytes;
}

inline std::vector<HistoryEntry> restoreHistory(const qlie::Bytes&bytes){
 StateReader reader{bytes};
 const auto version=reader.str();
 const bool hasVoice=version=="WHJ2-history-2";
 if(!hasVoice&&version!="WHJ2-history-1")throw std::runtime_error("History version");
 const auto count=reader.n();
 std::vector<HistoryEntry> entries;
 for(uint64_t i=0;i<count;i++){
  HistoryEntry entry;
  entry.speaker=reader.str();
  entry.text=reader.str();
  if(hasVoice)entry.voice=reader.str();
  entries.push_back(std::move(entry));
 }
 reader.end();
 return entries;
}
}
