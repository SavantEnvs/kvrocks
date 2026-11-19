// FT.SEARCHSQL / FT.EXPLAINSQL: the KQIR SQL front end (src/search/sql_parser.h, sql_transformer.h),
// semantic checks (ir_sema_checker.h), the IR passes (src/search/passes/) and plan execution against
// a pre-created index `idx` (kvrocks_fuzz_search.h). The input is the SQL query string.
#include <cstdint>
#include <deque>
#include <string>

#include "kvrocks_fuzz_search.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
  kvfuzz::InitSearch();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string sql(reinterpret_cast<const char *>(data), size);
  std::deque<CommandTokens> q = {{"ft.explainsql", sql}, {"ft.searchsql", sql}};
  kvfuzz::Exec(std::move(q));
  return 0;
}
