cmake_minimum_required(VERSION 3.22.1)

project("retrieval_app")

# Download nlohmann/json single-header at configure time if not already present
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/json.hpp)
    file(DOWNLOAD
        https://raw.githubusercontent.com/nlohmann/json/55f93686c01528224f448c19128836e7df245f72/single_include/nlohmann/json.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/json.hpp
    )
endif()

# QNN include path
set(QNN_INCLUDE_PATH "${QNN_SDK_ROOT_PATH}/include/QNN")
set(QNN_WRAPPER_DIR "${QNN_SDK_ROOT_PATH}/share/QNN/converter/jni")
include_directories(${QNN_INCLUDE_PATH})
include_directories(${QNN_WRAPPER_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(${CMAKE_PROJECT_NAME} SHARED
        retrieval_engine.cpp
        qnn_image_encoder.cpp
        qnn_text_encoder.cpp
        cosine_similarity.cpp
        qnn_helper.cpp
        ${QNN_WRAPPER_DIR}/QnnModel.cpp
        ${QNN_WRAPPER_DIR}/QnnWrapperUtils.cpp
        ${QNN_WRAPPER_DIR}/linux/QnnModelPal.cpp)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        jnigraphics)
