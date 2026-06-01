#include "qnn_helper.hpp"
#include "HTP/QnnHtpDevice.h"
#include "QnnWrapperUtils.hpp"
#include "QnnModel.hpp"

#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <fstream>
#include <vector>
#include <sstream>
#include <string_view>
#include <algorithm>
#include <cstdlib>

#define TAG "QnnHelper"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef Qnn_ErrorHandle_t (*QnnInterfaceGetProvidersFn_t)(
        const QnnInterface_t***,
        uint32_t*);

typedef Qnn_ErrorHandle_t (*QnnSystemInterfaceGetProvidersFn_t)(
        const QnnSystemInterface_t***,
        uint32_t*);

static void qnnLogCallback(const char* fmt, QnnLog_Level_t level, uint64_t timestamp, va_list args) {
    int androidLevel = ANDROID_LOG_INFO;
    switch(level) {
        case QNN_LOG_LEVEL_ERROR: androidLevel = ANDROID_LOG_ERROR; break;
        case QNN_LOG_LEVEL_WARN:  androidLevel = ANDROID_LOG_WARN;  break;
        case QNN_LOG_LEVEL_INFO:  androidLevel = ANDROID_LOG_INFO;  break;
        case QNN_LOG_LEVEL_VERBOSE:
        case QNN_LOG_LEVEL_DEBUG: androidLevel = ANDROID_LOG_DEBUG; break;
        default: break;
    }
    __android_log_vprint(androidLevel, "QnnBackend", fmt, args);
}

QnnHelper::QnnHelper() {
    memset(&m_qnnInterface, 0, sizeof(m_qnnInterface));
    memset(&m_systemInterface, 0, sizeof(m_systemInterface));
}

QnnHelper::~QnnHelper() {

    if (m_context &&
        m_qnnInterface.QNN_INTERFACE_VER_NAME.contextFree) {
        m_qnnInterface.QNN_INTERFACE_VER_NAME.contextFree(m_context, nullptr);
    }

    if (m_device &&
        m_qnnInterface.QNN_INTERFACE_VER_NAME.deviceFree) {
        m_qnnInterface.QNN_INTERFACE_VER_NAME.deviceFree(m_device);
    }

    if (m_backend &&
        m_qnnInterface.QNN_INTERFACE_VER_NAME.backendFree) {
        m_qnnInterface.QNN_INTERFACE_VER_NAME.backendFree(m_backend);
    }

    if (m_logHandle &&
        m_qnnInterface.QNN_INTERFACE_VER_NAME.logFree) {
        m_qnnInterface.QNN_INTERFACE_VER_NAME.logFree(m_logHandle);
    }

    if (m_backendHandle) {
        dlclose(m_backendHandle);
    }

    if (m_systemHandle) {
        dlclose(m_systemHandle);
    }

    if (m_modelDlcHandle) {
        dlclose(m_modelDlcHandle);
    }
}

