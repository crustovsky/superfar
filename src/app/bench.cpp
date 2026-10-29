#include "bench.h"

#include <QElapsedTimer>

#include <cstdio>
#include <cstdlib>

namespace bench {

namespace {
QElapsedTimer timer;
bool active = false;
} // namespace

void init()
{
    const char* env = std::getenv("SUPERFAR_BENCH");
    active = env && *env && *env != '0';
    timer.start();
}

bool enabled()
{
    return active;
}

void mark(const char* name)
{
    if (!active)
        return;
    std::fprintf(stderr, "bench %s %lld\n", name, static_cast<long long>(timer.elapsed()));
    std::fflush(stderr);
}

} // namespace bench
