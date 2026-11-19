// In-process kvrocks server for the fuzz harnesses (no sockets).
//
// One Server + Worker + RocksDB storage per process, in a fresh mkdtemp() directory under $TMPDIR,
// built the way upstream's own tests build it (tests/cppunit/test_base.h,
// tests/cppunit/server_client_pause_test.cc). Each command batch runs on a FRESH redis::Connection
// over a bufferevent pair, through the same entry point a TCP client reaches:
// redis::Request::Tokenize + redis::Connection::ExecuteCommands.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "server/redis_request.h"

using CommandTokens = redis::CommandTokens;

class Server;

namespace kvfuzz {

using Cmd = std::vector<std::string>;

// Opens the server once. `extra_conf` is appended to the generated kvrocks.conf.
void Init(const std::string &extra_conf = "");
Server *Srv();
const std::string &ScratchDir();

// Runs `cmds` on a fresh connection; returns everything the server replied.
// `admin`: true = the connection becomes admin (no requirepass, default namespace), as a local
// client does; false = a non-admin client of the default namespace.
std::string Exec(std::deque<CommandTokens> cmds, bool admin = true);
std::string Exec1(const Cmd &cmd, bool admin = true);

// Feeds raw RESP bytes to Request::Tokenize and executes the resulting commands whose name passes
// `allow` (an allow-list; see fuzz_resp.cc). Returns the replies.
std::string ExecResp(const uint8_t *data, size_t size, bool (*allow)(const CommandTokens &));

// Drops every key, Lua script and function, and unpauses clients, so the next input starts from
// the same empty database.
void Reset();

// Splits `data` on NUL bytes into at most `max_parts` parts (the last part keeps any further NULs).
std::vector<std::string> Split(const uint8_t *data, size_t size, size_t max_parts);

}  // namespace kvfuzz
