#include <jni.h>
#include <string>
#include <vector>
#include <cmath>
#include <android/bitmap.h>
#include <android/log.h>
#include "qnn_helper.hpp"
#include <cstdlib>

#define TAG "RetrievalEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

struct EngineContext {
    std::unique_ptr<QnnHelper> imageEncoder;
    std::unique_ptr<QnnHelper> textEncoder;

    EngineContext() {
        imageEncoder = std::make_unique<QnnHelper>();
        textEncoder = std::make_unique<QnnHelper>();
    }
};

extern "C" JNIEXPORT jlong JNICALL
Java_com_quicinc_chatapp_RetrievalEngine_nativeInit(JNIEnv* env, jobject thiz) {
    return reinterpret_cast<jlong>(new EngineContext());
}

extern "C" JNIEXPORT void JNICALL
Java_com_quicinc_chatapp_RetrievalEngine_nativeRelease(JNIEnv* env, jobject thiz, jlong handle) {
    delete reinterpret_cast<EngineContext*>(handle);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_quicinc_chatapp_RetrievalEngine_nativeLoadModels(
        JNIEnv* env,
        jobject thiz,
        jlong handle,
        jstring image_path,
        jstring text_path,
        jstring native_lib_path,
        jstring htp_config_path) {

    EngineContext* ctx =
            reinterpret_cast<EngineContext*>(handle);

    const char* imgPath =
            env->GetStringUTFChars(image_path, nullptr);

    const char* txtPath =
            env->GetStringUTFChars(text_path, nullptr);

    const char* libPath =
            env->GetStringUTFChars(native_lib_path, nullptr);

    const char* configPath =
            env->GetStringUTFChars(htp_config_path, nullptr);

    // Required for HTP backend
    setenv("ADSP_LIBRARY_PATH", libPath, 1);

    LOGI("ADSP_LIBRARY_PATH = %s", libPath);

    std::string htpBackendPath =
            std::string(libPath) + "/libQnnHtp.so";

    std::string cpuBackendPath =
            std::string(libPath) + "/libQnnCpu.so";

    std::string systemLibPath =
            std::string(libPath) + "/libQnnSystem.so";

    auto loadWithFallback =
            [&](std::unique_ptr<QnnHelper>& helper,
                const char* modelPath,
                bool isTextModel) {

                // =========================
                // Try HTP first
                // =========================

                if (helper->init(htpBackendPath, systemLibPath, configPath) &&
                    helper->loadModel(modelPath, isTextModel, configPath)) {

                    LOGI("Loaded %s on HTP", modelPath);

                    return true;
                }

                LOGI("HTP failed for %s", modelPath);

                // =========================
                // Fallback to CPU
                // =========================

                helper = std::make_unique<QnnHelper>();

                if (helper->init(cpuBackendPath, systemLibPath) &&
                    helper->loadModel(modelPath, isTextModel)) {

                    LOGI("Loaded %s on CPU", modelPath);

                    return true;
                }

                LOGE("Failed loading %s", modelPath);

                return false;
            };

    bool imgOk =
            loadWithFallback(
                    ctx->imageEncoder,
                    imgPath,
                    false);

    bool txtOk =
            loadWithFallback(
                    ctx->textEncoder,
                    txtPath,
                    true);

    env->ReleaseStringUTFChars(image_path, imgPath);

    env->ReleaseStringUTFChars(text_path, txtPath);

    env->ReleaseStringUTFChars(native_lib_path, libPath);

    env->ReleaseStringUTFChars(htp_config_path, configPath);

    return imgOk && txtOk;
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_quicinc_chatapp_RetrievalEngine_nativeEncodeImage(JNIEnv* env, jobject thiz, jlong handle, jobject bitmap) {
    EngineContext* ctx = reinterpret_cast<EngineContext*>(handle);

    AndroidBitmapInfo info;
    void* pixels;
    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) return nullptr;
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("Bitmap format must be RGBA_8888");
        return nullptr;
    }
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) < 0) return nullptr;

    const int targetWidth = 224;
    const int targetHeight = 224;
    const int channels = 3;

    // The Python code: np.transpose(image_array, (2, 0, 1))[np.newaxis, :]
    // This results in NCHW format: (1, 3, 224, 224)
    std::vector<float> inputData(1 * channels * targetHeight * targetWidth);

    uint32_t* src = (uint32_t*)pixels;
    for (int y = 0; y < targetHeight; ++y) {
        for (int x = 0; x < targetWidth; ++x) {
            // Check bounds if the input bitmap isn't exactly 224x224
            // (The Java side should ideally resize it first for efficiency)
            if (y < info.height && x < info.width) {
                uint32_t pixel = src[y * info.width + x];

                // Extract channels (RGBA_8888)
                float r = (float)((pixel >> 0) & 0xFF) / 255.0f;
                float g = (float)((pixel >> 8) & 0xFF) / 255.0f;
                float b = (float)((pixel >> 16) & 0xFF) / 255.0f;

                // NCHW layout
                inputData[0 * (targetHeight * targetWidth) + y * targetWidth + x] = r;
                inputData[1 * (targetHeight * targetWidth) + y * targetWidth + x] = g;
                inputData[2 * (targetHeight * targetWidth) + y * targetWidth + x] = b;
            }
        }
    }

    AndroidBitmap_unlockPixels(env, bitmap);

    std::vector<float> outputData;
    if (!ctx->imageEncoder->execute(inputData, outputData)) return nullptr;

    jfloatArray result = env->NewFloatArray(outputData.size());
    env->SetFloatArrayRegion(result, 0, outputData.size(), outputData.data());
    return result;
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_quicinc_chatapp_RetrievalEngine_nativeEncodeText(JNIEnv* env, jobject thiz, jlong handle, jlongArray token_ids) {
    EngineContext* ctx = reinterpret_cast<EngineContext*>(handle);

    jsize len = env->GetArrayLength(token_ids);
    jlong* tokens = env->GetLongArrayElements(token_ids, nullptr);

    std::vector<float> inputData(len);
    for(int i=0; i<len; ++i) inputData[i] = static_cast<float>(tokens[i]);

    env->ReleaseLongArrayElements(token_ids, tokens, JNI_ABORT);

    std::vector<float> outputData;
    if (!ctx->textEncoder->execute(inputData, outputData)) return nullptr;

    jfloatArray result = env->NewFloatArray(outputData.size());
    env->SetFloatArrayRegion(result, 0, outputData.size(), outputData.data());
    return result;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_quicinc_chatapp_RetrievalEngine_nativeComputeSimilarity(JNIEnv* env, jobject thiz, jfloatArray emb1, jfloatArray emb2) {
    if (emb1 == nullptr || emb2 == nullptr) return 0.0f;

    jsize len1 = env->GetArrayLength(emb1);
    jsize len2 = env->GetArrayLength(emb2);
    if (len1 != len2 || len1 == 0) return 0.0f;

    jfloat* e1 = env->GetFloatArrayElements(emb1, nullptr);
    jfloat* e2 = env->GetFloatArrayElements(emb2, nullptr);

    float dot = 0.0f, norm1 = 0.0f, norm2 = 0.0f;
    for (int i = 0; i < len1; ++i) {
        dot += e1[i] * e2[i];
        norm1 += e1[i] * e1[i];
        norm2 += e2[i] * e2[i];
    }

    env->ReleaseFloatArrayElements(emb1, e1, JNI_ABORT);
    env->ReleaseFloatArrayElements(emb2, e2, JNI_ABORT);

    return dot / (sqrt(norm1) * sqrt(norm2) + 1e-8f);
}
