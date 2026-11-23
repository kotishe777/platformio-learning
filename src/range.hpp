#pragma once

constexpr int clampValue(int value, int lo, int hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

constexpr int mapRange(int value, int inMin, int inMax, int outMin, int outMax) {
  return outMin + (clampValue(value, inMin, inMax) - inMin) * (outMax - outMin) / (inMax - inMin);
}
