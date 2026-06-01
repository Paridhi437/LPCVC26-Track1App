package com.quicinc.chatapp;

import android.graphics.Bitmap;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

public class RetrievalEngine {
    private static final String TAG = "RetrievalEngine";
    private static final String CACHE_FILE_NAME = "text_embeddings.bin";
    
    static {
        System.loadLibrary("retrieval_app");
    }

    private long nativeHandle;

    public RetrievalEngine() {
        nativeHandle = nativeInit();
    }

    public interface ProgressListener {
        void onProgress(int current, int total);
    }

    public void release() {
        nativeRelease(nativeHandle);
        nativeHandle = 0;
    }

    public boolean loadModels(String imageModelPath, String textModelPath, String nativeLibPath, String htpConfigPath) {
        return nativeLoadModels(nativeHandle, imageModelPath, textModelPath, nativeLibPath, htpConfigPath);
    }

    public float[] encodeImage(Bitmap bitmap) {
        return nativeEncodeImage(nativeHandle, bitmap);
    }

    public float[] encodeText(long[] tokenIds) {
        return nativeEncodeText(nativeHandle, tokenIds);
    }

    /**
     * Retrieves text embeddings from cache if available, otherwise calculates and saves them.
     */
    public List<float[]> getOrCreateTextEmbeddings(File cacheDir, List<long[]> allTokens, ProgressListener listener) {
        File cacheFile = new File(cacheDir, CACHE_FILE_NAME);
        int embeddingSize = 512; // Standard for this app's model

        if (cacheFile.exists()) {
            Log.i(TAG, "Loading text embeddings from cache: " + cacheFile.getAbsolutePath());
            try (FileInputStream fis = new FileInputStream(cacheFile)) {
                byte[] data = new byte[(int) cacheFile.length()];
                fis.read(data);
                ByteBuffer buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
                
                int numEmbeddings = data.length / (embeddingSize * 4);
                List<float[]> embeddings = new ArrayList<>(numEmbeddings);
                for (int i = 0; i < numEmbeddings; i++) {
                    float[] emb = new float[embeddingSize];
                    for (int j = 0; j < embeddingSize; j++) {
                        emb[j] = buffer.getFloat();
                    }
                    embeddings.add(emb);
                    if (listener != null) {
                        listener.onProgress(i + 1, numEmbeddings);
                    }
                }
                return embeddings;
            } catch (Exception e) {
                Log.e(TAG, "Failed to load cached embeddings, recalculating...", e);
            }
        }

        Log.i(TAG, "Calculating text embeddings and saving to cache...");
        List<float[]> embeddings = new ArrayList<>(allTokens.size());
        
        // Calculate all embeddings
        for (int i = 0; i < allTokens.size(); i++) {
            float[] emb = encodeText(allTokens.get(i));
            if (emb != null) {
                embeddings.add(emb);
            }
            if (listener != null) {
                listener.onProgress(i + 1, allTokens.size());
            }
        }

        // Save to cache
        try (FileOutputStream fos = new FileOutputStream(cacheFile)) {
            ByteBuffer buffer = ByteBuffer.allocate(embeddings.size() * embeddingSize * 4).order(ByteOrder.LITTLE_ENDIAN);
            for (float[] emb : embeddings) {
                for (float val : emb) {
                    buffer.putFloat(val);
                }
            }
            fos.write(buffer.array());
            Log.i(TAG, "Saved " + embeddings.size() + " embeddings to cache");
        } catch (Exception e) {
            Log.e(TAG, "Failed to save embeddings to cache", e);
        }

        return embeddings;
    }

    public float computeSimilarity(float[] emb1, float[] emb2) {
        return nativeComputeSimilarity(emb1, emb2);
    }

    private native long nativeInit();
    private native void nativeRelease(long handle);
    private native boolean nativeLoadModels(long handle, String imageModelPath, String textModelPath, String nativeLibPath, String htpConfigPath);
    private native float[] nativeEncodeImage(long handle, Bitmap bitmap);
    private native float[] nativeEncodeText(long handle, long[] tokenIds);
    private native float nativeComputeSimilarity(float[] emb1, float[] emb2);
}
