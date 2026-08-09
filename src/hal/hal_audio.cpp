/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/i2s.h>
#include <generated/embedded_assets.h>
#include <mooncake_log.h>
#include <utility/M5IOE1_Class.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstring>
#include <mutex>

namespace {

constexpr std::string_view tag = "HAL-Audio";
constexpr i2s_port_t i2sPort = I2S_NUM_0;
constexpr gpio_num_t i2sMclkPin = GPIO_NUM_18;
constexpr gpio_num_t i2sBclkPin = GPIO_NUM_17;
constexpr gpio_num_t i2sLrckPin = GPIO_NUM_15;
constexpr gpio_num_t i2sDataInPin = GPIO_NUM_16;
constexpr gpio_num_t i2sDataOutPin = GPIO_NUM_21;
constexpr gpio_num_t speakerPaPin = GPIO_NUM_14;
constexpr uint8_t es8311Address = 0x18;
constexpr uint32_t codecI2cFrequency = 100000;

class AudioEngine {
public:
    static constexpr int sampleRate = 44100;
    static constexpr int fftSize = 512;
    static constexpr int hopSize = 256;

    bool init()
    {
        if (!initI2s()) {
            return false;
        }
        if (!initCodec()) {
            i2s_driver_uninstall(i2sPort);
            return false;
        }

        initSpectrum();
        if (xTaskCreate([](void* context) { static_cast<AudioEngine*>(context)->playTask(); }, "audio_play", 5 * 1024,
                        this, 5, &_playTaskHandle) != pdPASS) {
            mclog::tagError(tag, "failed to create audio playback task");
            disableSpeakerPa();
            i2s_driver_uninstall(i2sPort);
            return false;
        }
        _ready.store(true);
        return true;
    }

    bool setVolume(int volume)
    {
        volume = std::clamp(volume, 0, 100);
        std::lock_guard<std::mutex> lock(_codecMutex);
        if (!writeCodecRegister(0x32, volumeRegister(volume))) {
            return false;
        }
        _volume.store(volume);
        return true;
    }

    int getVolume() const
    {
        return _volume.load();
    }

    void play(const std::vector<int16_t>& data, bool async)
    {
        if (!_ready.load()) {
            return;
        }
        if (!async) {
            if (_isPlaying.exchange(true)) {
                mclog::tagWarn(tag, "audio is already playing");
                return;
            }
            {
                std::lock_guard<std::mutex> outputLock(_outputMutex);
                if (data.empty()) {
                    i2s_zero_dma_buffer(i2sPort);
                } else {
                    writePcm(data, false);
                    writeSilence(false);
                }
            }
            _isPlaying.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(_playMutex);
            _pendingAudio = data;
            _hasPendingRequest = true;
        }
        if (_playTaskHandle != nullptr) {
            xTaskNotifyGive(_playTaskHandle);
        }
    }

    void record(std::vector<int16_t>& data, uint16_t durationMs, float gain)
    {
        std::lock_guard<std::mutex> lock(_micMutex);
        if (!_ready.load() || !setMicGain(gain)) {
            data.clear();
            return;
        }

        const std::size_t sampleCount = static_cast<std::size_t>(sampleRate) * durationMs / 1000;
        data.assign(sampleCount, 0);
        if (sampleCount == 0 || !readMono(data.data(), data.size())) {
            data.clear();
        }
    }

