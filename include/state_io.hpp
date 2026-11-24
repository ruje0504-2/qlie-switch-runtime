#pragma once
#include "qlie.hpp"
#include <cstring>
#include <stdexcept>
namespace whj2 {
struct StateWriter {qlie::Bytes bytes;void n(uint64_t v){for(int i=0;i<8;i++)bytes.push_back(uint8_t(v>>(8*i)));}void str(const std::string&s){n(s.size());bytes.insert(bytes.end(),s.begin(),s.end());}void f(float v){uint32_t u;std::memcpy(&u,&v,4);n(u);}};
struct StateReader {const qlie::Bytes&bytes;size_t p=0;uint64_t n(){if(bytes.size()-p<8)throw std::runtime_error("Truncated presentation state");uint64_t v=0;for(int i=0;i<8;i++)v|=uint64_t(bytes[p++])<<(8*i);return v;}std::string str(){auto size=n();if(size>bytes.size()-p)throw std::runtime_error("Truncated presentation string");std::string s(bytes.begin()+p,bytes.begin()+p+size);p+=size;return s;}float f(){uint32_t u=uint32_t(n());float v;std::memcpy(&v,&u,4);return v;}void end(){if(p!=bytes.size())throw std::runtime_error("Trailing presentation state");}};
}
