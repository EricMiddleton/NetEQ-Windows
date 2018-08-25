#pragma once

#include <memory>

#include "IAudioSource.hpp"
#include "IAudioSink.hpp"

class AudioManager {
public:
	AudioManager();
	
	void addAudioSource(const std::shared_ptr<IAudioSink>& sink);
	void addAudioSink(const std::shared_ptr<IAudioSource>& source);



};