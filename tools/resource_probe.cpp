#include "qlie.hpp"
#include "voice.hpp"
#include <iostream>
#include <iomanip>
int main(int argc,char**argv){try{if(argc<3)return 2;qlie::Assets a(argv[1]);std::string name=argv[2];if(name=="--voice"){if(argc!=4)return 2;whj2::VoiceResolver v(a);for(auto&n:v.candidates(argv[3]))std::cout<<"candidate "<<std::quoted(n)<<"\n";auto found=v.resolve(argv[3]);if(!found){std::cout<<"voice unresolved\n";return 0;}name=found->request;}auto loc=a.locate(name);if(!loc){std::cout<<"missing "<<std::quoted(name)<<"\n";return 0;}auto bytes=a.read(name);std::cout<<"source "<<std::quoted(loc->path)<<" loose="<<loc->loose<<" offset="<<loc->offset<<" stored="<<loc->size<<" decoded="<<bytes.size()<<" qlie_hash="<<qlie::hash(bytes)<<"\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
