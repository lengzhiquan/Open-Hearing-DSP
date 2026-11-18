#pragma once

#include <atomic>

#include "ohdsp/params.h"

namespace ohdsp {

// Wait-free for the audio reader: at most two attempts, then the last good
// copy. The writer never blocks the reader. Single writer (UI / control).
class ParamBus {
 public:
  void publish(const Params& p) {
    const int pub = published_.load(std::memory_order_relaxed);
    int dest = -1;
    for (int i = 0; i < 3; ++i) {
      if (i == pub) continue;
      if (slots_[i].readers.load(std::memory_order_acquire) == 0) {
        dest = i;
        break;
      }
    }
    if (dest < 0) return;
    slots_[dest].p = p;
    published_.store(dest, std::memory_order_release);
  }

  Params read(const Params& fallback) const {
    Params cached = fallback;
    for (int attempt = 0; attempt < 2; ++attempt) {
      const int i = published_.load(std::memory_order_acquire);
      slots_[i].readers.fetch_add(1, std::memory_order_acq_rel);
      const int again = published_.load(std::memory_order_acquire);
      if (i == again) {
        cached = slots_[i].p;
        slots_[i].readers.fetch_sub(1, std::memory_order_release);
        return cached;
      }
      slots_[i].readers.fetch_sub(1, std::memory_order_release);
    }
    return cached;
  }

 private:
  struct Slot {
    Params p{};
    mutable std::atomic<int> readers{0};
  };
  mutable Slot slots_[3]{};
  std::atomic<int> published_{0};
};

}  // namespace ohdsp
