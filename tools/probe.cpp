#include "profiles/whj2.hpp"
#include "qlie.hpp"
#include <iostream>
#include <filesystem>
int main(int argc,char**argv){try{if(argc<2)throw std::runtime_error("usage: probe GAME [ENTRY OUTPUT]");qlie::Assets a(argv[1],whj2::profile::archives);if(argc==4&&std::string(argv[2])!="--dump"){auto b=a.read(argv[2]);qlie::write_file(argv[3],b);std::cout<<b.size()<<" bytes\n";return 0;}size_t n=0,bytes=0;for(const auto&s:a.names()){auto b=a.read(s);bytes+=b.size();n++;if(argc==4&&std::string(argv[2])=="--dump"){auto dest=std::filesystem::path(argv[3])/s;std::filesystem::create_directories(dest.parent_path());qlie::write_file(dest.string(),b);}if(n%100==0)std::cout<<n<<" verified\n";}std::cout<<"PASS "<<n<<" effective resources, "<<bytes<<" decoded bytes\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
