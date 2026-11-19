// kvrocks.conf parser: Config::Load (src/config/config.cc, config_type.h, config_util.cc): line
// tokenizing, every field type (int/enum/yes-no/octal/size/cron/rename-command/namespace tokens)
// and the cross-field checks. The input is the config file. A fresh Config per input, no server.
// Config::Load creates the configured `dir` if it is missing; a relative one lands in the working
// directory, and an existing directory does not change the result of a later load.
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "config/config.h"
#include "kvrocks_fuzz_scratch.h"
#include "server/server.h"

static std::string g_path;

Server *GetServer() { return nullptr; }

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  kvfuzz::RaiseSoftLimits();
  g_path = kvfuzz::MakeScratchDir("kvrocks-fuzz-conf") + "/kvrocks.conf";
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FILE *f = fopen(g_path.c_str(), "wb");
  if (!f) return 0;
  if (size) fwrite(data, 1, size, f);
  fclose(f);
  Config config;
  (void)config.Load(CLIOptions(g_path));
  return 0;
}
