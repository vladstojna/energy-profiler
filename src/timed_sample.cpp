#include "timed_sample.hpp"

namespace tep {
bool timed_sample::operator==(const timed_sample &rhs) const noexcept {
  return timestamp == rhs.timestamp && sample == rhs.sample;
}

bool timed_sample::operator!=(const timed_sample &rhs) const noexcept {
  return !(*this == rhs);
}

timed_sample::duration
timed_sample::operator-(const timed_sample &rhs) const noexcept {
  return timestamp - rhs.timestamp;
}

timed_sample::operator const nrgprf::sample &() const noexcept {
  return sample;
}

timed_sample::operator nrgprf::sample &() noexcept { return sample; }
} // namespace tep
