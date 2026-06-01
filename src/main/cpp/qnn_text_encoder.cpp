#include "qnn_helper.hpp"
#include <vector>

// This file would contain text-specific preprocessing (tokenization) and QNN graph execution logic
bool encodeTextQnn(QnnHelper* helper, const std::vector<long>& tokenIds, std::vector<float>& embedding) {
    std::vector<float> floatTokens;
    for(long id : tokenIds) floatTokens.push_back(static_cast<float>(id));
    return helper->execute(floatTokens, embedding);
}