bool QnnHelper::init(const std::string& backendPath, const std::string& systemLibPath, const std::string& htpConfigPath) {

    // 1. Load Backend
    m_backendHandle = dlopen(backendPath.c_str(), RTLD_NOW);
    if (!m_backendHandle) {
        LOGE("Failed to load backend: %s", dlerror());
        return false;
    }

    auto getProviders =
            (QnnInterfaceGetProvidersFn_t)dlsym(
                    m_backendHandle,
                    "QnnInterface_getProviders");

    if (!getProviders) {
        LOGE("Failed to find QnnInterface_getProviders");
        return false;
    }

    const QnnInterface_t** providers = nullptr;
    uint32_t numProviders = 0;

    if (getProviders(&providers, &numProviders) != QNN_SUCCESS ||
        numProviders == 0) {
        LOGE("Failed to get QNN providers");
        return false;
    }
    m_qnnInterface = *providers[0];

    // 2. Load System Lib (Optional, used for metadata)
    if (!systemLibPath.empty()) {
        m_systemHandle = dlopen(systemLibPath.c_str(), RTLD_NOW);
        if (m_systemHandle) {
            auto getSystemProviders =
                    (QnnSystemInterfaceGetProvidersFn_t)dlsym(
                            m_systemHandle,
                            "QnnSystemInterface_getProviders");
            if (getSystemProviders) {
                const QnnSystemInterface_t** sysProviders = nullptr;
                uint32_t numSysProviders = 0;
                if (getSystemProviders(&sysProviders, &numSysProviders) == QNN_SUCCESS && numSysProviders > 0) {
                    m_systemInterface = *sysProviders[0];
                    LOGI("Loaded QNN System Interface");
                }
            }
        }

        // Also load ModelDlc loader from the same directory
        std::string libDir = systemLibPath.substr(0, systemLibPath.find_last_of('/'));
        std::string dlcLoaderPath = libDir + "/libQnnModelDlc.so";
        m_modelDlcHandle = dlopen(dlcLoaderPath.c_str(), RTLD_NOW);
        if (m_modelDlcHandle) {
            m_composeGraphsFromDlcFn = (QnnModel_composeGraphsFromDlcFn_t)dlsym(m_modelDlcHandle, "QnnModel_composeGraphsFromDlc");
            if (m_composeGraphsFromDlcFn) {
                LOGI("Loaded QnnModel_composeGraphsFromDlc from %s", dlcLoaderPath.c_str());
            } else {
                LOGE("Failed to find QnnModel_composeGraphsFromDlc in %s", dlcLoaderPath.c_str());
            }
        } else {
            LOGE("Failed to load libQnnModelDlc.so from %s: %s", dlcLoaderPath.c_str(), dlerror());
        }
    }

    // 3. Set up logging (Tutorial Step 3)
    if (m_qnnInterface.QNN_INTERFACE_VER_NAME.logCreate) {
        if (m_qnnInterface.QNN_INTERFACE_VER_NAME.logCreate(qnnLogCallback, QNN_LOG_LEVEL_INFO, &m_logHandle) != QNN_SUCCESS) {
            LOGE("logCreate failed");
        }
    }

    // 4. Initialize backend (Tutorial Step 4)
    if (m_qnnInterface.QNN_INTERFACE_VER_NAME.backendCreate(
            m_logHandle, nullptr, &m_backend) != QNN_SUCCESS) {
        LOGE("backendCreate failed");
        return false;
    }

    // 5. Create device (Tutorial Step 6)
    const QnnDevice_Config_t* devConfigs[] = {nullptr, nullptr};
    QnnDevice_Config_t socConfig;
    QnnHtpDevice_CustomConfig_t htpSocConfig;

    if (backendPath.find("libQnnHtp.so") != std::string::npos) {
        htpSocConfig.option = QNN_HTP_DEVICE_CONFIG_OPTION_SOC;
        htpSocConfig.socModel = 69; // Try 8 Elite (69) first
        socConfig.option = QNN_DEVICE_CONFIG_OPTION_CUSTOM;
        socConfig.customConfig = &htpSocConfig;
        devConfigs[0] = &socConfig;
        LOGI("Trying HTP SoC Model 69");
    }

    Qnn_ErrorHandle_t devErr =
            m_qnnInterface.QNN_INTERFACE_VER_NAME.deviceCreate(
                    m_logHandle, devConfigs[0] ? devConfigs : nullptr, &m_device);

    if (devErr != QNN_SUCCESS && htpSocConfig.socModel == 69) {
        LOGI("HTP SoC Model 69 failed, trying 57 (8 Gen 3)");
        htpSocConfig.socModel = 57;
        devErr = m_qnnInterface.QNN_INTERFACE_VER_NAME.deviceCreate(
                    m_logHandle, devConfigs, &m_device);
    }

    if (devErr != QNN_SUCCESS) {
        LOGE("deviceCreate failed: 0x%X", static_cast<uint32_t>(devErr));
        m_device = nullptr;
    } else {
        LOGI("Device created successfully");
    }

    LOGI("QNN initialized successfully following tutorial workflow");
    return true;
}

