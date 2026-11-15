#include "runtime_config.h"

#include <stdlib.h>

namespace
{
    bool isPowerOfTwo(int v) { return v > 0 && (v & (v - 1)) == 0; }
}

bool parseRuntimeConfig(JsonObjectConst cfg, RuntimeConfig& out, std::string& err)
{
    out = RuntimeConfig();

    out.version = cfg["version"] | 0;
    out.motor = cfg["motor"] | "";

    // ---- sensores ----
    JsonArrayConst sensors = cfg["sensors"].as<JsonArrayConst>();
    if (sensors.isNull() || sensors.size() == 0 || sensors.size() > MAX_SENSORS)
    {
        err = "campo 'sensors' ausente ou com tamanho invalido";
        return false;
    }

    for (JsonObjectConst s : sensors)
    {
        const char* name = s["type"] | "";
        SensorSpec spec;
        if (!sensorTypeFromName(name, spec.type))
        {
            err = std::string("sensor desconhecido: ") + name;
            return false;
        }
        spec.channels = s["channels"] | 0;
        spec.rangeG = s["range_g"] | 16;
        if (spec.channels != sensorChannels(spec.type))
        {
            err = std::string("numero de canais incompativel para ") + name;
            return false;
        }
        out.axes += static_cast<size_t>(spec.channels);
        out.sensors.push_back(spec);
    }

    if (out.axes < 1 || out.axes > MAX_AXES)
    {
        err = "numero total de canais invalido";
        return false;
    }

    // ---- janela ----
    out.samplingHz = cfg["window"]["sampling_hz"] | 100;
    out.windowMs = cfg["window"]["size_ms"] | 2560;
    out.strideMs = cfg["window"]["stride_ms"] | 128;

    if (out.samplingHz < 10 || out.samplingHz > 100 || 1000 % out.samplingHz != 0 ||
        (1000 / out.samplingHz) % 10 != 0)
    {
        err = "sampling_hz invalido (periodo multiplo de 10 ms: 100, 50, 25, 20, 10)";
        return false;
    }
    if ((out.windowMs * out.samplingHz) % 1000 != 0)
    {
        err = "size_ms nao corresponde a um numero inteiro de amostras";
        return false;
    }
    out.windowFrames = static_cast<size_t>(out.windowMs) * out.samplingHz / 1000;
    out.strideFrames = static_cast<size_t>(out.strideMs) * out.samplingHz / 1000;   // arredonda para baixo, como o Studio
    if (out.windowFrames < 16 || out.windowFrames > MAX_WINDOW_FRAMES ||
        out.strideFrames < 1 || out.strideFrames > out.windowFrames)
    {
        err = "janela ou avanco fora dos limites";
        return false;
    }

    // ---- DSP ----
    JsonObjectConst d = cfg["dsp"].as<JsonObjectConst>();
    out.dsp.fftLength = d["fft_length"] | 16;
    out.dsp.peaks = d["peaks"] | 3;
    out.dsp.peaksThreshold = d["peaks_threshold"] | 0.1f;
    out.dsp.powerEdges = d["power_edges"] | "0.1, 0.5, 1.0, 2.0, 5.0";
    out.dsp.doLog = d["do_log"] | true;
    out.dsp.fftOverlap = d["fft_overlap"] | true;
    out.dsp.filterType = d["filter_type"] | "none";
    out.dsp.filterCutoff = d["filter_cutoff"] | 3.0f;
    out.dsp.filterOrder = d["filter_order"] | 6;
    out.dsp.scaleAxes = d["scale_axes"] | 1.0f;
    out.dsp.decimation = d["decimation"] | 1;

    if (!isPowerOfTwo(out.dsp.fftLength) || out.dsp.fftLength < 16 || out.dsp.fftLength > 1024)
    {
        err = "fft_length deve ser potencia de 2 entre 16 e 1024";
        return false;
    }
    if (out.dsp.peaks < 1 || out.dsp.peaks > 8)
    {
        err = "peaks invalido";
        return false;
    }

    // ---- modelo ----
    JsonObjectConst m = cfg["model"].as<JsonObjectConst>();
    out.arenaBytes = m["arena_bytes"] | 8192;
    out.features = m["features"] | 52;
    out.modelSize = m["size"] | 0;
    const char* crc = m["crc32"] | "";
    out.modelCrc32 = crc[0] ? static_cast<uint32_t>(strtoul(crc, nullptr, 0)) : 0;

    JsonArrayConst labels = m["labels"].as<JsonArrayConst>();
    for (JsonVariantConst l : labels)
    {
        const char* s = l | "";
        if (!s[0])
        {
            err = "label vazio";
            return false;
        }
        out.labels.emplace_back(s);
    }

    if (out.labels.size() < 2 || out.labels.size() > MAX_LABELS)
    {
        err = "numero de labels invalido";
        return false;
    }
    if (out.arenaBytes < 512 || out.arenaBytes > 65536)
    {
        err = "arena_bytes fora de 512..65536";
        return false;
    }
    if (out.features < 1 || out.features > MAX_FEATURES)
    {
        err = "features fora do limite";
        return false;
    }

    return true;
}
