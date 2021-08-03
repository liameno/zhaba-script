#pragma once
#include <unordered_map>
#include <string>
#include <map>
#include "Types.hpp"

struct ScopeInfo {
  std::unordered_map<std::string, Type> vars;
};