#include "SignalChain.hpp"

#include <cstring>

namespace DSP {
  
SignalChain::SignalChain()
  : m_sampleRate{DEFAULT_SAMPLE_RATE}
  , m_avgProcTime{0}
  , m_maxProcTime{0}
  , m_avgBufferSize{0} {
}

void SignalChain::addFilter(std::unique_ptr<IFilter>&& filter) {
  m_filters.emplace_back(std::move(filter));
  m_filters.back()->setSampleRate(m_sampleRate);
}

void SignalChain::setSampleRate(int sampleRate) {
  
  m_sampleRate = sampleRate;

  for(auto& filter : m_filters) {
    filter->setSampleRate(m_sampleRate);
  }
}

void SignalChain::processSamples(SampleBuffer& samples) {
  for(auto& filter : m_filters) {
    filter->processSamples(samples);
  }
}

uint32_t SignalChain::avgProcTime() const {
  return m_avgProcTime;
}

uint32_t SignalChain::maxProcTime() const {
  return m_maxProcTime;
}

uint32_t SignalChain::avgBufferSize() const {
  return m_avgBufferSize;
}

}
