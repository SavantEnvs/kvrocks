// RDB file import: `RDB LOAD <path>` (src/commands/cmd_server.cc CommandRdb) -> RdbFileStream ->
// RDB::LoadRdb (src/storage/rdb/rdb.cc): header/version, aux/selectdb/expire/resize opcodes and every
// object loader (ziplist, listpack, intset, zipmap, quicklist, stream, LZF strings).
// The input is the whole .rdb file, written to a scratch file in the per-process temp dir.
// LoadRdb verifies the file's CRC64 only after it has decoded every object, so no fix-up is needed
// to reach the loaders.
#include <cstdint>
#include <cstdio>
#include <string>

#include "kvrocks_fuzz_fixture.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  kvfuzz::Init();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const std::string path = kvfuzz::ScratchDir() + "/input.rdb";
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return 0;
  if (size) fwrite(data, 1, size, f);
  fclose(f);
  kvfuzz::Exec1({"rdb", "load", path});
  // Read back what was loaded, through the normal commands.
  kvfuzz::Exec1({"scan", "0", "count", "1000"});
  kvfuzz::Reset();
  remove(path.c_str());
  return 0;
}
