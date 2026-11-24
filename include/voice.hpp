#pragma once
#include "qlie.hpp"
namespace whj2 {
struct VoiceMatch {std::string request;qlie::ResourceLocation source;};
class VoiceResolver {
 qlie::Assets&assets;std::string prefix="voice/";
public:
 float gain=1;bool fileErrors=false;
 explicit VoiceResolver(qlie::Assets&);
 std::vector<std::string> candidates(std::string id)const;
 std::optional<VoiceMatch> resolve(const std::string&id)const;
};
}
