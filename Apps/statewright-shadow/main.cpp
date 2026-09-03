#include "statewright/common/error.hpp"
#include "statewright/contracts/build_identity.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <iostream>
#include <string>

namespace {

statewright::contracts::Json execute(
    const statewright::contracts::Json &request) {
  using namespace statewright::contracts;
  const std::string operation = request.at("operation").get<std::string>();
  Json output;
  if (operation == "canonical_json") {
    output = canonical_json(request.at("payload"));
  } else if (operation == "sha256_json") {
    output = sha256_json(request.at("payload"));
  } else if (operation == "typed_id") {
    output = typed_id(request.at("object_type").get<std::string>(),
                      request.at("payload"));
  } else if (operation == "build_identity") {
    output = build_identity();
  } else {
    throw statewright::common::Error(
        statewright::common::ErrorCode::invalid_argument,
        "unsupported shadow operation: " + operation);
  }

  const Json response_material = {
      {"operation", operation}, {"output", output}, {"protocol", 1}};
  return {
      {"build", build_identity()},
      {"ok", true},
      {"operation", operation},
      {"output", output},
      {"output_hash", sha256_json(response_material)},
      {"protocol", "statewright.shadow.v1"},
  };
}

} // namespace

int main() {
  using namespace statewright;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;
    }
    try {
      std::cout << contracts::canonical_json(execute(contracts::parse_json(line)))
                << '\n';
    } catch (const common::Error &error) {
      const contracts::Json response = {
          {"error",
           {{"code", common::error_code_name(error.code())},
            {"message", error.what()}}},
          {"ok", false},
          {"protocol", "statewright.shadow.v1"},
      };
      std::cout << contracts::canonical_json(response) << '\n';
    } catch (const std::exception &error) {
      const contracts::Json response = {
          {"error",
           {{"code", "internal_failure"}, {"message", error.what()}}},
          {"ok", false},
          {"protocol", "statewright.shadow.v1"},
      };
      std::cout << contracts::canonical_json(response) << '\n';
    }
  }
  return 0;
}

