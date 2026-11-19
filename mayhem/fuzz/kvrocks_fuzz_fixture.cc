#include "kvrocks_fuzz_fixture.h"
#include "kvrocks_fuzz_scratch.h"

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>

#include "config/config.h"
#include "server/redis_connection.h"
#include "server/server.h"
#include "server/worker.h"
#include "storage/storage.h"

namespace {
Config *g_config = nullptr;
engine::Storage *g_storage = nullptr;
Server *g_server = nullptr;
Worker *g_worker = nullptr;
event_base *g_base = nullptr;
std::string g_dir;

std::string Drain(evbuffer *buf) {
  std::string out;
  size_t n = evbuffer_get_length(buf);
  if (n) {
    out.resize(n);
    evbuffer_remove(buf, out.data(), n);
  }
  return out;
}

template <typename F>
std::string WithConnection(bool admin, F &&fn) {
  bufferevent *pair[2] = {nullptr, nullptr};
  if (bufferevent_pair_new(g_base, 0, pair) != 0) abort();
  std::string out;
  {
    auto conn = std::make_unique<redis::Connection>(pair[0], g_worker);
    conn->NeedNotFreeBufferEvent();
    // ExecuteCommands makes a connection with no namespace an admin of the default namespace when
    // requirepass is empty; a non-admin client already sits in the default namespace.
    if (!admin) conn->SetNamespace(kDefaultNamespace);
    fn(conn.get());
    out = Drain(conn->Output());
    // What Worker::FreeConnection does before deleting a client.
    g_server->ResetWatchedKeys(conn.get());
    g_server->CleanupWaitConnection(conn.get());
    if (conn->IsPaused()) g_server->RemovePausedConn(conn.get());
  }
  out += Drain(bufferevent_get_input(pair[1]));
  bufferevent_free(pair[0]);
  bufferevent_free(pair[1]);
  return out;
}
}  // namespace

// Upstream code reaches the running server through this global (src/cli/main.cc defines it for the
// real binary, tests/cppunit/main.cc for the unit tests).
Server *GetServer() { return g_server; }

namespace kvfuzz {

void Init(const std::string &extra_conf) {
  if (g_server) return;
  spdlog::set_level(spdlog::level::off);

  RaiseSoftLimits();
  g_dir = MakeScratchDir("kvrocks-fuzz");

  std::string conf_path = g_dir + "/kvrocks.conf";
  {
    std::ofstream conf(conf_path);
    conf << "dir " << g_dir << "\n"
         << "log-level fatal\n"
         << "workers 1\n"
         // Few descriptors: Server::AdjustOpenFilesLimit exits unless the soft limit covers
         // maxclients + rocksdb.max_open_files + 128.
         << "maxclients 16\n"
         << "rocksdb.max_open_files 64\n"
         << "rocksdb.compression no\n"
         << "rocksdb.write_buffer_size 4\n"
         << extra_conf << "\n";
  }
  g_config = new Config();
  auto s = g_config->Load(CLIOptions(conf_path));
  if (!s.IsOK()) {
    fprintf(stderr, "kvfuzz: config load failed: %s\n", s.Msg().c_str());
    abort();
  }
  g_storage = new engine::Storage(g_config);
  s = g_storage->Open();
  if (!s.IsOK()) {
    fprintf(stderr, "kvfuzz: storage open failed: %s\n", s.Msg().c_str());
    abort();
  }
  g_server = new Server(g_storage, g_config);
  // As in upstream's tests: no listener and no cron/background threads.
  g_server->Stop();
  g_server->Join();
  g_worker = new Worker(g_server, g_config);
  g_base = event_base_new();
  if (!g_base) abort();
}

Server *Srv() { return g_server; }
const std::string &ScratchDir() { return g_dir; }

std::string Exec(std::deque<CommandTokens> cmds, bool admin) {
  return WithConnection(admin, [&](redis::Connection *conn) { conn->ExecuteCommands(&cmds); });
}

std::string Exec1(const Cmd &cmd, bool admin) {
  std::deque<CommandTokens> q;
  q.push_back(cmd);
  return Exec(std::move(q), admin);
}

std::string ExecResp(const uint8_t *data, size_t size, bool (*allow)(const CommandTokens &)) {
  return WithConnection(true, [&](redis::Connection *conn) {
    evbuffer *in = evbuffer_new();
    evbuffer_add(in, data, size);
    redis::Request req(g_server);
    // Tokenize stops at the first protocol error; whatever it parsed before that still runs, as
    // it would on a live connection that is about to be closed.
    (void)req.Tokenize(in);
    std::deque<CommandTokens> allowed;
    for (auto &cmd : *req.GetCommands()) {
      if (!cmd.empty() && allow(cmd)) allowed.push_back(std::move(cmd));
    }
    evbuffer_free(in);
    conn->ExecuteCommands(&allowed);
  });
}

void Reset() {
  g_server->UnpauseConns();
  std::deque<CommandTokens> q;
  q.push_back({"flushall"});
  q.push_back({"script", "flush"});
  q.push_back({"function", "flush"});
  Exec(std::move(q), true);
}

std::vector<std::string> Split(const uint8_t *data, size_t size, size_t max_parts) {
  std::vector<std::string> parts;
  std::string cur;
  for (size_t i = 0; i < size; i++) {
    if (data[i] == 0 && parts.size() + 1 < max_parts) {
      parts.push_back(std::move(cur));
      cur.clear();
    } else {
      cur.push_back(static_cast<char>(data[i]));
    }
  }
  parts.push_back(std::move(cur));
  return parts;
}

}  // namespace kvfuzz
