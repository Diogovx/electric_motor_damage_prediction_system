set(EI_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(EI_SDK  ${EI_ROOT}/edge-impulse-sdk)
set(EI_TFM  ${EI_SDK}/tensorflow/lite/micro)

file(GLOB EI_SRCS
    ${EI_SDK}/dsp/kissfft/*.cpp
    ${EI_SDK}/dsp/dct/*.cpp
    ${EI_SDK}/dsp/memory.cpp
    ${EI_SDK}/porting/espressif/*.c*
    ${EI_SDK}/tensorflow/lite/kernels/*.cc
    ${EI_SDK}/tensorflow/lite/kernels/internal/*.cc
    ${EI_TFM}/*.cc
    ${EI_TFM}/memory_planner/*.cc
    ${EI_SDK}/tensorflow/lite/core/api/*.cc
)

# Esse resolver referencia TODOS os kernels; nao vamos usa-lo
list(FILTER EI_SRCS EXCLUDE REGEX "all_ops_resolver\\.cc$")

# Apenas os kernels necessarios (rede densa + extras baratos)
file(GLOB EI_KERNELS
    ${EI_TFM}/kernels/fully_connected*.cc
    ${EI_TFM}/kernels/softmax*.cc
    ${EI_TFM}/kernels/reshape*.cc
    ${EI_TFM}/kernels/quantize*.cc
    ${EI_TFM}/kernels/dequantize*.cc
    ${EI_TFM}/kernels/kernel_util*.cc
)
list(APPEND EI_SRCS ${EI_KERNELS})

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
    REQUIRES esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-error=maybe-uninitialized
    -Wno-maybe-uninitialized
    -Wno-deprecated-declarations
    -Wno-missing-field-initializers
    -Wno-unused-parameter)
