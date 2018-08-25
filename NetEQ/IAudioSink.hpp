#pragma once

#include "types.hpp"

class IAudioSink {
public:
	virtual void start() = 0;
	virtual void stop() = 0;
	virtual bool running() const = 0;

	virtual void registerCallback(const DSP::AudioCallback& cb) = 0;
};