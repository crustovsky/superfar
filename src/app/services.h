// The per-window core services every tab shares: the dirsize worker
// pool and the thumbnailer. Passed down explicitly (no globals).
#pragma once

namespace core {
class DirSizePool;
class Thumbnailer;
}
class Config;

struct Services {
    core::DirSizePool* sizes = nullptr;
    core::Thumbnailer* thumbs = nullptr;
    Config* config = nullptr;
};
