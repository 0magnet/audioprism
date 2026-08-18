#include <cstring>
#include <complex>
#include <unistd.h>

#include "SpectrogramThread.hpp"

SpectrogramThread::SpectrogramThread(ThreadSafeQueue<std::vector<float>> &samplesQueue, ThreadSafeQueue<std::vector<uint32_t>> &pixelsQueue, const Configuration::Settings &initialSettings) : _samplesQueue(samplesQueue), _pixelsQueue(pixelsQueue), _realDft(initialSettings.dftSize, initialSettings.dftWindowFunction), _spectrumRenderer(initialSettings.magnitudeMin, initialSettings.magnitudeMax, initialSettings.magnitudeLog, initialSettings.colorScheme) {
    _samplesOverlap = static_cast<unsigned int>(initialSettings.samplesOverlap * static_cast<float>(initialSettings.dftSize));
    _pixelLine.resize((initialSettings.orientation == Configuration::Orientation::Vertical) ? initialSettings.width : initialSettings.height);
    _samplesQueueCount = 0;
}

void SpectrogramThread::start() {
    _running = true;
    _thread = std::thread(&SpectrogramThread::_run, this);
}

void SpectrogramThread::stop() {
    _running = false;
    _thread.join();
}

void SpectrogramThread::_run() {
    /* Audio Samples Buffer */
    std::vector<float> audioSamples;
    /* Overlapped Samples */
    std::vector<float> overlapSamples;
    /* DFT of Overlapped Samples */
    std::vector<std::complex<float>> dftSamples;

    while (_running) {
        std::vector<float> newAudioSamples;

        /* Poll with timeout, in case this thread is asked to stop */
        if (!_samplesQueue.wait(std::chrono::milliseconds(100)))
            continue;

        /* Pop new audio samples */
        newAudioSamples = _samplesQueue.pop();

        /* Track samples queue count for debug statistics */
        _samplesQueueCount = _samplesQueue.count();

        /* Add new audio samples to our audio samples buffer */
        audioSamples.insert(audioSamples.end(), newAudioSamples.begin(), newAudioSamples.end());

        {
            /* Lock DFT */
            std::lock_guard<std::mutex> dftLg(_realDftLock);

            /* Resize overlap samples buffer and DFT samples buffer if N changed */
            if (overlapSamples.size() != _realDft.getSize()) {
                overlapSamples.resize(_realDft.getSize());
                dftSamples.resize(_realDft.getSize() / 2 + 1);
            }

            /* One hop of new audio advances the window by one column, so that
             * is what we wait for, what we consume, and what we append. The
             * remaining _samplesOverlap samples are the part this window shares
             * with the last one, and they move down from one hop in.
             *
             * All three counts used to be the other quantity: it waited for and
             * consumed _samplesOverlap samples per column while only ever
             * appending a hop of them, and it moved the wrong part of the
             * window down. At 50% overlap the hop and the overlap are equal and
             * none of it shows. Away from 50% it advanced the display at the
             * wrong rate through audio it was partly discarding, over a window
             * left holding stale samples. */
            const unsigned int hop = static_cast<unsigned int>(overlapSamples.size()) - _samplesOverlap;

            /* If we don't have a hop's worth of samples yet, pop more */
            if (audioSamples.size() < hop)
                continue;

            /* Move down the samples this window shares with the last one */
            memmove(overlapSamples.data(), overlapSamples.data() + hop, sizeof(float) * _samplesOverlap);
            /* Append one hop of new samples */
            memcpy(overlapSamples.data() + _samplesOverlap, audioSamples.data(), sizeof(float) * hop);
            /* Erase used audio samples */
            audioSamples.erase(audioSamples.begin(), audioSamples.begin() + hop);

            /* Compute DFT */
            _realDft.compute(dftSamples, overlapSamples);
        }

        {
            /* Lock spectrum renderer */
            std::lock_guard<std::mutex> spectrumLg(_spectrumRendererLock);
            /* Render spectrogram line */
            _spectrumRenderer.render(_pixelLine, dftSamples);
        }

        /* Put into pixels queue */
        _pixelsQueue.push(_pixelLine);
    }
}

void SpectrogramThread::setWidth(unsigned int width) {
    std::lock_guard<std::mutex> dftLg(_spectrumRendererLock);
    _pixelLine.resize(width);
}

float SpectrogramThread::getSamplesOverlap() {
    std::lock_guard<std::mutex> dftLg(_realDftLock);
    return static_cast<float>(_samplesOverlap) / static_cast<float>(_realDft.getSize());
}

void SpectrogramThread::setSamplesOverlap(float overlap) {
    std::lock_guard<std::mutex> dftLg(_realDftLock);
    _samplesOverlap = static_cast<unsigned int>(overlap * static_cast<float>(_realDft.getSize()));
}

unsigned int SpectrogramThread::getDftSize() {
    std::lock_guard<std::mutex> dftLg(_realDftLock);
    return _realDft.getSize();
}

void SpectrogramThread::setDftSize(unsigned int N) {
    float overlap = getSamplesOverlap();

    {
        std::lock_guard<std::mutex> dftLg(_realDftLock);
        _realDft.setSize(N);
    }

    /* Preserve overlap percentage */
    setSamplesOverlap(overlap);
}

DFT::RealDft::WindowFunction SpectrogramThread::getDftWindowFunction() {
    std::lock_guard<std::mutex> dftLg(_realDftLock);
    return _realDft.getWindowFunction();
}

void SpectrogramThread::setDftWindowFunction(DFT::RealDft::WindowFunction wf) {
    std::lock_guard<std::mutex> dftLg(_realDftLock);
    _realDft.setWindowFunction(wf);
}

float SpectrogramThread::getMagnitudeMin() {
    std::lock_guard<std::mutex> spectrumLg(_spectrumRendererLock);
    return _spectrumRenderer.getMagnitudeMin();
}

void SpectrogramThread::setMagnitudeMin(float min) {
    std::lock_guard<std::mutex> spectrumLg(_spectrumRendererLock);
    _spectrumRenderer.setMagnitudeMin(min);
}

float SpectrogramThread::getMagnitudeMax() {
    std::lock_guard<std::mutex> spectrumLg(_spectrumRendererLock);
    return _spectrumRenderer.getMagnitudeMax();
}

void SpectrogramThread::setMagnitudeMax(float max) {
    std::lock_guard<std::mutex> spectrumLg(_spectrumRendererLock);
    _spectrumRenderer.setMagnitudeMax(max);
}

bool SpectrogramThread::getMagnitudeLog() {
    std::lock_guard<std::mutex> spectrumLg(_spectrumRendererLock);
    return _spectrumRenderer.getMagnitudeLog();
}

void SpectrogramThread::setMagnitudeLog(bool logarithmic) {
    std::lock_guard<std::mutex> spectrumLg(_spectrumRendererLock);
    _spectrumRenderer.setMagnitudeLog(logarithmic);
}

Spectrogram::SpectrumRenderer::ColorScheme SpectrogramThread::getColorScheme() {
    std::lock_guard<std::mutex> spectrumLg(_spectrumRendererLock);
    return _spectrumRenderer.getColorScheme();
}

void SpectrogramThread::setColorScheme(Spectrogram::SpectrumRenderer::ColorScheme colorScheme) {
    std::lock_guard<std::mutex> spectrumLg(_spectrumRendererLock);
    _spectrumRenderer.setColorScheme(colorScheme);
}

size_t SpectrogramThread::getDebugSamplesQueueCount() {
    return _samplesQueueCount;
}
