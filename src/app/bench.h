// Timing marks for tools/bench.sh. Active only with SUPERFAR_BENCH=1;
// each mark prints one line to stderr: "bench <name> <ms since start>".
#pragma once

namespace bench {

void init();
bool enabled();
void mark(const char* name);

} // namespace bench