    void updateSpectrum(Hal::AudioSpectrumFrame& frame)
    {
        std::unique_lock<std::mutex> lock(_micMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }
        if (!_ready.load() || !readMono(_spectrumPcmHop.data(), _spectrumPcmHop.size())) {
            return;
        }

        appendSpectrumHop();
        if (_spectrumSamplesReady >= fftSize) {
            processSpectrumFrame(frame);
        }
    }

private:
    bool initI2s()
    {
        i2s_config_t config{};
        config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
        config.sample_rate = sampleRate;
        config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
        config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
        config.dma_buf_count = 8;
        config.dma_buf_len = 256;
        config.use_apll = true;
        config.tx_desc_auto_clear = true;
        config.fixed_mclk = sampleRate * 256;
        config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

        esp_err_t error = i2s_driver_install(i2sPort, &config, 0, nullptr);
        if (error != ESP_OK) {
            mclog::tagError(tag, "full-duplex I2S install failed: {}", static_cast<int>(error));
            return false;
        }

        i2s_pin_config_t pins{};
        pins.mck_io_num = i2sMclkPin;
        pins.bck_io_num = i2sBclkPin;
        pins.ws_io_num = i2sLrckPin;
        pins.data_out_num = i2sDataOutPin;
        pins.data_in_num = i2sDataInPin;
        error = i2s_set_pin(i2sPort, &pins);
        if (error != ESP_OK) {
            mclog::tagError(tag, "full-duplex I2S pin setup failed: {}", static_cast<int>(error));
            i2s_driver_uninstall(i2sPort);
            return false;
        }
        i2s_zero_dma_buffer(i2sPort);
        return true;
    }

