#include "statewright/common/error.hpp"
#include "statewright/common/utf8.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("UTF-8 validation accepts valid Unicode") {
  REQUIRE(statewright::common::is_valid_utf8("plain ASCII"));
  REQUIRE(statewright::common::is_valid_utf8("München — 日本語"));
}

TEST_CASE("UTF-8 validation rejects malformed sequences") {
  const std::string overlong{"\xC0\xAF", 2};
  const std::string surrogate{"\xED\xA0\x80", 3};
  const std::string too_high{"\xF4\x90\x80\x80", 4};
  REQUIRE_FALSE(statewright::common::is_valid_utf8(overlong));
  REQUIRE_FALSE(statewright::common::is_valid_utf8(surrogate));
  REQUIRE_FALSE(statewright::common::is_valid_utf8(too_high));
  REQUIRE_THROWS_AS(statewright::common::require_valid_utf8(overlong),
                    statewright::common::Error);
}

