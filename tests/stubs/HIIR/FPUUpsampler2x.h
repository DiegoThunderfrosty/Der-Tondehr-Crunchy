#pragma once
// Test-only HIIR compatibility stub. Production plugin builds use iPlug2's real
// HIIR implementation. This keeps the DSP state/routing stress tests standalone.
namespace hiir {
template<int N, class T> class Upsampler2xFPU {
public:
  void clear_buffers() {}
  void set_coefs(const double*) {}
  void process_sample(T& a, T& b, T x) { a = x; b = x; }
};
}
