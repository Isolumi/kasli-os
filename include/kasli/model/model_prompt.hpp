#pragma once

#include <kasli/model/model_provider.hpp>

#include <string>

namespace kasli::model {

std::string build_evidence_prompt(const ModelRequest& request);

}  // namespace kasli::model
