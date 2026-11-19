// Shared set-up for the FT.* targets: one search index over hashes, created once per process with
// a few documents, the way a client would (FT.CREATE + HSET). The query targets only read it.
#pragma once
#include "kvrocks_fuzz_fixture.h"

namespace kvfuzz {
inline void InitSearch() {
  Init();
  // Two float32 components: (1,0), (0,1), (1,1).
  const std::string v1("\x00\x00\x80\x3f\x00\x00\x00\x00", 8), v2("\x00\x00\x00\x00\x00\x00\x80\x3f", 8),
      v3("\x00\x00\x80\x3f\x00\x00\x80\x3f", 8);
  std::deque<CommandTokens> q = {
      {"ft.create", "idx", "on", "hash", "prefix", "1", "doc:", "schema", "t", "tag", "n", "numeric", "sortable",
       "v", "vector", "hnsw", "6", "type", "float32", "dim", "2", "distance_metric", "l2"},
      {"hset", "doc:1", "t", "a,b", "n", "1", "v", v1},
      {"hset", "doc:2", "t", "b", "n", "2.5", "v", v2},
      {"hset", "doc:3", "t", "c,a", "n", "-3", "v", v3},
  };
  Exec(std::move(q));
}
}  // namespace kvfuzz
