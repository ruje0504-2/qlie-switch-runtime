#pragma once
#include "qlie.hpp"
#include <array>
namespace whj2 {
struct Vec3 {float x,y,z;};
struct ModelFace {unsigned corners;int material;std::array<unsigned,4> vertices;std::array<float,8> uv;std::array<float,16> colors;std::array<float,7> auxiliary;};
struct ModelObject {std::string name;bool visible;float facet;std::vector<Vec3> vertices;std::vector<ModelFace> faces;};
struct ModelMaterial {std::string name,texture;std::array<uint32_t,13> properties;};
struct Model {qlie::Bytes scene;std::vector<ModelMaterial> materials;std::vector<ModelObject> objects;};
Model model(const qlie::Bytes&);
}
