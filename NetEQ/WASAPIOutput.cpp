#include "WASAPIOutput.hpp"

#include <iostream>
#include <cstring>
#include <cstdlib>

#include <Audioclient.h>
#include <avrt.h>

WASAPIOutput::WASAPIOutput(IMMDevice *device, const WAVEFORMATEX& waveFormat)
	: m_device{ device }
	, m_waveFormat{ waveFormat }
	, m_stopEvent{ CreateEvent(NULL, FALSE, FALSE, NULL) }
	, m_audioThread{ [this]() { threadRoutine(); } } {
}

WASAPIOutput::~WASAPIOutput() {
	SetEvent(m_stopEvent);
	m_audioThread.join();
}

void WASAPIOutput::writeSamples(const DSP::SampleBuffer& leftSamples,
	const DSP::SampleBuffer& rightSamples) {

	std::lock_guard<std::mutex> sampleLock(m_sampleMutex);
	
	m_leftBuffer.insert(m_leftBuffer.end(), leftSamples.begin(), leftSamples.end());
	m_rightBuffer.insert(m_rightBuffer.end(), rightSamples.begin(), rightSamples.end());
}

void WASAPIOutput::packSamples(const DSP::SampleBuffer& left, const DSP::SampleBuffer& right,
	BYTE* output, size_t sampleCount) {

	for (size_t i = 0; i < sampleCount; ++i) {
		auto offset = i * 4;

		std::memcpy(output + offset, &right[i], 2);
		std::memcpy(output + offset + 2, &left[i], 2);
	}
}

