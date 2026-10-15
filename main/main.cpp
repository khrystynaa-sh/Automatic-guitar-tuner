#include <cstdio>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"

constexpr adc_channel_t ADC_CHANNEL = ADC_CHANNEL_0;
constexpr uint32_t SAMPLE_RATE = 8000;
constexpr int N = 2048;
constexpr uint32_t READ_CHUNK_BYTES = 1024;

constexpr float MIN_FREQ = 70.0f;
constexpr float MAX_FREQ = 400.0f;
constexpr float YIN_THRESHOLD = 0.15f;
constexpr float IN_TUNE_CENTS = 3.0f;

constexpr int TAU_MIN = SAMPLE_RATE / 400;
constexpr int TAU_MAX = SAMPLE_RATE / 70;

struct GuitarString {
    const char *name;
    int number;
    float freq;
};

const GuitarString strings[] = {
    {"E2", 6, 82.41f},
    {"A2", 5, 110.00f},
    {"D3", 4, 146.83f},
    {"G3", 3, 196.00f},
    {"B3", 2, 246.94f},
    {"E4", 1, 329.63f},
};
constexpr int STRING_COUNT = sizeof(strings) / sizeof(strings[0]);

adc_continuous_handle_t adc;
uint8_t raw[READ_CHUNK_BYTES];
float samples[N];
float yin[TAU_MAX + 2];
float sound_threshold = 15.0f;

void adc_init(void)
{
    adc_continuous_handle_cfg_t handle_cfg = {};
    handle_cfg.max_store_buf_size = 4 * READ_CHUNK_BYTES;
    handle_cfg.conv_frame_size = READ_CHUNK_BYTES;
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &adc));

    adc_digi_pattern_config_t pattern = {};
    pattern.atten = ADC_ATTEN_DB_12;
    pattern.channel = ADC_CHANNEL;
    pattern.unit = ADC_UNIT_1;
    pattern.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

    adc_continuous_config_t cfg = {};
    cfg.pattern_num = 1;
    cfg.adc_pattern = &pattern;
    cfg.sample_freq_hz = SAMPLE_RATE;
    cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    ESP_ERROR_CHECK(adc_continuous_config(adc, &cfg));
}

float capture_block(void)
{
    int n = 0;
    double sum = 0;

    ESP_ERROR_CHECK(adc_continuous_start(adc));
    while (n < N) {
        uint32_t got = 0;
        if (adc_continuous_read(adc, raw, READ_CHUNK_BYTES, &got, 1000) != ESP_OK) {
            continue;
        }
        for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= got && n < N; i += SOC_ADC_DIGI_RESULT_BYTES) {
            auto *p = reinterpret_cast<adc_digi_output_data_t *>(&raw[i]);
            if (p->type2.channel != ADC_CHANNEL) {
                continue;
            }
            samples[n] = static_cast<float>(p->type2.data);
            sum += samples[n];
            n++;
        }
    }
    ESP_ERROR_CHECK(adc_continuous_stop(adc));

    float mean = sum / N;
    double sum_sq = 0;
    for (int i = 0; i < N; i++) {
        samples[i] -= mean;
        sum_sq += samples[i] * samples[i];
    }
    return sqrtf(sum_sq / N);
}

float detect_pitch(void)
{
    const int W = N - TAU_MAX;

    for (int tau = 1; tau <= TAU_MAX; tau++) {
        float d = 0;
        for (int j = 0; j < W; j++) {
            float diff = samples[j] - samples[j + tau];
            d += diff * diff;
        }
        yin[tau] = d;
    }

    yin[0] = 1.0f;
    float running = 0;
    for (int tau = 1; tau <= TAU_MAX; tau++) {
        running += yin[tau];
        yin[tau] = (running > 0) ? yin[tau] * tau / running : 1.0f;
    }

    int tau_est = -1;
    for (int tau = TAU_MIN; tau <= TAU_MAX; tau++) {
        if (yin[tau] < YIN_THRESHOLD) {
            while (tau + 1 <= TAU_MAX && yin[tau + 1] < yin[tau]) {
                tau++;
            }
            tau_est = tau;
            break;
        }
    }
    if (tau_est < 0) {
        return 0;
    }

    float better = static_cast<float>(tau_est);
    if (tau_est > 1 && tau_est < TAU_MAX) {
        float s0 = yin[tau_est - 1], s1 = yin[tau_est], s2 = yin[tau_est + 1];
        float denom = s0 + s2 - 2.0f * s1;
        if (fabsf(denom) > 1e-9f) {
            better = tau_est + (s0 - s2) / (2.0f * denom);
        }
    }
    return SAMPLE_RATE / better;
}

void print_result(float freq)
{
    int best = 0;
    float cents = 1e9f;
    for (int i = 0; i < STRING_COUNT; i++) {
        float c = 1200.0f * log2f(freq / strings[i].freq);
        if (fabsf(c) < fabsf(cents)) {
            cents = c;
            best = i;
        }
    }

    const GuitarString &s = strings[best];
    float diff_hz = freq - s.freq;

    printf("String %d (%s) | %.2f Hz | target %.2f Hz | %+.1f cents | ",
           s.number, s.name, freq, s.freq, cents);

    if (fabsf(cents) <= IN_TUNE_CENTS) {
        printf("IN TUNE\n");
        return;
    }

    const char *direction = (cents < 0) ? "TIGHTEN (pitch up)" : "LOOSEN (pitch down)";
    const char *amount = (fabsf(cents) > 50) ? "a lot" : (fabsf(cents) > 15) ? "a little" : "slightly";
    printf("%s %s, %.2f Hz\n", direction, amount, fabsf(diff_hz));
}

void tuner_task(void *)
{
    float noise = 0;
    for (int i = 0; i < 4; i++) {
        noise += capture_block();
    }
    sound_threshold = fmaxf(15.0f, (noise / 4) * 3.0f);
    printf("Guitar tuner ready. Play a string.\n");

    while (true) {
        if (capture_block() < sound_threshold) {
            continue;
        }
        float freq = detect_pitch();
        if (freq >= MIN_FREQ && freq <= MAX_FREQ) {
            print_result(freq);
        }
    }
}

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    adc_init();
    xTaskCreate(tuner_task, "tuner", 8192, nullptr, 5, nullptr);
}
