// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "diagnostics.h"
#include "qlie.hpp"
#include "switch_storage.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace whj2 {
class SaveSlotWrite {
 struct Part {
  std::filesystem::path live,staged,backup;
  bool backedUp=false,published=false;
 };
 const std::string slot;
 const std::filesystem::path directory;
 std::vector<Part> parts;
 bool keepBackups=false,committed=false;

 void rollback(){
  std::error_code failure;
  for(auto part=parts.rbegin();part!=parts.rend();++part){
   std::error_code error;
   if(part->published)std::filesystem::remove(part->live,error);
   if(error&&!failure)failure=error;
   if(part->backedUp)std::filesystem::rename(part->backup,part->live,error);
   if(error&&!failure)failure=error;
  }
  if(failure)throw std::runtime_error(failure.message());
  commitSavePath(slot);
  committed=true;
 }
public:
 explicit SaveSlotWrite(std::string path)
  :slot(std::move(path)),
   directory(slot+".writing-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())){
  std::filesystem::create_directories(directory.parent_path());
  if(!std::filesystem::create_directory(directory))throw std::runtime_error("Slot staging directory already exists");
 }
 SaveSlotWrite(const SaveSlotWrite&)=delete;
 SaveSlotWrite&operator=(const SaveSlotWrite&)=delete;
 ~SaveSlotWrite(){
  if(keepBackups)return;
  std::error_code error;
  std::filesystem::remove_all(directory,error);
  if(error){
   QLIE_LOGF("SAVE staging cleanup failed: %s\n",error.message().c_str());
  }else if(committed){
   try{commitSavePath(slot);}
   catch(const std::exception&e){QLIE_LOGF("SAVE cleanup commit failed: %s\n",e.what());}
  }
 }
 void write(const std::string&suffix,const qlie::Bytes&bytes){
  Part part{slot+suffix,directory/("new"+suffix),directory/("old"+suffix)};
  qlie::write_uncommitted_file(part.staged.string(),bytes);
  parts.push_back(std::move(part));
 }
 void publish(){
  try{
   for(auto&part:parts){
    if(std::filesystem::exists(part.live)){
     if(!std::filesystem::is_regular_file(part.live))throw std::runtime_error("Slot component is not a file: "+part.live.string());
     std::filesystem::rename(part.live,part.backup);
     part.backedUp=true;
    }
    std::filesystem::rename(part.staged,part.live);
    part.published=true;
   }
   commitSavePath(slot);
   committed=true;
  }catch(...){
   try{rollback();}
   catch(const std::exception&e){
    keepBackups=true;
    throw std::runtime_error("Slot rollback failed; backups in "+directory.string()+": "+e.what());
   }
   throw;
  }
 }
};
}
