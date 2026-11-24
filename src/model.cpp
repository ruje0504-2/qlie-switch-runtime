// SPDX-License-Identifier: GPL-2.0-or-later
// Layout verified against WHJ2 PE readers: file offsets df250,df42c,df684,df950.
#include "model.hpp"
#include <stdexcept>
#include <cstring>
#include <cmath>
namespace whj2 {
struct ModelReader {
 const qlie::Bytes&b;size_t p=0;
 uint32_t n(unsigned count=4){if(p+count>b.size())throw std::runtime_error("Truncated binary model");uint32_t v=0;for(unsigned i=0;i<count;i++)v|=uint32_t(b[p++])<<(8*i);return v;}
 float f(){auto bits=n();float v;std::memcpy(&v,&bits,4);if(!std::isfinite(v))throw std::runtime_error("Nonfinite model value");return v;}
 qlie::Bytes data(size_t size){if(size>b.size()-p)throw std::runtime_error("Truncated model bytes");qlie::Bytes d(b.begin()+p,b.begin()+p+size);p+=size;return d;}
 std::string tag(){auto d=data(16);size_t len=0;while(len<16&&d[len])len++;return std::string(d.begin(),d.begin()+len);}
 std::string str(){return qlie::cp932(data(n(2)));}
 unsigned count(){unsigned v=n();if(v>1000000)throw std::runtime_error("Invalid model count");return v;}
 void require(const char*t){if(tag()!=t)throw std::runtime_error(std::string("Unexpected model tag; expected ")+t);}
};
Model model(const qlie::Bytes&b){ModelReader r{b};r.require("ARGBModel1.0");r.require("TQMOmdl1.0");auto scene=r.tag();if(scene!="MQOscn1.1"&&scene!="MQOscn1.0")throw std::runtime_error("Unsupported model scene");Model m;m.scene=r.data(scene=="MQOscn1.1"?128:76);unsigned nm=r.count();while(nm--){r.require("MQOmat1.0");ModelMaterial a;a.name=r.str();a.texture=r.str();for(auto&v:a.properties)v=r.n();m.materials.push_back(std::move(a));}unsigned no=r.count();while(no--){r.require("MQOobj1.0");ModelObject o;o.name=r.str();o.visible=r.n(1)!=0;o.facet=r.f();unsigned nv=r.count(),nf=r.count();for(unsigned i=0;i<nv;i++){float x=r.f(),y=r.f(),z=r.f();o.vertices.push_back({x,y,z});}for(unsigned i=0;i<nf;i++){ModelFace face;face.corners=r.n();face.material=int32_t(r.n());if(face.corners<3||face.corners>4||face.material< -1||(face.material>=0&&unsigned(face.material)>=m.materials.size()))throw std::runtime_error("Invalid model face");for(auto&v:face.vertices)v=r.n();for(unsigned j=0;j<face.corners;j++)if(face.vertices[j]>=nv)throw std::runtime_error("Model vertex outside object");for(auto&v:face.uv)v=r.f();for(auto&v:face.colors)v=r.f();for(auto&v:face.auxiliary)v=r.f();o.faces.push_back(face);}m.objects.push_back(std::move(o));}if(r.p!=b.size())throw std::runtime_error("Unexpected trailing model data");return m;}
}
