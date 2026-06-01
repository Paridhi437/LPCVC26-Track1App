#include "qnn_helper.hpp"
#include <vector>

// This file would contain image-specific preprocessing and QNN graph execution logic
bool encodeImageQnn(QnnHelper* helper, const std::vector<float>& imageData, std::vector<float>& embedding) {
    return helper->execute(imageData, embedding);
}
