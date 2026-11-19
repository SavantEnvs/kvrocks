// Lua scripting: EVAL and FUNCTION LOAD (src/storage/scripting.cc, src/commands/cmd_script.cc,
// src/commands/cmd_function.cc): the Lua sandbox, redis.call/pcall from Lua, Lua <-> RESP reply
// conversion and the cjson/cmsgpack/struct/bit libraries.
//
// Byte 0 picks the mode, the rest is the script source:
//   even: EVAL <script> <numkeys> k1 k2 a1  -- numkeys = (byte0 >> 1) % 4 - 1, so -1..2 (the -1
//         case is what #1568 crashed on)
//   odd:  FUNCTION LOAD <script>, then FCALL of the function name `f` (scripts register it with
//         redis.register_function('f', ...)); the library header (#!lua name=...) is part of the input.
// The script runs on a NON-admin client of the default namespace. redis.call() refuses no-script and
// exclusive commands (scripting.cc), so a script cannot shut down, reconfigure or replicate the server.
// The database, scripts and functions are flushed after every input.
#include <cstdint>
#include <string>

#include "kvrocks_fuzz_fixture.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  kvfuzz::Init();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1) return -1;
  const uint8_t mode = data[0];
  std::string script(reinterpret_cast<const char *>(data + 1), size - 1);
  if ((mode & 1) == 0) {
    int numkeys = (mode >> 1) % 4 - 1;
    kvfuzz::Exec1({"eval", script, std::to_string(numkeys), "k1", "k2", "a1"}, false);
  } else {
    // FUNCTION LOAD is an exclusive admin command; FCALL runs on a normal client.
    kvfuzz::Exec1({"function", "load", "replace", script}, true);
    kvfuzz::Exec1({"fcall", "f", "1", "k1", "a1"}, false);
  }
  kvfuzz::Reset();
  return 0;
}
