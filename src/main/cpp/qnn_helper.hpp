#pragma once

#include <string>
#include <vector>

#include "QnnInterface.h"
#include "QnnTypes.h"
#include "System/QnnSystemInterface.h"
#include "QnnWrapperUtils.hpp"

typedef qnn_wrapper_api::ModelError_t (*QnnModel_composeGraphsFromDlcFn_t)(
    Qnn_BackendHandle_t,
    QNN_INTERFACE_VER_TYPE,
    Qnn_ContextHandle_t,
    const qnn_wrapper_api::GraphConfigInfo_t **,
    const char *,
    const uint32_t,
    qnn_wrapper_api::GraphInfo_t ***,
    uint32_t *,
    bool,
    QnnLog_Callback_t,
    QnnLog_Level_t);

// Using definitions from QnnWrapperUtils.hpp
using qnn_wrapper_api::GraphInfo_t;
using qnn_wrapper_api::GraphInfoPtr_t;

class QnnHelper {
public:
    QnnHelper();
    ~QnnHelper();

    bool init(const std::string& backendPath, const std::string& systemLibPath = "", const std::string& htpConfigPath = "");

    bool loadModel(
            const std::string& modelPath,
            bool isTextModel,
            const std::string& htpConfigPath = "");

    bool execute(
            const std::vector<float>& inputData,
            std::vector<float>& outputData);

private:
    bool copyMetadataToGraphsInfo(const QnnSystemContext_BinaryInfo_t* binaryInfo);
    bool deepCopyQnnTensorInfo(Qnn_Tensor_t* dst, const Qnn_Tensor_t* src);

    std::string extractGraphNameFromDLC(const char* buffer, size_t size);

    void* m_backendHandle = nullptr;
    void* m_systemHandle = nullptr;
    void* m_modelDlcHandle = nullptr;

    QnnInterface_t m_qnnInterface{};
    QnnSystemInterface_t m_systemInterface{};
    QnnModel_composeGraphsFromDlcFn_t m_composeGraphsFromDlcFn = nullptr;

    Qnn_LogHandle_t m_logHandle = nullptr;
    Qnn_BackendHandle_t m_backend = nullptr;
    Qnn_DeviceHandle_t m_device = nullptr;
    Qnn_ContextHandle_t m_context = nullptr;
    Qnn_GraphHandle_t m_graph = nullptr;

    std::string m_graphName;
    std::string m_inputName;
    std::string m_outputName;
    uint32_t m_inputId = 0;
    uint32_t m_outputId = 0;
    uint32_t m_outputSize = 0;
    Qnn_DataType_t m_inputDataType = QNN_DATATYPE_FLOAT_32;
    bool m_isTextModel = false;
};
