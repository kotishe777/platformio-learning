#pragma once

#include <cstddef>
#include <cstdint>

template <size_t N>
class MovingAverage {
  static_assert(N > 0, "SMA window must not be empty");

 public:
  int add(int sample) {
    sum_            -= buffer_[index_];
    buffer_[index_]  = sample;
    sum_            += sample;
    index_           = (index_ + 1) % N;
    if (count_ < N) {
      ++count_;
    }
    return static_cast<int>(sum_ / static_cast<int32_t>(count_));
  }

 private:
  int     buffer_[N]{};
  int32_t sum_   = 0;
  size_t  index_ = 0;
  size_t  count_ = 0;
};
