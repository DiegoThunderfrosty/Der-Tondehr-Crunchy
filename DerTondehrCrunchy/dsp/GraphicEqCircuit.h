#pragma once
#include <array>
#include "CircuitValues.h"

namespace crunchy {
// Five physical series-RLC slider branches from the handwritten IIC+ sheet.
// Transistor differential amplifier is reduced to an ideal linear feedback stage.
// Both summing resistors are 3.3k, sliders 50k; branch states interact via summing node.
class GraphicEqCircuit {
  struct Branch {
    double resistance=1, rl=1, rc=1, conductance=1, position=.5;
    double current=0, voltageL=0, voltageC=0;
  };
public:
  void Prepare(double fs) {
    constexpr double inductance[]={circuit::GraphicEqBand1L,circuit::GraphicEqBand2L,circuit::GraphicEqBand3L,circuit::GraphicEqBand4L,circuit::GraphicEqBand5L};
    constexpr double capacitance[]={circuit::GraphicEqBand1C,circuit::GraphicEqBand2C,circuit::GraphicEqBand3C,circuit::GraphicEqBand4C,circuit::GraphicEqBand5C};
    constexpr double series[]={circuit::GraphicEqBand1Series,circuit::GraphicEqBand2Series,circuit::GraphicEqBand3Series,circuit::GraphicEqBand4Series,circuit::GraphicEqBand5Series};
    // Inductor DCR is not decoded in Pass 9. Keep the build-0.9 estimates as an
    // explicit provisional physical correction rather than mixing them with the
    // authoritative schematic series resistors.
    constexpr double provisionalWindingDcr[]={167,68,32,10,6};
    for (int i=0;i<5;++i) {
      auto& b=branches[i]; b={}; b.resistance=series[i]+provisionalWindingDcr[i];
      b.rl=2*fs*inductance[i]; b.rc=1/(2*fs*capacitance[i]);
      SetBand(i,0);
    }
  }
  void Reset() {
    for (auto& b : branches) { b.current = 0.0; b.voltageL = 0.0; b.voltageC = 0.0; }
  }
  void SetBand(int band,double gainDb) {
    auto& b=branches[band];
    const double desired=std::pow(10.,std::clamp(gainDb,-12.,12.)/20.);
    // Calibrate nominal dB at this isolated branch's LC resonance. With other
    // bands engaged the shared network changes the actual gain, as expected.
    double lo=0,hi=1;
    for (int i=0;i<24;++i) {
      const double p=(lo+hi)*.5, r=b.resistance+kPot*p*(1-p);
      const double gain=(r+kSum*p)/(r+kSum*(1-p));
      if (gain<desired) lo=p; else hi=p;
    }
    b.position=gainDb==0 ? .5 : (lo+hi)*.5;
    const double r=b.resistance+kPot*b.position*(1-b.position);
    b.conductance=1/(r+b.rl+b.rc);
  }
  double Process(double input) {
    std::array<double,5> historyL{},historyC{},history{};
    double conductanceIn=0,historyIn=0;
    for (int i=0;i<5;++i) {
      const auto& b=branches[i];
      historyL[i]=-b.rl*b.current-b.voltageL;
      historyC[i]=b.voltageC+b.rc*b.current;
      history[i]=historyL[i]+historyC[i];
      const double weight=(1-b.position)*b.conductance;
      conductanceIn+=weight; historyIn+=weight*history[i];
    }
    const double node=(input+kSum*historyIn)/(1+kSum*conductanceIn);
    double feedbackCurrent=0;
    for (int i=0;i<5;++i) {
      auto& b=branches[i];
      b.current=b.conductance*(node-history[i]);
      b.voltageL=b.rl*b.current+historyL[i];
      b.voltageC=b.rc*b.current+historyC[i];
      feedbackCurrent+=b.position*b.current;
    }
    return node+kSum*feedbackCurrent;
  }
private:
  static constexpr double kPot=circuit::GraphicEqSliderOhms,kSum=circuit::GraphicEqSummingOhms;
  std::array<Branch,5> branches{};
};
}