    bool writeCodecRegister(uint8_t reg, uint8_t value)
    {
        // Match M5Unified's StopWatch codec transaction: send the register and
        // payload as separate write stages, with the same three retries.
        for (uint8_t attempt = 0; attempt < 4; ++attempt) {
            if (M5.In_I2C.writeRegister(es8311Address, reg, &value, 1, codecI2cFrequency)) {
                return true;
            }
            if (attempt < 3) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        mclog::tagError(tag, "ES8311 write failed: reg=0x{:02X} value=0x{:02X}", reg, value);
        return false;
    }

    bool readCodecRegister(uint8_t reg, uint8_t& value)
    {
        if (!M5.In_I2C.readRegister(es8311Address, reg, &value, 1, codecI2cFrequency)) {
            mclog::tagError(tag, "ES8311 read failed: reg=0x{:02X}", reg);
            return false;
        }
        return true;
    }

    bool initCodec()
    {
        auto& ioe = static_cast<m5::M5IOE1_Class&>(M5.getIOExpander(0));
        ioe.setHighImpedance(m5::M5IOE1_Class::gpio3, false);
        ioe.setHighImpedance(m5::M5IOE1_Class::gpio10, false);
        ioe.setDirection(m5::M5IOE1_Class::gpio3, true);
        ioe.setDirection(m5::M5IOE1_Class::gpio10, true);
        ioe.digitalWrite(m5::M5IOE1_Class::gpio10, false);
        ioe.digitalWrite(m5::M5IOE1_Class::gpio3, true);
        gpio_set_direction(speakerPaPin, GPIO_MODE_OUTPUT);
        gpio_set_level(speakerPaPin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));

        struct RegisterValue {
            uint8_t reg;
            uint8_t value;
        };
        // Register order and values mirror esp_codec_dev 1.5.4 with
        // WORK_MODE_BOTH, slave I2S, external MCLK and 44.1 kHz/16-bit PCM.
        static constexpr RegisterValue openSequence[] = {
            {0x44, 0x08}, {0x44, 0x08}, {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10},
            {0x16, 0x24}, {0x04, 0x10}, {0x05, 0x00}, {0x0B, 0x00}, {0x0C, 0x00},
            {0x10, 0x1F}, {0x11, 0x7F}, {0x00, 0x80}, {0x01, 0x3F}, {0x13, 0x10},
            {0x1B, 0x0A}, {0x1C, 0x6A}, {0x44, 0x58},
        };
        for (const auto& item : openSequence) {
            if (!writeCodecRegister(item.reg, item.value)) {
                disableSpeakerPa();
                return false;
            }
        }

        uint8_t interfaceClock = 0;
        if (!readCodecRegister(0x06, interfaceClock) ||
            !writeCodecRegister(0x06, static_cast<uint8_t>(interfaceClock & ~0x20U))) {
            disableSpeakerPa();
            return false;
        }

        static constexpr RegisterValue formatAndClockSequence[] = {
            {0x09, 0x0C}, {0x0A, 0x0C}, {0x02, 0x00}, {0x05, 0x00}, {0x03, 0x10},
            {0x04, 0x10}, {0x07, 0x00}, {0x08, 0xFF}, {0x06, 0x03},
        };
        for (const auto& item : formatAndClockSequence) {
            if (!writeCodecRegister(item.reg, item.value)) {
                disableSpeakerPa();
                return false;
            }
        }

        if (!setMicGain(30.0f)) {
            disableSpeakerPa();
            return false;
        }

        static constexpr RegisterValue startSequence[] = {
            {0x00, 0x80}, {0x01, 0x3F}, {0x09, 0x0C}, {0x0A, 0x0C}, {0x17, 0xBF},
            {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01}, {0x15, 0x40},
            {0x37, 0x08}, {0x45, 0x00},
        };
        for (const auto& item : startSequence) {
            if (!writeCodecRegister(item.reg, item.value)) {
                disableSpeakerPa();
                return false;
            }
        }

        ioe.digitalWrite(m5::M5IOE1_Class::gpio10, true);
        gpio_set_level(speakerPaPin, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        return true;
    }

    void disableSpeakerPa()
    {
        auto& ioe = static_cast<m5::M5IOE1_Class&>(M5.getIOExpander(0));
        ioe.digitalWrite(m5::M5IOE1_Class::gpio10, false);
        ioe.digitalWrite(m5::M5IOE1_Class::gpio3, false);
        gpio_set_level(speakerPaPin, 0);
    }

    static uint8_t volumeRegister(int volume)
    {
        const float requestedDb = volume == 0 ? -96.0f : -50.0f + static_cast<float>(volume) * 0.5f;
        // esp_codec_dev 1.5.4 default hardware gain for an unspecified board:
        // 20*log10(3.3V/5.0V) = -3.609121 dB.
        const float codecDb = std::clamp(requestedDb + 3.609121f, -95.5f, 32.0f);
        return static_cast<uint8_t>((codecDb + 95.5f) * 2.0f);
    }

    bool setMicGain(float gainDb)
    {
        gainDb = std::max(gainDb, 0.0f);
        uint8_t gainRegister = 0;
        if (gainDb >= 42.0f) {
            gainRegister = 7;
        } else if (gainDb >= 36.0f) {
            gainRegister = 6;
        } else if (gainDb >= 30.0f) {
            gainRegister = 5;
        } else if (gainDb >= 24.0f) {
            gainRegister = 4;
        } else if (gainDb >= 18.0f) {
            gainRegister = 3;
        } else if (gainDb >= 12.0f) {
            gainRegister = 2;
        } else if (gainDb >= 6.0f) {
            gainRegister = 1;
        }
        std::lock_guard<std::mutex> lock(_codecMutex);
        return writeCodecRegister(0x16, gainRegister);
    }

    bool readMono(int16_t* destination, std::size_t sampleCount)
    {
        std::array<int16_t, 512> stereo{};
        std::size_t completed = 0;
        while (completed < sampleCount) {
            const std::size_t framesRequested = std::min<std::size_t>(256, sampleCount - completed);
            std::size_t bytesRead = 0;
            const esp_err_t error = i2s_read(i2sPort, stereo.data(), framesRequested * 2 * sizeof(int16_t),
                                             &bytesRead, portMAX_DELAY);
            if (error != ESP_OK || bytesRead < 2 * sizeof(int16_t)) {
                mclog::tagError(tag, "I2S record failed: error={} bytes={}", static_cast<int>(error), bytesRead);
                return false;
            }
            const std::size_t framesRead = std::min(bytesRead / (2 * sizeof(int16_t)), sampleCount - completed);
            for (std::size_t frame = 0; frame < framesRead; ++frame) {
                // ES8311 exposes ADCL in the left slot and the DAC reference in
                // the right slot when its internal ADC/DAC reference is enabled.
                destination[completed + frame] = stereo[frame * 2];
            }
            completed += framesRead;
        }
        return true;
    }

    bool writeMonoChunk(const int16_t* source, std::size_t sampleCount)
    {
        std::array<int16_t, 1024> stereo{};
        for (std::size_t index = 0; index < sampleCount; ++index) {
            stereo[index * 2] = source[index];
            stereo[index * 2 + 1] = source[index];
        }
        const std::size_t totalBytes = sampleCount * 2 * sizeof(int16_t);
        std::size_t offset = 0;
        while (offset < totalBytes) {
            std::size_t written = 0;
            const esp_err_t error = i2s_write(i2sPort, reinterpret_cast<uint8_t*>(stereo.data()) + offset,
                                               totalBytes - offset, &written, portMAX_DELAY);
            if (error != ESP_OK || written == 0) {
                mclog::tagError(tag, "I2S playback failed: error={} bytes={}", static_cast<int>(error), written);
                return false;
            }
            offset += written;
        }
        return true;
    }

    bool writePcm(const std::vector<int16_t>& data, bool interruptible)
    {
        std::size_t offset = 0;
        while (offset < data.size()) {
            if (interruptible && ulTaskNotifyTake(pdTRUE, 0) > 0) {
                return false;
            }
            const std::size_t count = std::min<std::size_t>(512, data.size() - offset);
            if (!writeMonoChunk(data.data() + offset, count)) {
                return false;
            }
            offset += count;
        }
        return true;
    }

    bool writeSilence(bool interruptible)
    {
        static const std::array<int16_t, 512> silence{};
        std::size_t remaining = sampleRate / 10;
        while (remaining > 0) {
            if (interruptible && ulTaskNotifyTake(pdTRUE, 0) > 0) {
                return false;
            }
            const std::size_t count = std::min<std::size_t>(silence.size(), remaining);
            if (!writeMonoChunk(silence.data(), count)) {
                return false;
            }
            remaining -= count;
        }
        return true;
    }

    void playTask()
    {
        std::vector<int16_t> current;
        for (;;) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            for (;;) {
                bool hasRequest = false;
                {
                    std::lock_guard<std::mutex> lock(_playMutex);
                    current.swap(_pendingAudio);
                    _pendingAudio.clear();
                    hasRequest = _hasPendingRequest;
                    _hasPendingRequest = false;
                }

                if (!hasRequest) {
                    break;
                }
                if (current.empty()) {
                    std::lock_guard<std::mutex> outputLock(_outputMutex);
                    i2s_zero_dma_buffer(i2sPort);
                    _isPlaying.store(false);
                    break;
                }

                _isPlaying.store(true);
                bool completed = false;
                {
                    std::lock_guard<std::mutex> outputLock(_outputMutex);
                    completed = writePcm(current, true) && writeSilence(true);
                    if (!completed) {
                        i2s_zero_dma_buffer(i2sPort);
                    }
                }
                _isPlaying.store(false);
                if (!completed) {
                    continue;
                }
                if (ulTaskNotifyTake(pdTRUE, 0) == 0) {
                    break;
                }
            }
        }
    }

