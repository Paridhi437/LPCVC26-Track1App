package com.quicinc.chatapp;

import android.Manifest;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;
import android.media.ExifInterface;
import android.net.Uri;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.content.FileProvider;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import org.json.JSONArray;
import org.json.JSONObject;

import com.qualcomm.qti.platformvalidator.PlatformValidator;
import com.qualcomm.qti.platformvalidator.PlatformValidatorUtil;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class RetrievalActivity extends AppCompatActivity {
    private static final String TAG = "RetrievalActivity";
    private RetrievalEngine engine;
    private ImageView ivSelectedImage;
    private Button btnRun;
    private View loadingOverlay;
    private android.widget.ProgressBar progressBar;
    private TextView tvLoadingStatus;
    private TextView tvProgressPercent;
    private Bitmap selectedBitmap;
    private Uri cameraImageUri;
    private ResultAdapter adapter;
    private List<ResultItem> resultsList = new ArrayList<>();
    private List<DatasetItem> dataset = new ArrayList<>();
    private List<float[]> mTextEmbeddings = new ArrayList<>();

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == R.id.action_info) {
            startActivity(new Intent(this, InfoActivity.class));
            return true;
        } else if (item.getItemId() == R.id.action_dashboard) {
            startActivity(new Intent(this, DashboardActivity.class));
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    private static class DatasetItem {
        String text;
        long[] tokens;
        int index;
        DatasetItem(String text, long[] tokens, int index) {
            this.text = text;
            this.tokens = tokens;
            this.index = index;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_retrieval);

        engine = new RetrievalEngine();
        loadingOverlay = findViewById(R.id.loading_overlay);
        progressBar = findViewById(R.id.pb_loading);
        tvLoadingStatus = findViewById(R.id.tv_loading_status);
        tvProgressPercent = findViewById(R.id.tv_progress_percent);

        // Check if DSP/HTP is available
        PlatformValidator pv = new PlatformValidator(PlatformValidatorUtil.Runtime.DSP);
        boolean isDspAvailable = pv.isRuntimeAvailable(getApplication());
        Log.i(TAG, "DSP/HTP Runtime Available: " + isDspAvailable);

        ivSelectedImage = findViewById(R.id.iv_selected_image);
        btnRun = findViewById(R.id.btn_run_model);
        RecyclerView rvResults = findViewById(R.id.rv_results);
        rvResults.setLayoutManager(new LinearLayoutManager(this));
        adapter = new ResultAdapter(resultsList);
        rvResults.setAdapter(adapter);

        Button btnPick = findViewById(R.id.btn_pick_image);
        ActivityResultLauncher<String> pickerLauncher = registerForActivityResult(
                new ActivityResultContracts.GetContent(),
                uri -> {
                    if (uri != null) {
                        prepareImage(uri);
                    }
                }
        );

        ActivityResultLauncher<Uri> cameraLauncher = registerForActivityResult(
                new ActivityResultContracts.TakePicture(),
                success -> {
                    if (success && cameraImageUri != null) {
                        prepareImage(cameraImageUri);
                    }
                }
        );

        ActivityResultLauncher<String> permissionLauncher = registerForActivityResult(
                new ActivityResultContracts.RequestPermission(),
                isGranted -> {
                    if (isGranted) {
                        launchCamera(cameraLauncher);
                    } else {
                        Toast.makeText(this, "Camera permission is required to take photos", Toast.LENGTH_SHORT).show();
                    }
                }
        );

        btnPick.setOnClickListener(v -> pickerLauncher.launch("image/*"));
        
        Button btnTakePhoto = findViewById(R.id.btn_take_photo);
        btnTakePhoto.setOnClickListener(v -> {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
                launchCamera(cameraLauncher);
            } else {
                permissionLauncher.launch(Manifest.permission.CAMERA);
            }
        });

        btnRun.setOnClickListener(v -> runModel());

        // Start initialization in background
        initializeInBackground();
    }

    private void launchCamera(ActivityResultLauncher<Uri> launcher) {
        try {
            File photoFile = new File(getExternalFilesDir(Environment.DIRECTORY_PICTURES), "captured_image.jpg");
            cameraImageUri = FileProvider.getUriForFile(this, getPackageName() + ".fileprovider", photoFile);
            launcher.launch(cameraImageUri);
        } catch (Exception e) {
            Log.e(TAG, "Failed to create photo file", e);
            Toast.makeText(this, "Failed to open camera", Toast.LENGTH_SHORT).show();
        }
    }

    private void initializeInBackground() {
        loadingOverlay.setVisibility(View.VISIBLE);
        new Thread(() -> {
            // Stage 1: Initializing app (copying models, loading engine)
            runOnUiThread(() -> {
                tvLoadingStatus.setText(R.string.starting_app);
                progressBar.setIndeterminate(true);
            });

            String imageModelPath = copyAssetToInternalStorage("models/image_encoder.dlc");
            String textModelPath = copyAssetToInternalStorage("models/text_encoder.dlc");
            String htpConfigPath = copyAssetToInternalStorage("htp_config/qualcomm-snapdragon-8-gen2.json");

            if (imageModelPath != null && textModelPath != null && htpConfigPath != null) {
                String nativeLibPath = getApplicationInfo().nativeLibraryDir;
                engine.loadModels(imageModelPath, textModelPath, nativeLibPath, htpConfigPath);
            }

            // Stage 2: Loading dataset and calculating embeddings
            loadDataset();

            runOnUiThread(() -> {
                tvLoadingStatus.setText(R.string.generating_embeddings);
                progressBar.setIndeterminate(false);
                progressBar.setProgress(0);
                progressBar.setMax(dataset.size());
            });

            // Small delay to ensure UI thread processed the indeterminate change
            try { Thread.sleep(50); } catch (InterruptedException ignored) {}

            List<long[]> allTokens = new ArrayList<>();
            for (DatasetItem item : dataset) {
                allTokens.add(item.tokens);
            }
            
            mTextEmbeddings = engine.getOrCreateTextEmbeddings(getFilesDir(), allTokens, (current, total) -> {
                runOnUiThread(() -> {
                    progressBar.setProgress(current);
                    int percent = (int) ((current * 100.0f) / total);
                    tvProgressPercent.setText(getString(R.string.progress_percent, percent));
                });
            });

            runOnUiThread(() -> {
                loadingOverlay.setVisibility(View.GONE);
            });
        }).start();
    }

    private void loadDataset() {
        try (InputStream is = getAssets().open("data/dataset.json");
             BufferedReader reader = new BufferedReader(new InputStreamReader(is))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) sb.append(line);
            
            JSONArray array = new JSONArray(sb.toString());
            for (int i = 0; i < array.length(); i++) {
                JSONObject obj = array.getJSONObject(i);
                String text = obj.getString("text");
                JSONArray tokensJson = obj.getJSONArray("tokens");
                long[] tokens = new long[tokensJson.length()];
                for (int j = 0; j < tokensJson.length(); j++) {
                    tokens[j] = tokensJson.getLong(j);
                }
                dataset.add(new DatasetItem(text, tokens, i));
            }
            Log.i(TAG, "Loaded " + dataset.size() + " items from dataset.json");
        } catch (Exception e) {
            Log.e(TAG, "Error loading dataset.json", e);
        }
    }

    private String copyAssetToInternalStorage(String assetPath) {
        String fileName = new java.io.File(assetPath).getName();
        java.io.File file = new java.io.File(getFilesDir(), fileName);
        if (file.exists()) return file.getAbsolutePath();

        try (InputStream is = getAssets().open(assetPath);
             java.io.FileOutputStream os = new java.io.FileOutputStream(file)) {
            byte[] buffer = new byte[4096];
            int read;
            while ((read = is.read(buffer)) != -1) os.write(buffer, 0, read);
            return file.getAbsolutePath();
        } catch (Exception e) {
            Log.e(TAG, "Error copying asset: " + assetPath, e);
            return null;
        }
    }

    private void prepareImage(Uri uri) {
        try {
            InputStream is = getContentResolver().openInputStream(uri);
            Bitmap originalBitmap = BitmapFactory.decodeStream(is);
            if (is != null) is.close();

            // 1. Correct Orientation based on EXIF
            originalBitmap = rotateBitmapIfRequired(originalBitmap, uri);
            
            // 2. Create a high-quality square crop for display (to avoid pixelation)
            int width = originalBitmap.getWidth();
            int height = originalBitmap.getHeight();
            int size = Math.min(width, height);
            int x = (width - size) / 2;
            int y = (height - size) / 2;
            Bitmap displayBitmap = Bitmap.createBitmap(originalBitmap, x, y, size, size);
            
            // 3. Create the specific 224x224 version for the AI model
            selectedBitmap = Bitmap.createScaledBitmap(displayBitmap, 224, 224, true);
            
            // 4. Show the high-quality square in the UI
            ivSelectedImage.setImageBitmap(displayBitmap);

            btnRun.setEnabled(true);
            resultsList.clear();
            adapter.notifyDataSetChanged();
        } catch (Exception e) {
            Log.e(TAG, "Error preparing image", e);
        }
    }

    private Bitmap rotateBitmapIfRequired(Bitmap img, Uri selectedImage) throws Exception {
        InputStream input = getContentResolver().openInputStream(selectedImage);
        ExifInterface ei;
        if (Build.VERSION.SDK_INT > 23) {
            ei = new ExifInterface(input);
        } else {
            ei = new ExifInterface(selectedImage.getPath());
        }
        int orientation = ei.getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL);
        if (input != null) input.close();

        switch (orientation) {
            case ExifInterface.ORIENTATION_ROTATE_90:
                return rotateImage(img, 90);
            case ExifInterface.ORIENTATION_ROTATE_180:
                return rotateImage(img, 180);
            case ExifInterface.ORIENTATION_ROTATE_270:
                return rotateImage(img, 270);
            default:
                return img;
        }
    }

    private static Bitmap rotateImage(Bitmap img, int degree) {
        Matrix matrix = new Matrix();
        matrix.postRotate(degree);
        Bitmap rotatedImg = Bitmap.createBitmap(img, 0, 0, img.getWidth(), img.getHeight(), matrix, true);
        img.recycle();
        return rotatedImg;
    }

    private void runModel() {
        if (selectedBitmap == null) return;
        
        try {
            // 1. Encode Image
            float[] imageEmb = engine.encodeImage(selectedBitmap);
            if (imageEmb == null) {
                Log.e(TAG, "Image encoding failed (check logcat for QNN errors)");
                return;
            }

            // 2. Compute similarity for all dataset items
            resultsList.clear();
            for (DatasetItem item : dataset) {
                if (item.index < mTextEmbeddings.size()) {
                    float[] textEmb = mTextEmbeddings.get(item.index);
                    float score = engine.computeSimilarity(imageEmb, textEmb);
                    resultsList.add(new ResultItem(item.text, score));
                }
            }

            // 3. Rank
            Collections.sort(resultsList, (a, b) -> Float.compare(b.score, a.score));
            adapter.notifyDataSetChanged();

        } catch (Exception e) {
            Log.e(TAG, "Error running model", e);
        }
    }

    private static class ResultItem {
        String text;
        float score;
        ResultItem(String text, float score) {
            this.text = text;
            this.score = score;
        }
    }

    private static class ResultAdapter extends RecyclerView.Adapter<ResultAdapter.ViewHolder> {
        private List<ResultItem> items;
        ResultAdapter(List<ResultItem> items) { this.items = items; }

        @NonNull
        @Override
        public ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(android.R.layout.simple_list_item_2, parent, false);
            return new ViewHolder(view);
        }

        @Override
        public void onBindViewHolder(@NonNull ViewHolder holder, int position) {
            ResultItem item = items.get(position);
            holder.text1.setText(item.text);
            holder.text2.setText(String.format("Score: %.4f", item.score));
        }

        @Override
        public int getItemCount() { return items.size(); }

        static class ViewHolder extends RecyclerView.ViewHolder {
            TextView text1, text2;
            ViewHolder(View v) {
                super(v);
                text1 = v.findViewById(android.R.id.text1);
                text2 = v.findViewById(android.R.id.text2);
            }
        }
    }
}
