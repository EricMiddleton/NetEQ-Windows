#pragma once

#include <mutex>
#include <thread>

#include <mmdeviceapi.h>

#include "types.hpp"

class WASAPIOutput {
public:
	WASAPIOutput(IMMDevice *device, const WAVEFORMATEX& waveFormat);
	~WASAPIOutput();

	void writeSamples(const DSP::SampleBuffer& leftSamples, const DSP::SampleBuffer& rightSamples);

private:
	void threadRoutine();
	static void packSamples(const DSP::SampleBuffer& left, const DSP::SampleBuffer& right,
		BYTE* output, size_t sampleCount);

	IMMDevice *m_device;
	WAVEFORMATEX m_waveFormat;
	
	DSP::SampleBuffer m_leftBuffer, m_rightBuffer;
	std::mutex m_sampleMutex;

	HANDLE m_stopEvent;
	std::thread m_audioThread;
};