void WASAPIOutput::threadRoutine() {
	HRESULT hr;

	// CoInitialize
	hr = CoInitialize(NULL);
	if (FAILED(hr)) {
		printf("CoInitialize failed: hr = 0x%08x\n", hr);
		return;
	}

	// activate an IAudioClient
	IAudioClient *pAudioClient;
	hr = m_device->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL, NULL,
		(void**)&pAudioClient
	);
	if (FAILED(hr)) {
		printf("IMMDevice::Activate(IAudioClient) failed: hr = 0x%08x", hr);
		CoUninitialize();
		return;
	}

	
	// get the mix format
	WAVEFORMATEX *pwfx;
	hr = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr)) {
		printf("IAudioClient::GetMixFormat failed: hr = 0x%08x\n", hr);
		pAudioClient->Release();
		CoUninitialize();
		return;
	}

	if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
		pwfx->wFormatTag = WAVE_FORMAT_PCM;
		pwfx->wBitsPerSample = 16;
		pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
		pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
	}
	else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		std::cout << "[Info] Output device format is EXTENSIBLE" << std::endl;

		auto* extendedFormat = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);

		if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, extendedFormat->SubFormat)) {
			//We need to coax the output into int16 format
			extendedFormat->Format.wBitsPerSample = 16;
			extendedFormat->Samples.wValidBitsPerSample = 16;
			extendedFormat->Format.nSamplesPerSec = 44100;
			extendedFormat->Format.nBlockAlign = extendedFormat->Format.nChannels * extendedFormat->Format.wBitsPerSample / 8;
			extendedFormat->Format.nAvgBytesPerSec = extendedFormat->Format.nBlockAlign * extendedFormat->Format.nSamplesPerSec;
			extendedFormat->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
		}
		else if(!IsEqualGUID(KSDATAFORMAT_SUBTYPE_PCM, extendedFormat->SubFormat)) {
			//Unknown format
			std::cerr << "[WASAPIOutput] Unable to convert output device format (unknown extended format)" << std::endl;
			return;
		}
	}
	else if(pwfx->wFormatTag != WAVE_FORMAT_PCM) {
		//Unknown format
		std::cerr << "[WASAPIOutput] Unable to convert output device format (unknown format tag)" << std::endl;
		return;
	}

	std::cout << "[WASAPIOutput] Bits per sample: " << pwfx->wBitsPerSample << ", Channels: " << pwfx->nChannels << ", Sample Rate: "
		<< pwfx->nSamplesPerSec << ", Block Align: " << pwfx->nBlockAlign << std::endl;

	REFERENCE_TIME hnsRequestedDuration = 0;
	pAudioClient->GetDevicePeriod(NULL, &hnsRequestedDuration);

	// initialize the audio client
	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
		0, 0, pwfx, NULL
	);
	CoTaskMemFree(pwfx);
	if (FAILED(hr)) {
		printf("[WASAPIOutput] IAudioClient::Initialize failed: hr 0x%08x\n", hr);
		pAudioClient->Release();
		CoUninitialize();
		return;
	}

	// get the buffer size
	UINT32 nFramesInBuffer;
	hr = pAudioClient->GetBufferSize(&nFramesInBuffer);
	if (FAILED(hr)) {
		printf("IAudioClient::GetBufferSize failed: hr 0x%08x\n", hr);
		pAudioClient->Release();
		CoUninitialize();
		return;
	}

	// get an IAudioRenderClient
	IAudioRenderClient *pAudioRenderClient;
	hr = pAudioClient->GetService(
		__uuidof(IAudioRenderClient),
		(void**)&pAudioRenderClient
	);
	if (FAILED(hr)) {
		printf("IAudioClient::GetService(IAudioRenderClient) failed: hr 0x%08x\n", hr);
		pAudioClient->Release();
		CoUninitialize();
		return;
	}

	// create a "feed me" event
	HANDLE hFeedMe;
	hFeedMe = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (NULL == hFeedMe) {
		DWORD dwErr = GetLastError();
		hr = HRESULT_FROM_WIN32(dwErr);
		printf("CreateEvent failed: last error is %u\n", dwErr);
		pAudioRenderClient->Release();
		pAudioClient->Release();
		CoUninitialize();
		return;
	}

	// set it as the event handle
	hr = pAudioClient->SetEventHandle(hFeedMe);
	if (FAILED(hr)) {
		printf("IAudioClient::SetEventHandle failed: hr = 0x%08x\n", hr);
		pAudioRenderClient->Release();
		pAudioClient->Release();
		CoUninitialize();
		return;
	}

	// pre-fill a single buffer of silence
	BYTE *pData;
	hr = pAudioRenderClient->GetBuffer(nFramesInBuffer, &pData);
	if (FAILED(hr)) {
		printf("IAudioRenderClient::GetBuffer failed on pre-fill: hr = 0x%08x\n", hr);
		pAudioClient->Stop();
		printf("TODO: unregister with MMCSS\n");
		CloseHandle(hFeedMe);
		pAudioRenderClient->Release();
		pAudioClient->Release();
		CoUninitialize();
		return;
	}

	// release the buffer with the silence flag
	hr = pAudioRenderClient->ReleaseBuffer(nFramesInBuffer, 0);
	if (FAILED(hr)) {
		printf("IAudioRenderClient::ReleaseBuffer failed on pre-fill: hr = 0x%08x\n", hr);
		pAudioClient->Stop();
		printf("TODO: unregister with MMCSS\n");
		CloseHandle(hFeedMe);
		pAudioRenderClient->Release();
		pAudioClient->Release();
		CoUninitialize();
		return;
	}

	// register with MMCSS
	DWORD nTaskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics(L"Pro Audio", &nTaskIndex);
	if (NULL == hTask) {
		DWORD dwErr = GetLastError();
		hr = HRESULT_FROM_WIN32(dwErr);
		printf("AvSetMmThreadCharacteristics failed: last error = %u\n", dwErr);
		pAudioRenderClient->Release();
		pAudioClient->Release();
		CoUninitialize();
		return;
	}

	// call Start
	hr = pAudioClient->Start();
	if (FAILED(hr)) {
		printf("IAudioClient::Start failed: hr = 0x%08x\n", hr);
		printf("TODO: unregister with MMCSS\n");
		AvRevertMmThreadCharacteristics(hTask);
		pAudioRenderClient->Release();
		pAudioClient->Release();
		CoUninitialize();
		return;
	}
	//SetEvent(pArgs->hStartedEvent);

	HANDLE waitArray[2] = { m_stopEvent, hFeedMe };
	DWORD dwWaitResult;

	bool bDone = false;
	for (UINT32 nPasses = 0; !bDone; nPasses++) {
		dwWaitResult = WaitForMultipleObjects(
			ARRAYSIZE(waitArray), waitArray,
			FALSE, INFINITE
		);

		if (WAIT_OBJECT_0 == dwWaitResult) {
			printf("Received stop event after %u passes\n", nPasses);
			bDone = true;
			continue; // exits loop
		}

		if (WAIT_OBJECT_0 + 1 != dwWaitResult) {
			hr = E_UNEXPECTED;
			printf("Unexpected WaitForMultipleObjects return value %u on pass %u\n", dwWaitResult, nPasses);
			pAudioClient->Stop();
			AvRevertMmThreadCharacteristics(hTask);
			CloseHandle(hFeedMe);
			pAudioRenderClient->Release();
			pAudioClient->Release();
			CoUninitialize();
			return;
		}

		// got "feed me" event - see how much padding there is
		//
		// padding is how much of the buffer is currently in use
		//
		// note in particular that event-driven (pull-mode) render should not
		// call GetCurrentPadding multiple times
		// in a single processing pass
		// this is in stark contrast to timer-driven (push-mode) render
		UINT32 nFramesOfPadding;
		hr = pAudioClient->GetCurrentPadding(&nFramesOfPadding);
		if (FAILED(hr)) {
			printf("IAudioClient::GetCurrentPadding failed on pass %u: hr = 0x%08x\n", nPasses, hr);
			pAudioClient->Stop();
			AvRevertMmThreadCharacteristics(hTask);
			CloseHandle(hFeedMe);
			pAudioRenderClient->Release();
			pAudioClient->Release();
			CoUninitialize();
			return;
		}

		if (nFramesOfPadding == nFramesInBuffer) {
			hr = E_UNEXPECTED;
			printf("Got \"feed me\" event but IAudioClient::GetCurrentPadding reports buffer is full - glitch?\n");
			pAudioClient->Stop();
			AvRevertMmThreadCharacteristics(hTask);
			CloseHandle(hFeedMe);
			pAudioRenderClient->Release();
			pAudioClient->Release();
			CoUninitialize();
			return;
		}

		hr = pAudioRenderClient->GetBuffer(nFramesInBuffer - nFramesOfPadding, &pData);
		if (FAILED(hr)) {
			printf("IAudioRenderClient::GetBuffer failed on pass %u: hr = 0x%08x - glitch?\n", nPasses, hr);
			pAudioClient->Stop();
			AvRevertMmThreadCharacteristics(hTask);
			CloseHandle(hFeedMe);
			pAudioRenderClient->Release();
			pAudioClient->Release();
			CoUninitialize();
			return;
		}

		// *** AT THIS POINT ***
		// If you wanted to render something besides silence,
		// you would fill the buffer pData
		// with (nFramesInBuffer - nFramesOfPadding) worth of audio data
		// this should be in the same wave format
		// that the stream was initialized with
		//
		// In particular, if you didn't want to use the mix format,
		// you would need to either ask for a different format in IAudioClient::Initialize
		// or do a format conversion
		//
		// If you do, then change the AUDCLNT_BUFFERFLAGS_SILENT flags value below to 0
		
		auto samplesNeeded = nFramesInBuffer - nFramesOfPadding;
		bool samplesAvailable = true;
		{
			std::lock_guard<std::mutex> lock(m_sampleMutex);

			if (m_leftBuffer.size() < samplesNeeded) {
				samplesNeeded = false;
			}
			else {
				packSamples(m_leftBuffer, m_rightBuffer, pData, samplesNeeded);
				m_leftBuffer.erase(m_leftBuffer.begin(), m_leftBuffer.begin() + samplesNeeded);
				m_rightBuffer.erase(m_rightBuffer.begin(), m_rightBuffer.begin() + samplesNeeded);
			}
		}

		if (!samplesAvailable) {
			std::cout << "[WASAPI] Output starved for samples, injecting silence instead"
				<< std::endl;
		}

		// release the buffer with the silence flag
		hr = pAudioRenderClient->ReleaseBuffer(nFramesInBuffer - nFramesOfPadding, 0);
			//samplesAvailable ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);
		if (FAILED(hr)) {
			printf("IAudioRenderClient::ReleaseBuffer failed on pass %u: hr = 0x%08x - glitch?\n", nPasses, hr);
			pAudioClient->Stop();
			AvRevertMmThreadCharacteristics(hTask);
			CloseHandle(hFeedMe);
			pAudioRenderClient->Release();
			pAudioClient->Release();
			CoUninitialize();
			return;
		}
	} // for each pass

	pAudioClient->Stop();
	AvRevertMmThreadCharacteristics(hTask);
	CloseHandle(hFeedMe);
	pAudioRenderClient->Release();
	pAudioClient->Release();
	CoUninitialize();
	return;
}