/* =========================================================
 * Extract graph name from DLC metadata (QNN_MODEL_GRAPH_NAMES)
 * Example metadata string: "graph_m7zrfumf"
 * ========================================================= */
bool QnnHelper::deepCopyQnnTensorInfo(Qnn_Tensor_t* dst, const Qnn_Tensor_t* src) {
    if (nullptr == dst || nullptr == src) return false;
    dst->version = src->version;
    if (src->version == QNN_TENSOR_VERSION_1) {
        dst->v1.id = src->v1.id;
        dst->v1.name = strdup(src->v1.name);
        dst->v1.type = src->v1.type;
        dst->v1.dataFormat = src->v1.dataFormat;
        dst->v1.dataType = src->v1.dataType;
        dst->v1.quantizeParams = src->v1.quantizeParams;
        dst->v1.rank = src->v1.rank;
        dst->v1.dimensions = (uint32_t*)malloc(src->v1.rank * sizeof(uint32_t));
        memcpy(dst->v1.dimensions, src->v1.dimensions, src->v1.rank * sizeof(uint32_t));
    }
    return true;
}

bool QnnHelper::copyMetadataToGraphsInfo(const QnnSystemContext_BinaryInfo_t* binaryInfo) {
    if (nullptr == binaryInfo) return false;

    uint32_t numGraphs = 0;
    QnnSystemContext_GraphInfo_t* graphs = nullptr;

    if (binaryInfo->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_1) {
        numGraphs = binaryInfo->contextBinaryInfoV1.numGraphs;
        graphs = binaryInfo->contextBinaryInfoV1.graphs;
    } else if (binaryInfo->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_2) {
        numGraphs = binaryInfo->contextBinaryInfoV2.numGraphs;
        graphs = binaryInfo->contextBinaryInfoV2.graphs;
    } else if (binaryInfo->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_3) {
        numGraphs = binaryInfo->contextBinaryInfoV3.numGraphs;
        graphs = binaryInfo->contextBinaryInfoV3.graphs;
    }

    if (numGraphs > 0 && graphs) {
        // We only care about the first graph for this app
        if (graphs[0].version == QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_1) {
            m_graphName = graphs[0].graphInfoV1.graphName;
        } else if (graphs[0].version == QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_2) {
            m_graphName = graphs[0].graphInfoV2.graphName;
        } else if (graphs[0].version == QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_3) {
            m_graphName = graphs[0].graphInfoV3.graphName;
        }
        LOGI("Retrieved graph name: %s", m_graphName.c_str());
        return true;
    }
    return false;
}

std::string QnnHelper::extractGraphNameFromDLC(const char* buffer, size_t size) {

    // Avoid copying the entire buffer into a string (OOM risk)
    std::string_view dlcView(buffer, size);

    const std::string key = "QNN_MODEL_GRAPH_NAMES";

    size_t pos = dlcView.find(key);
    if (pos == std::string::npos) {
        LOGI("Graph metadata key not found, using fallback");
        return "graph_m7zrfumf";  // safe fallback
    }

    size_t valueStart = dlcView.find_first_of("=:", pos);
    if (valueStart == std::string::npos) {
        return "graph_m7zrfumf";
    }

    valueStart++;

    size_t valueEnd = dlcView.find_first_of("\n\r", valueStart);
    if (valueEnd == std::string::npos) {
        valueEnd = dlcView.size();
    }

    std::string_view graphNameView = dlcView.substr(valueStart, valueEnd - valueStart);

    // trim spaces
    size_t first = graphNameView.find_first_not_of(" \t");
    if (std::string_view::npos == first) return "graph_m7zrfumf";
    size_t last = graphNameView.find_last_not_of(" \t");
    graphNameView = graphNameView.substr(first, (last - first + 1));

    std::string graphName(graphNameView);
    LOGI("Extracted graph name: %s", graphName.c_str());

    return graphName.empty() ? "graph_m7zrfumf" : graphName;
}

