// «По кадрам» без поддержки ОС: macOS (общего способа узнать кадры чужого окна нет)
// и Linux, собранный без libxcb-present
#include "core/framewatch_os.h"

using namespace FrameWatch;

bool Os::supported() { return false; }
bool Os::needsAdmin() { return false; }
bool Os::byProcess() { return false; }

void Os::run(Hub& hub) {
    std::vector<Target> want;
    bool changed = false;
    while (hub.next(want, changed, std::chrono::milliseconds(250)))
        for (const Target& t : want)
            if (auto f = t.feed.lock()) f->setStatus(Status::Unsupported);
}
