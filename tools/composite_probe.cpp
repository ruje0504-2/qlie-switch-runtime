#include "image.hpp"
#include <filesystem>
#include <iostream>
void dump(const qlie::Bytes&b,const std::string&out,unsigned&n){if(b.size()>5&&std::equal(b.begin(),b.begin()+5,"abmp1")){for(auto&m:whj2::abmp(b))if(m.type.rfind("abimg",0)==0||m.type.rfind("absnd",0)==0)dump(m.data,out,n);}else if(b.size()>4&&std::equal(b.begin(),b.begin()+4,"OggS")){auto path=out+"/stream-"+std::to_string(n++)+".ogg";qlie::write_file(path,b);std::cout<<path<<" bytes="<<b.size()<<"\n";}}
int main(int argc,char**argv){try{if(argc!=4)return 2;qlie::Assets a(argv[1]);std::filesystem::create_directories(argv[3]);unsigned n=0;dump(a.read(argv[2]),argv[3],n);return n?0:3;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
