// Per-process scratch directories for the kvrocks harnesses.
//
// Every harness process opens its own RocksDB (or writes its own config file) under $TMPDIR. A
// process that crashes never cleans up, and Mayhem replays each crashing or new test case in a
// fresh process, so without cleanup every replay leaves ~9 MB of RocksDB files behind until the
// scratch filesystem is full and every later Storage::Open fails. So each directory is named after
// its process (<prefix>-<pid>-XXXXXX), and creating one first removes the directories of processes
// that no longer exist.
#pragma once

#include <dirent.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace kvfuzz {

// Raises the soft file-size and open-files limits to their hard limits. A replay tool that lowers
// the soft limits would otherwise make RocksDB fail on its first write (EFBIG) or make
// Server::AdjustOpenFilesLimit exit(1), before the input is even read.
inline void RaiseSoftLimits() {
  for (int res : {RLIMIT_FSIZE, RLIMIT_NOFILE}) {
    rlimit lim;
    if (getrlimit(res, &lim) == 0 && lim.rlim_cur < lim.rlim_max) {
      lim.rlim_cur = lim.rlim_max;
      setrlimit(res, &lim);
    }
  }
}

inline std::string MakeScratchDir(const std::string &prefix) {
  const char *tmp = getenv("TMPDIR");
  const std::string base = tmp && *tmp ? tmp : "/tmp";
  const std::string head = prefix + "-";

  if (DIR *d = opendir(base.c_str())) {
    while (dirent *e = readdir(d)) {
      std::string name = e->d_name;
      if (name.compare(0, head.size(), head) != 0) continue;
      char *end = nullptr;
      long pid = strtol(name.c_str() + head.size(), &end, 10);
      if (pid <= 0 || !end || *end != '-') continue;
      if (kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH) continue;  // still running
      std::string path = base + "/" + name;
      struct stat st;
      if (lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) continue;
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }
    closedir(d);
  }

  std::string tmpl = base + "/" + head + std::to_string(getpid()) + "-XXXXXX";
  if (!mkdtemp(tmpl.data())) {
    perror("kvfuzz: mkdtemp");
    abort();
  }
  return tmpl;
}

}  // namespace kvfuzz
