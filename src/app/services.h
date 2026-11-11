// The per-window core services every tab shares: the dirsize worker
// pool, the thumbnailer and the job queue. Passed down explicitly (no globals).
#pragma once

namespace core {
class DirSizePool;
class JobQueue;
class Thumbnailer;
}
class Config;

struct Services {
    core::DirSizePool* sizes = nullptr;
    core::Thumbnailer* thumbs = nullptr;
    Config* config = nullptr;
    const core::JobQueue* jobs = nullptr;  // for panels that offer to queue what they start
};
