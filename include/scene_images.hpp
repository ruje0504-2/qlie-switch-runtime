#pragma once
#include "image.hpp"
#include "particles.hpp"
#include <map>
#include <memory>
namespace whj2 {
struct Composite;
struct SceneSoundEvent {bool stop=false,loop=false;unsigned channel=0;float gain=1;std::string source;qlie::Bytes bytes;};
class SceneImages {
 struct Layer {std::map<std::string,std::string> params;std::string prefix,path;std::shared_ptr<Image> pixels;std::shared_ptr<Composite> composite;double milliseconds=0,audioMilliseconds=-1;};
 uint32_t particleState=particleIdentityInitialState();
 qlie::Assets&assets;std::map<std::string,Layer>layers;std::map<std::string,std::map<std::string,std::string>>templates;std::map<std::string,std::shared_ptr<Image>>cache;
 std::map<std::string,std::shared_ptr<Composite>>composites;
 std::string expand(std::string,const std::string&);
public:
 explicit SceneImages(qlie::Assets&);
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
 std::vector<SceneSoundEvent> takeSoundEvents();
 void update(double milliseconds);bool command(const std::string&);void clear();void draw(qlie::Bytes&)const;
};
}
