// RedisJSON-compatible commands (src/commands/cmd_json.cc, src/types/redis_json.cc, src/types/json.h):
// the JSON document parser, JSONPath evaluation and the JSON.* mutations (NUMINCRBY/NUMMULTBY, #2345).
// Input = <document> NUL <jsonpath> NUL <value>. The document is stored with JSON.SET k $ <document>,
// then every command below runs on it with the fuzzed path and value. The key is deleted after.
#include <cstdint>
#include <deque>
#include <string>

#include "kvrocks_fuzz_fixture.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  kvfuzz::Init();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  auto parts = kvfuzz::Split(data, size, 3);
  parts.resize(3);
  const std::string &doc = parts[0], &path = parts[1], &val = parts[2];
  std::deque<CommandTokens> q = {
      {"json.set", "k", "$", doc},
      {"json.get", "k", path},
      {"json.type", "k", path},
      {"json.numincrby", "k", path, val},
      {"json.nummultby", "k", path, val},
      {"json.strappend", "k", path, val},
      {"json.arrappend", "k", path, val},
      {"json.arrinsert", "k", path, "0", val},
      {"json.arrindex", "k", path, val},
      {"json.arrtrim", "k", path, "0", "1"},
      {"json.arrpop", "k", path},
      {"json.merge", "k", path, val},
      {"json.set", "k", path, val},
      {"json.objkeys", "k", path},
      {"json.objlen", "k", path},
      {"json.strlen", "k", path},
      {"json.arrlen", "k", path},
      {"json.toggle", "k", path},
      {"json.resp", "k", path},
      {"json.debug", "memory", "k", path},
      {"json.mget", "k", path},
      {"json.clear", "k", path},
      {"json.del", "k", path},
      {"json.get", "k"},
  };
  kvfuzz::Exec(std::move(q));
  kvfuzz::Exec1({"del", "k"});
  return 0;
}
