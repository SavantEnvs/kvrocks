// Leaks are not the bug class fuzzed here: turn LeakSanitizer off, keep ASan + UBSan.
extern "C" int __lsan_is_turned_off() { return 1; }
