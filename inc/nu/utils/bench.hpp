#pragma once

#include <algorithm>
#include <iostream>

template <typename T>
void print_percentile(T *container) {
  sort(container->begin(), container->end());
  for (auto percentile = 10; percentile < 100; percentile += 10) {
    auto idx = percentile / 100.0 * container->size();
    std::cout << percentile << "\t" << (*container)[idx] << std::endl;
  }
}