    void initSpectrum()
    {
        constexpr float pi = 3.14159265358979323846f;
        for (int i = 0; i < fftSize; ++i) {
            _spectrumWindow[i] = 0.5f - 0.5f * std::cos((2.0f * pi * i) / static_cast<float>(fftSize - 1));
        }

        constexpr int maxBin = fftSize / 2;
        const float nyquist = static_cast<float>(sampleRate) * 0.5f;
        const float minHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
        const float logMin = std::log10(minHz);
        const float logMax = std::log10(nyquist);

        _bandBinEdges[0] = 1;
        for (std::size_t i = 1; i < Hal::AudioSpectrumFrame::bandCount; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(Hal::AudioSpectrumFrame::bandCount);
            const float edgeHz = std::pow(10.0f, logMin + (logMax - logMin) * t);
            const int edgeBin = static_cast<int>(std::lround(edgeHz * fftSize / sampleRate));
            const int minEdge = _bandBinEdges[i - 1] + 1;
            const int maxEdge = maxBin - static_cast<int>(Hal::AudioSpectrumFrame::bandCount - i);
            _bandBinEdges[i] = std::clamp(edgeBin, minEdge, maxEdge);
        }
        _bandBinEdges[Hal::AudioSpectrumFrame::bandCount] = maxBin;
    }

