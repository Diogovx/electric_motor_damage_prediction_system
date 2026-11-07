set(EI_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(EI_SDK  ${EI_ROOT}/edge-impulse-sdk)

file(GLOB EI_SRCS
    ${EI_ROOT}/tflite-model/*.cpp
    ${EI_SDK}/dsp/kissfft/*.cpp
    ${EI_SDK}/dsp/dct/*.cpp
    ${EI_SDK}/dsp/memory.cpp
    ${EI_SDK}/porting/espressif/*.c*
    ${EI_SDK}/tensorflow/lite/kernels/*.cc
    ${EI_SDK}/tensorflow/lite/kernels/internal/*.cc
    ${EI_SDK}/tensorflow/lite/micro/*.cc
    ${EI_SDK}/tensorflow/lite/micro/kernels/*.cc
    ${EI_SDK}/tensorflow/lite/micro/memory_planner/*.cc
    ${EI_SDK}/tensorflow/lite/core/api/*.cc
)

set(EI_ESP_DSP ${EI_SDK}/porting/espressif/esp-dsp/modules)
set(EI_ESP_DSP_INCS
    ${EI_ESP_DSP}/common/include
    ${EI_ESP_DSP}/fft/include
    ${EI_ESP_DSP}/dotprod/include
    ${EI_ESP_DSP}/support/include
    ${EI_ESP_DSP}/windows/include
    ${EI_ESP_DSP}/math/include
)

idf_component_register(
    SRCS ${EI_SRCS}
    INCLUDE_DIRS
        ${EI_ROOT}
        ${EI_ROOT}/model-parameters
        ${EI_ROOT}/tflite-model
        ${EI_SDK}
        ${EI_SDK}/third_party
        ${EI_SDK}/third_party/flatbuffers/include
        ${EI_SDK}/third_party/gemmlowp
        ${EI_SDK}/third_party/ruy
        ${EI_ESP_DSP_INCS}
    REQUIRES esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-deprecated-declarations)