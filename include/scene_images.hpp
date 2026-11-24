#pragma once
#include "image.hpp"
#include <map>
#include <memory>
namespace whj2 {
class SceneImages {
 struct Layer {std::map<std::string,std::string> params;std::string prefix,path;std::shared_ptr<Image> pixels;};
 qlie::Assets&assets;std::map<std::string,Layer>layers;std::map<std::string,std::map<std::string,std::string>>templates;std::map<std::string,std::shared_ptr<Image>>cache;
 std::string expand(std::string,const std::string&);
public:
 explicit SceneImages(qlie::Assets&);
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
 bool command(const std::string&);void clear();void draw(qlie::Bytes&)const;
};
}
