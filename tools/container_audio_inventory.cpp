// SPDX-License-Identifier: GPL-2.0-or-later
#include "image.hpp"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <stdexcept>
struct Inventory {
 unsigned containers=0,sounds=0,videos=0,errors=0;
 void scan(const qlie::Bytes&data,const std::string&path,unsigned depth=0){
  if(depth>32)throw std::runtime_error("Container nesting limit: "+path);
  if(data.size()<5||std::memcmp(data.data(),"abmp1",5))return;
  containers++;auto members=whj2::abmp(data);
  for(size_t i=0;i<members.size();i++){auto&m=members[i];auto member=path+"#"+std::to_string(i);
   if(m.type.rfind("absnd",0)==0){sounds++;std::cout<<"SOUND "<<std::quoted(member)<<" "<<std::quoted(m.name)<<" bytes="<<m.data.size()<<" signature=";for(size_t j=0;j<std::min(size_t(8),m.data.size());j++)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(m.data[j]);std::cout<<std::dec<<"\n";}
   if(m.type.rfind("abimg",0)==0&&m.data.size()>4&&!std::memcmp(m.data.data(),"OggS",4)){videos++;std::cout<<"VIDEO "<<std::quoted(member)<<" "<<std::quoted(m.name)<<" bytes="<<m.data.size()<<"\n";}
   scan(m.data,member,depth+1);
  }
 }
};
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1]);Inventory inventory;unsigned files=0;
 for(const auto&name:assets.names())if(name.size()>2&&name.substr(name.size()-2)==".b"){
  files++;try{inventory.scan(assets.read(name),name);}catch(const std::exception&e){inventory.errors++;std::cerr<<"ERROR "<<std::quoted(name)<<" "<<e.what()<<"\n";}
 }
 std::cout<<"TOTAL files="<<files<<" containers="<<inventory.containers<<" sounds="<<inventory.sounds<<" videos="<<inventory.videos<<" errors="<<inventory.errors<<"\n";return inventory.errors?1:0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
