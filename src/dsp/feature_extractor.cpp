#include "feature_extractor.h"

// Este e o UNICO arquivo que inclui o SDK de DSP (a funcao esta definida no
// cabecalho, sem inline). Nao inclui o modelo embutido.
#include "model-parameters/model_metadata.h"
#include "edge-impulse-sdk/classifier/ei_run_dsp.h"

#include "esp_log.h"

using namespace ei;

static const char* TAG = "DSP";

bool extractFeatures(const float* window, size_t frames, size_t axes,
                     float sampleRateHz, const DspParams& p,
                     float* out, size_t outCount)
{
    signal_t signal;
    if (numpy::signal_from_buffer(const_cast<float*>(window), frames * axes, &signal) != 0)
    {
        ESP_LOGE(TAG, "Falha ao criar o signal");
        return false;
    }

    // A ordem dos campos e a do ei_dsp_config_1135680_2 em model_variables.h
    // (validada: diferenca zero contra o bloco embutido)
    ei_dsp_config_spectral_analysis_t cfg = {
        2,                              // blockId
        4,                              // implementation version
        static_cast<int>(axes),
        p.scaleAxes,
        p.decimation,
        p.filterType.c_str(),
        p.filterCutoff,
        p.filterOrder,
        "FFT",
        p.fftLength,
        p.peaks,
        p.peaksThreshold,
        p.powerEdges.c_str(),
        p.doLog,
        p.fftOverlap,
        1,                              // wavelet level (nao usado)
        "db4",                          // wavelet (nao usado)
        false                           // extra low freq
    };

    matrix_t features(1, outCount, out);
    const int rc = extract_spectral_analysis_features(&signal, &features, &cfg, sampleRateHz);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "extract_spectral_analysis_features retornou %d", rc);
        return false;
    }
    return true;
}
