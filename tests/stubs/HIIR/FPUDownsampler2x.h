#pragma once
// Test-only HIIR compatibility stub. Production plugin builds use iPlug2's real
// HIIR implementation. This validates circuit logic without vendoring HIIR.
namespace hiir {
template<int N, class T> class Downsampler2xFPU {
public:
  void clear_buffers() {}
  void set_coefs(const double*) {}
  T process_sample(const T* x) { return x[0]; }
};
}
