#pragma once

#include "statewright/sources/fetch.hpp"

namespace statewright::sources {

class HttpFetchProvider {
public:
  virtual ~HttpFetchProvider() = default;
  [[nodiscard]] virtual FetchResponse fetch(const FetchRequest &request) = 0;
};

class CurlHttpFetchProvider final : public HttpFetchProvider {
public:
  CurlHttpFetchProvider();
  [[nodiscard]] FetchResponse fetch(const FetchRequest &request) override;
};

} // namespace statewright::sources
