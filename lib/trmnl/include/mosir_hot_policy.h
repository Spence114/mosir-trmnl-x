#pragma once
#include <stdint.h>

namespace Mosir {
inline bool eligible(bool usb, bool nativeWifi, bool optedIn, uint64_t rate,
                     bool normalResponse, bool specialAction) {
  return usb && nativeWifi && optedIn && rate >= 1 && rate <= 5 && normalResponse && !specialAction;
}

struct Gesture { uint8_t channel; bool held; };
// Fed by the touch controller task, so taps and holds during an HTTP request are retained.
// The caller supplies synchronization. Overflow drops newest events, never overwrites old ones.
class Gestures {
public:
  void reset(uint8_t alreadyDown = 0) {
    *this = Gestures();
    for (uint8_t c = 0; c < 3; ++c) {
      down_[c] = fired_[c] = (alreadyDown & (1 << c)) != 0;
    }
  }
  void poll(uint8_t mask, uint32_t now) {
    for (uint8_t c = 0; c < 3; ++c) {
      bool down = mask & (1 << c);
      if (down && !down_[c]) { since_[c] = now; fired_[c] = false; }
      if (down && !fired_[c] && uint32_t(now - since_[c]) >= 2000) {
        push({c, true}); fired_[c] = true;
      }
      if (!down && down_[c] && !fired_[c]) push({c, false});
      down_[c] = down;
    }
  }
  bool take(Gesture &out) {
    if (!count_) return false;
    out = queue_[head_]; head_ = (head_ + 1) % 8; --count_; return true;
  }
private:
  void push(Gesture event) {
    if (count_ == 8) return;
    queue_[(head_ + count_) % 8] = event; ++count_;
  }
  Gesture queue_[8] = {};
  uint8_t head_ = 0, count_ = 0;
  bool down_[3] = {}, fired_[3] = {};
  uint32_t since_[3] = {};
};

class RefreshBudget {
public:
  bool full(uint32_t now, uint32_t maxPartials, uint32_t cleanMs) const {
    return !valid_ || partials_ >= maxPartials || uint32_t(now - lastFull_) >= cleanMs;
  }
  void completed(bool full, uint32_t now) {
    valid_ = true;
    if (full) { partials_ = 0; lastFull_ = now; } else ++partials_;
  }
  void invalidate() { valid_ = false; }
  bool valid() const { return valid_; }
private:
  bool valid_ = false;
  uint32_t partials_ = 0, lastFull_ = 0;
};
}
