#include "animation.hpp"
#include "image.hpp"
#include <iostream>
size_t count=0,motions=0,layers=0,keys=0;
void check(const qlie::Bytes&b,const std::string&name){if(b.size()<5||!std::equal(b.begin(),b.begin()+5,"abmp1"))return;for(auto&m:whj2::abmp(b)){if(m.type.rfind("abdata",0)==0){try{auto a=whj2::animation(m.type,m.data);count++;motions+=a.motions.size();for(auto&mo:a.motions){layers+=mo.layers.size();for(auto&l:mo.layers)keys+=l.keys.size();}}catch(const std::exception&e){throw std::runtime_error(name+": "+e.what());}}else if(m.type.rfind("abimg",0)==0)check(m.data,name+"#"+m.name);}}
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets a(argv[1]);for(auto&n:a.names())if(n.size()>2&&n.substr(n.size()-2)==".b")check(a.read(n),n);std::cout<<"PASS metadata="<<count<<" motions="<<motions<<" layers="<<layers<<" keys="<<keys<<"\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