    void appendSpectrumHop()
    {
        std::move(_spectrumTimeDomain.begin() + hopSize, _spectrumTimeDomain.end(), _spectrumTimeDomain.begin());
        for (int i = 0; i < hopSize; ++i) {
            _spectrumTimeDomain[fftSize - hopSize + i] = static_cast<float>(_spectrumPcmHop[i]) / 32768.0f;
        }
        _spectrumSamplesReady = std::min<std::size_t>(_spectrumSamplesReady + hopSize, fftSize);
    }

    void processSpectrumFrame(Hal::AudioSpectrumFrame& frame)
    {
        float mean = 0.0f;
        for (float sample : _spectrumTimeDomain) {
            mean += sample;
        }
        mean /= static_cast<float>(fftSize);

        for (int i = 0; i < fftSize; ++i) {
            _spectrumFftBuffer[i] = {(_spectrumTimeDomain[i] - mean) * _spectrumWindow[i], 0.0f};
        }
        stopwatch_core::radix2Fft(_spectrumFftBuffer);

        float peakBinMagnitude = 0.0f;
        int peakBinIndex = 0;
        float top1 = 0.0f;
        float top2 = 0.0f;
        float top3 = 0.0f;

        for (std::size_t band = 0; band < Hal::AudioSpectrumFrame::bandCount; ++band) {
            const int startBin = _bandBinEdges[band];
            const int endBin = _bandBinEdges[band + 1];
            float energy = 0.0f;
            float peak = 0.0f;
            int count = 0;
            for (int bin = startBin; bin < endBin; ++bin) {
                const float magnitude = std::abs(_spectrumFftBuffer[bin]) * (2.0f / static_cast<float>(fftSize));
                if (magnitude > peakBinMagnitude) {
                    peakBinMagnitude = magnitude;
                    peakBinIndex = bin;
                }
                peak = std::max(peak, magnitude);
                energy += magnitude * magnitude;
                ++count;
            }

            const float rms = count > 0 ? std::sqrt(energy / static_cast<float>(count)) : 0.0f;
            float raw = rms * 0.48f + peak * 0.52f;
            raw *= 1.12f - 0.22f *
                               (static_cast<float>(band) /
                                static_cast<float>(Hal::AudioSpectrumFrame::bandCount - 1));
            const float floorAlpha = raw < _spectrumNoiseFloor[band] ? 0.45f : 0.004f;
            _spectrumNoiseFloor[band] += (raw - _spectrumNoiseFloor[band]) * floorAlpha;
            raw = std::max(raw - (_spectrumNoiseFloor[band] * 2.20f + 0.0018f), 0.0f);
            _spectrumRawBands[band] = raw;

            if (raw >= top1) {
                top3 = top2;
                top2 = top1;
                top1 = raw;
            } else if (raw >= top2) {
                top3 = top2;
                top2 = raw;
            } else if (raw > top3) {
                top3 = raw;
            }
        }

        const float frameReference = std::max(top1 * 0.80f + top2 * 0.14f + top3 * 0.06f, 0.0015f);
        const float normAlpha = frameReference > _spectrumNormalizationLevel ? 0.44f : 0.16f;
        _spectrumNormalizationLevel += (frameReference - _spectrumNormalizationLevel) * normAlpha;
        _spectrumNormalizationLevel = std::clamp(_spectrumNormalizationLevel, 0.0015f, 1.0f);

        if (peakBinMagnitude > 0.0f) {
            float refinedBin = static_cast<float>(peakBinIndex);
            if (peakBinIndex > 1 && peakBinIndex < (fftSize / 2 - 1)) {
                const float centerPower = peakBinMagnitude * peakBinMagnitude;
                const float leftPower = std::norm(_spectrumFftBuffer[peakBinIndex - 1]);
                const float rightPower = std::norm(_spectrumFftBuffer[peakBinIndex + 1]);
                const float denominator = leftPower - 2.0f * centerPower + rightPower;
                if (std::fabs(denominator) > 1e-9f) {
                    refinedBin += std::clamp(0.5f * (leftPower - rightPower) / denominator, -0.5f, 0.5f);
                }
            }
            frame.peakFrequencyHz = refinedBin * static_cast<float>(sampleRate) / static_cast<float>(fftSize);
        } else {
            frame.peakFrequencyHz = 0.0f;
        }

        for (std::size_t band = 0; band < Hal::AudioSpectrumFrame::bandCount; ++band) {
            const float ratio = _spectrumRawBands[band] / _spectrumNormalizationLevel;
            float normalized = std::clamp(std::pow(ratio, 0.55f), 0.0f, 1.0f);
            if (normalized < 0.035f) {
                normalized = 0.0f;
            }
            const float alpha = normalized > _spectrumSmoothedBands[band] ? 0.82f : 0.40f;
            _spectrumSmoothedBands[band] += (normalized - _spectrumSmoothedBands[band]) * alpha;
            frame.bands[band] = std::clamp(_spectrumSmoothedBands[band], 0.0f, 1.0f);
        }
    }