bool QnnHelper::loadModel(
        const std::string& modelPath,
        bool isTextModel,
        const std::string& htpConfigPath) {

    // 1. Create Context (Tutorial Step 8)
    if (m_qnnInterface.QNN_INTERFACE_VER_NAME.contextCreate(
            m_backend, m_device, nullptr, &m_context) != QNN_SUCCESS) {
        LOGE("contextCreate failed");
        return false;
    }

    // 2. Compose Graphs from DLC (Modern QNN DLC loading)
    if (!m_composeGraphsFromDlcFn) {
        LOGE("m_composeGraphsFromDlcFn is null, cannot load DLC");
        return false;
    }

    qnn_wrapper_api::GraphInfo_t** graphsInfo = nullptr;
    uint32_t numGraphs = 0;

    auto modelStatus = m_composeGraphsFromDlcFn(
            m_backend,
            m_qnnInterface.QNN_INTERFACE_VER_NAME,
            m_context,
            nullptr, // graphsConfigInfo
            modelPath.c_str(),
            0, // numGraphsConfigInfo
            &graphsInfo,
            &numGraphs,
            false, // debug
            qnnLogCallback,
            QNN_LOG_LEVEL_INFO);

    if (modelStatus != qnn_wrapper_api::MODEL_NO_ERROR || numGraphs == 0) {
        LOGE("QnnModel_composeGraphsFromDlc failed: %d", (int)modelStatus);
        return false;
    }

    // 3. Finalize Graphs (Tutorial Step 11)
    m_graph = graphsInfo[0]->graph;
    m_graphName = graphsInfo[0]->graphName;

    if (m_qnnInterface.QNN_INTERFACE_VER_NAME.graphFinalize(m_graph, nullptr, nullptr) != QNN_SUCCESS) {
        LOGE("graphFinalize failed for %s", m_graphName.c_str());
        return false;
    }

    LOGI("Loaded and finalized graph: %s from %s", m_graphName.c_str(), modelPath.c_str());

    // Extract dynamic names and IDs from composed graph metadata
    if (graphsInfo[0]->numInputTensors > 0) {
        m_inputName = QNN_TENSOR_GET_NAME(graphsInfo[0]->inputTensors[0]);
        m_inputId = QNN_TENSOR_GET_ID(graphsInfo[0]->inputTensors[0]);
    }
    if (graphsInfo[0]->numOutputTensors > 0) {
        m_outputName = QNN_TENSOR_GET_NAME(graphsInfo[0]->outputTensors[0]);
        m_outputId = QNN_TENSOR_GET_ID(graphsInfo[0]->outputTensors[0]);
    }

    // Model config overrides for known models
    if (isTextModel) {
        if (m_inputName.empty()) m_inputName = "text";
        if (m_outputName.empty()) m_outputName = "text_embeds";
        m_inputDataType = QNN_DATATYPE_INT_32;
        m_outputSize = 512;
        m_isTextModel = true;
    } else {
        if (m_inputName.empty()) m_inputName = "image";
        if (m_outputName.empty()) m_outputName = "image_embeds";
        m_inputDataType = QNN_DATATYPE_FLOAT_32;
        m_outputSize = 512;
        m_isTextModel = false;
    }

    // Free the wrapper metadata (not the graph itself)
    qnn_wrapper_api::freeGraphsInfo(&graphsInfo, numGraphs);

    return true;
}

