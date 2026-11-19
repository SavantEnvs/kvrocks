// kvrocks RESP command stream: the bytes a TCP client sends.
//
// Input -> redis::Request::Tokenize (RESP + inline protocol) -> redis::Connection::ExecuteCommands
// (command lookup, arity, every command's Parse + Execute against RocksDB). This is the path every
// network client takes (src/server/worker.cc, Connection::OnRead).
//
// Commands are ALLOW-LISTED by name below: the data-type and key-space commands a client uses,
// plus MULTI/EXEC, CLIENT KILL/LIST and RESTORE. Left out on purpose: commands that leave the
// process or the input (SHUTDOWN, DEBUG, SLAVEOF/REPLICAOF, PSYNC, BGSAVE, CONFIG, NAMESPACE,
// RDB/SST loads, CLUSTER*), blocking forms that park the connection on server-global wait lists
// (BLPOP & co, WAIT, XREAD ... BLOCK, SUBSCRIBE), and the scripting / search engines, which have
// their own targets (fuzz_lua.cc, fuzz_ft_*.cc). The database is flushed after every input.
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>

#include "common/string_util.h"
#include "kvrocks_fuzz_fixture.h"

namespace {

const std::unordered_set<std::string> kAllowed = {
    // strings / keys
    "append", "cad", "cas", "copy", "dbsize", "decr", "decrby", "del", "delex", "digest", "dump", "echo",
    "exists", "expire", "expireat", "expiretime", "get", "getdel", "getex", "getrange", "getset", "incr",
    "incrby", "incrbyfloat", "keys", "kmetadata", "lcs", "mget", "mset", "msetex", "msetnx", "object",
    "persist", "pexpire", "pexpireat", "pexpiretime", "psetex", "pttl", "randomkey", "rename", "renamenx",
    "restore", "scan", "set", "setex", "setnx", "setrange", "sort", "sort_ro", "strlen", "substr", "ttl",
    "type", "unlink", "move", "movex", "memory",
    // bitmaps / hyperloglog
    "bitcount", "bitfield", "bitfield_ro", "bitop", "bitpos", "getbit", "setbit", "pfadd", "pfcount",
    "pfmerge",
    // hashes
    "hdel", "hexists", "hexpire", "hexpireat", "hexpiretime", "hget", "hgetall", "hgetex", "hincrby",
    "hincrbyfloat", "hkeys", "hlen", "hmget", "hmset", "hpersist", "hpexpire", "hpexpireat", "hpexpiretime",
    "hpttl", "hrandfield", "hrangebylex", "hscan", "hset", "hsetex", "hsetexpire", "hsetnx", "hstrlen",
    "httl", "hvals",
    // lists (non-blocking)
    "lindex", "linsert", "llen", "lmove", "lmpop", "lpop", "lpos", "lpush", "lpushx", "lrange", "lrem",
    "lset", "ltrim", "rpop", "rpoplpush", "rpush", "rpushx",
    // sets / sorted int sets
    "sadd", "scard", "sdiff", "sdiffcard", "sdiffstore", "sinter", "sintercard", "sinterstore",
    "sismember", "smembers", "smismember", "smove", "spop", "srandmember", "srem", "sscan", "sunion",
    "sunionstore", "siadd", "sicard", "siexists", "sirange", "sirangebyvalue", "sirem", "sirevrange",
    "sirevrangebyvalue",
    // sorted sets
    "zadd", "zcard", "zcount", "zdiff", "zdiffstore", "zincrby", "zinter", "zintercard", "zinterstore",
    "zlexcount", "zmpop", "zmscore", "zpopmax", "zpopmin", "zrandmember", "zrange", "zrangebylex",
    "zrangebyscore", "zrangestore", "zrank", "zrem", "zremrangebylex", "zremrangebyrank",
    "zremrangebyscore", "zrevrange", "zrevrangebylex", "zrevrangebyscore", "zrevrank", "zscan", "zscore",
    "zunion", "zunionstore",
    // geo
    "geoadd", "geodist", "geohash", "geopos", "georadius", "georadiusbymember", "georadiusbymember_ro",
    "georadius_ro", "geosearch", "geosearchstore",
    // streams
    "xack", "xadd", "xautoclaim", "xclaim", "xdel", "xgroup", "xinfo", "xlen", "xpending", "xrange",
    "xread", "xreadgroup", "xrevrange", "xsetid", "xtrim",
    // transactions / connection
    "multi", "exec", "discard", "watch", "unwatch", "ping", "hello", "auth", "select", "reset", "quit",
    "time", "role", "command", "client", "publish", "mpublish", "pubsub",
};

// Families allowed by prefix: JSON, bloom, cuckoo, t-digest, time series.
const char *const kAllowedPrefixes[] = {"json.", "bf.", "cf.", "tdigest.", "ts."};

// CLIENT subcommands that only touch this connection or read the client list.
const std::unordered_set<std::string> kClientSub = {"kill", "list", "info", "id", "getname", "setname",
                                                    "reply", "no-evict", "no-touch"};

bool Allow(const CommandTokens &cmd) {
  std::string name = util::ToLower(cmd[0]);
  bool ok = kAllowed.count(name) > 0;
  if (!ok) {
    for (const char *p : kAllowedPrefixes) ok = ok || name.rfind(p, 0) == 0;
  }
  if (!ok) return false;
  if (name == "client") return cmd.size() >= 2 && kClientSub.count(util::ToLower(cmd[1])) > 0;
  if (name == "xread" || name == "xreadgroup") {
    // XREAD[GROUP] ... BLOCK <ms> parks the connection; the non-blocking form is kept.
    return std::none_of(cmd.begin() + 1, cmd.end(), [](const std::string &a) { return util::EqualICase(a, "block"); });
  }
  return true;
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  kvfuzz::Init();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  kvfuzz::ExecResp(data, size, Allow);
  kvfuzz::Reset();
  return 0;
}
