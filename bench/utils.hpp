#pragma once

/**
 * Measures how long a function takes to execute in microseconds.
 */
template <typename F, typename... Args>
uint64_t time(F fn, Args &&... args) {
  auto t0 = microtime();
  fn(std::forward(args)...);
  auto t1 = microtime();
  return t1 - t0;
}