bool QnnHelper::execute(
        const std::vector<float>& inputData,
        std::vector<float>& outputData) {

    if (!m_graph) {
        LOGE("Graph is null");
        return false;
    }

    Qnn_Tensor_t inputTensor = QNN_TENSOR_INIT;
    inputTensor.version = QNN_TENSOR_VERSION_1;
    inputTensor.v1.id = m_inputId;
    inputTensor.v1.name = m_inputName.c_str();
    inputTensor.v1.type = QNN_TENSOR_TYPE_APP_WRITE;
    inputTensor.v1.dataFormat = QNN_TENSOR_DATA_FORMAT_FLAT_BUFFER;
    inputTensor.v1.memType = QNN_TENSORMEMTYPE_RAW;

    std::vector<uint32_t> inputDims;
    size_t inputSizeInBytes = 0;
    void* alignedInput = nullptr;

    if (m_isTextModel) {
        inputTensor.v1.dataType = QNN_DATATYPE_INT_32;
        inputTensor.v1.rank = 2;
        inputDims = {1, 77};
        inputTensor.v1.dimensions = inputDims.data();
        inputSizeInBytes = 1 * 77 * sizeof(int32_t);

        if (posix_memalign(&alignedInput, 64, inputSizeInBytes) != 0) return false;
        int32_t* intPtr = static_cast<int32_t*>(alignedInput);
        for (size_t i = 0; i < 77; ++i) {
            intPtr[i] = (i < inputData.size()) ? static_cast<int32_t>(inputData[i]) : 0;
        }
    } else {
        inputTensor.v1.dataType = QNN_DATATYPE_FLOAT_32;
        inputTensor.v1.rank = 4;
        inputDims = {1, 3, 224, 224};
        inputTensor.v1.dimensions = inputDims.data();
        inputSizeInBytes = 1 * 3 * 224 * 224 * sizeof(float);

        if (posix_memalign(&alignedInput, 64, inputSizeInBytes) != 0) return false;
        memcpy(alignedInput, inputData.data(), std::min(inputSizeInBytes, inputData.size() * sizeof(float)));
    }

    inputTensor.v1.clientBuf.data = alignedInput;
    inputTensor.v1.clientBuf.dataSize = inputSizeInBytes;

    outputData.resize(m_outputSize);
    void* alignedOutput = nullptr;
    size_t outputSizeInBytes = m_outputSize * sizeof(float);
    if (posix_memalign(&alignedOutput, 64, outputSizeInBytes) != 0) {
        free(alignedInput);
        return false;
    }

    Qnn_Tensor_t outputTensor = QNN_TENSOR_INIT;
    outputTensor.version = QNN_TENSOR_VERSION_1;
    outputTensor.v1.id = m_outputId;
    outputTensor.v1.name = m_outputName.c_str();
    outputTensor.v1.type = QNN_TENSOR_TYPE_APP_READ;
    outputTensor.v1.dataType = QNN_DATATYPE_FLOAT_32;
    outputTensor.v1.dataFormat = QNN_TENSOR_DATA_FORMAT_FLAT_BUFFER;
    outputTensor.v1.memType = QNN_TENSORMEMTYPE_RAW;
    outputTensor.v1.rank = 2;

    std::vector<uint32_t> outputDims = {1, m_outputSize};
    outputTensor.v1.dimensions = outputDims.data();

    outputTensor.v1.clientBuf.data = alignedOutput;
    outputTensor.v1.clientBuf.dataSize = outputSizeInBytes;

    Qnn_ErrorHandle_t execErr =
            m_qnnInterface.QNN_INTERFACE_VER_NAME.graphExecute(
                    m_graph,
                    &inputTensor,
                    1,
                    &outputTensor,
                    1,
                    nullptr,
                    nullptr);

    if (execErr == QNN_SUCCESS) {
        memcpy(outputData.data(), alignedOutput, outputSizeInBytes);
    } else {
        LOGE("graphExecute failed: 0x%X", static_cast<uint32_t>(execErr));
    }

    free(alignedInput);
    free(alignedOutput);

    return (execErr == QNN_SUCCESS);
}
