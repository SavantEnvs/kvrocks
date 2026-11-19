// Cluster topology: CLUSTERX SETNODES <nodes> <version> FORCE (src/cluster/cluster.cc
// Cluster::SetClusterNodes / parseClusterNodes: node lines, roles, slot ranges), then the commands
// that render it (CLUSTER NODES / SLOTS / SHARDS / INFO, CLUSTERX VERSION) and a keyed command routed
// through the new slot table. The input is the nodes string, as a cluster controller sends it.
// The server runs with cluster-enabled yes and no `bind`, so no parsed node can be "myself" and no
// replication link is started. Every input replaces the whole topology (FORCE).
#include <cstdint>
#include <deque>
#include <string>

#include "kvrocks_fuzz_fixture.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  kvfuzz::Init("cluster-enabled yes");
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string nodes(reinterpret_cast<const char *>(data), size);
  std::deque<CommandTokens> q = {
      {"clusterx", "setnodes", nodes, "1", "force"},
      {"cluster", "nodes"},
      {"cluster", "slots"},
      {"cluster", "shards"},
      {"cluster", "info"},
      {"clusterx", "version"},
      {"get", "somekey"},
  };
  kvfuzz::Exec(std::move(q));
  return 0;
}
