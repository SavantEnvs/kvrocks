// FT.SEARCH / FT.EXPLAIN: the RediSearch query language (src/search/redis_query_parser.h,
// redis_query_transformer.h), the IR passes and plan execution against the pre-created index `idx`
// (kvrocks_fuzz_search.h). Input = <query> NUL <rest>; <rest> is split on NUL into extra FT.SEARCH
// arguments (LIMIT, SORTBY, RETURN, PARAMS, DIALECT, ...).
#include <cstdint>
#include <deque>
#include <string>

#include "kvrocks_fuzz_search.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  kvfuzz::InitSearch();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  auto parts = kvfuzz::Split(data, size, 16);
  CommandTokens search = {"ft.search", "idx", parts[0]};
  CommandTokens explain = {"ft.explain", "idx", parts[0]};
  for (size_t i = 1; i < parts.size(); i++) {
    search.push_back(parts[i]);
    explain.push_back(parts[i]);
  }
  std::deque<CommandTokens> q = {explain, search};
  kvfuzz::Exec(std::move(q));
  return 0;
}