    TaskHandle_t _playTaskHandle = nullptr;
    std::mutex _playMutex;
    std::mutex _outputMutex;
    std::mutex _codecMutex;
    std::vector<int16_t> _pendingAudio;
    bool _hasPendingRequest = false;
    std::atomic<bool> _ready{false};
    std::atomic<bool> _isPlaying{false};
    std::atomic<int> _volume{80};

    std::mutex _micMutex;
    std::array<int16_t, hopSize> _spectrumPcmHop{};
    std::array<float, fftSize> _spectrumTimeDomain{};
    std::array<float, fftSize> _spectrumWindow{};
    std::array<std::complex<float>, fftSize> _spectrumFftBuffer{};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrumRawBands{};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrumSmoothedBands{};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrumNoiseFloor{};
    std::array<int, Hal::AudioSpectrumFrame::bandCount + 1> _bandBinEdges{};
    std::size_t _spectrumSamplesReady = 0;
    float _spectrumNormalizationLevel = 0.03f;
};

AudioEngine audioEngine;

}  // namespace

bool Hal::audioInit()
{
    mclog::tagInfo(tag, "init shared full-duplex ES8311 audio");
    if (!audioEngine.init()) {
        return false;
    }
    _speakerVolume = getSpeakerVolume(true);
    if (!audioEngine.setVolume(_speakerVolume)) {
        mclog::tagError(tag, "failed to set initial speaker volume");
        return false;
    }
    return true;
}

void Hal::setSpeakerVolume(int volume, bool saveToSettings)
{
    _speakerVolume = std::clamp(volume, 0, 100);
    if (!audioEngine.setVolume(_speakerVolume)) {
        mclog::tagError(tag, "failed to set speaker volume to {}", _speakerVolume);
        return;
    }
    if (saveToSettings) {
        Preferences settings;
        if (settings.begin(SettingsNs.data(), false)) {
            settings.putInt("spk_vol", _speakerVolume);
            settings.end();
        }
    }
}

int Hal::getSpeakerVolume(bool loadFromSettings)
{
    if (loadFromSettings) {
        Preferences settings;
        if (settings.begin(SettingsNs.data(), true)) {
            _speakerVolume = std::clamp(settings.getInt("spk_vol", 80), 0, 100);
            settings.end();
        }
    } else {
        _speakerVolume = audioEngine.getVolume();
    }
    return _speakerVolume;
}

void Hal::audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain)
{
    audioEngine.record(data, durationMs, gain);
}

void Hal::audioPlay(std::vector<int16_t>& data, bool async)
{
    audioEngine.play(data, async);
}

int Hal::getAudioSampleRate()
{
    return AudioEngine::sampleRate;
}

void Hal::updateAudioSpectrum()
{
    audioEngine.updateSpectrum(_audioSpectrum);
}

void Hal::playBootSfx()
{
    if (embedded_assets::bootSfxSize == 0 || embedded_assets::bootSfxSize % sizeof(int16_t) != 0) {
        mclog::tagError(tag, "invalid embedded boot sfx size: {}", embedded_assets::bootSfxSize);
        return;
    }
    const auto* begin = reinterpret_cast<const int16_t*>(embedded_assets::bootSfx);
    std::vector<int16_t> pcm(begin, begin + embedded_assets::bootSfxSize / sizeof(int16_t));
    audioEngine.play(pcm, true);
}
