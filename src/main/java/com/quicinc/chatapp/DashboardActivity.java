package com.quicinc.chatapp;

import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.documentfile.provider.DocumentFile;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

public class DashboardActivity extends AppCompatActivity {
    private static final String TAG = "DashboardActivity";
    private static final float THRESHOLD = 0.25f;

    private RetrievalEngine engine;
    private RecyclerView recyclerView;
    private DashboardAdapter adapter;
    private ProgressBar progressBar;
    private EditText etSourcePath;
    private Uri sourceFolderUri;

    private List<DashboardItem> filteredResults = new ArrayList<>();
    private List<DatasetItem> dataset = new ArrayList<>();
    private List<float[]> mTextEmbeddings = new ArrayList<>();

    private final ActivityResultLauncher<Uri> folderPickerLauncher = registerForActivityResult(
            new ActivityResultContracts.OpenDocumentTree(),
            uri -> {
                if (uri != null) {
                    sourceFolderUri = uri;
                    etSourcePath.setText(uri.getPath());
                    // Persist permission for later runs
                    getContentResolver().takePersistableUriPermission(uri, 
                            Intent.FLAG_GRANT_READ_URI_PERMISSION);
                }
            }
    );

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_dashboard);

        engine = new RetrievalEngine();
        progressBar = findViewById(R.id.pb_dashboard_loading);
        recyclerView = findViewById(R.id.rv_dashboard);
        etSourcePath = findViewById(R.id.et_source_path);
        Button btnBrowse = findViewById(R.id.btn_browse);
        Button btnRun = findViewById(R.id.btn_run_dashboard);

        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        adapter = new DashboardAdapter(filteredResults);
        recyclerView.setAdapter(adapter);

        btnBrowse.setOnClickListener(v -> folderPickerLauncher.launch(null));
        btnRun.setOnClickListener(v -> {
            if (sourceFolderUri == null) {
                Toast.makeText(this, "Please select a source folder first", Toast.LENGTH_SHORT).show();
                return;
            }
            runProcessingOnSource();
        });
    }

    private void runProcessingOnSource() {
        filteredResults.clear();
        adapter.notifyDataSetChanged();
        progressBar.setVisibility(View.VISIBLE);

        new Thread(() -> {
            try {
                // 1. Ensure Engine is initialized
                String imageModelPath = getFileStreamPath("image_encoder.dlc").getAbsolutePath();
                String textModelPath = getFileStreamPath("text_encoder.dlc").getAbsolutePath();
                String htpConfigPath = getFileStreamPath("qualcomm-snapdragon-8-gen2.json").getAbsolutePath();
                String nativeLibPath = getApplicationInfo().nativeLibraryDir;
                engine.loadModels(imageModelPath, textModelPath, nativeLibPath, htpConfigPath);

                // 2. Load Dataset and Embeddings
                if (dataset.isEmpty()) loadDataset();
                if (mTextEmbeddings.isEmpty()) {
                    List<long[]> allTokens = new ArrayList<>();
                    for (DatasetItem item : dataset) allTokens.add(item.tokens);
                    mTextEmbeddings = engine.getOrCreateTextEmbeddings(getFilesDir(), allTokens, null);
                }

                // 3. Process images in the selected folder
                DocumentFile root = DocumentFile.fromTreeUri(this, sourceFolderUri);
                if (root != null && root.isDirectory()) {
                    for (DocumentFile file : root.listFiles()) {
                        String type = file.getType();
                        if (type != null && type.startsWith("image/")) {
                            processSingleImage(file.getUri());
                        }
                    }
                }

                runOnUiThread(() -> {
                    progressBar.setVisibility(View.GONE);
                    if (filteredResults.isEmpty()) {
                        Toast.makeText(this, "No matches found with score > 0.25", Toast.LENGTH_LONG).show();
                    }
                    adapter.notifyDataSetChanged();
                });

            } catch (Exception e) {
                Log.e(TAG, "Error in manual processing", e);
                runOnUiThread(() -> {
                    progressBar.setVisibility(View.GONE);
                    Toast.makeText(this, "Error processing folder: " + e.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }

    private void processSingleImage(Uri uri) {
        try (InputStream is = getContentResolver().openInputStream(uri)) {
            Bitmap original = BitmapFactory.decodeStream(is);
            if (original == null) return;

            // Simple 224x224 scaling for the model
            Bitmap scaled = Bitmap.createScaledBitmap(original, 224, 224, true);
            float[] imageEmb = engine.encodeImage(scaled);
            if (imageEmb == null) return;

            float maxScore = -1.0f;
            String bestMatchText = "";

            for (int i = 0; i < dataset.size(); i++) {
                if (i < mTextEmbeddings.size()) {
                    float score = engine.computeSimilarity(imageEmb, mTextEmbeddings.get(i));
                    if (score > maxScore) {
                        maxScore = score;
                        bestMatchText = dataset.get(i).text;
                    }
                }
            }

            if (maxScore > THRESHOLD) {
                // Keep the original image for display, but scaled down slightly to save memory in the list
                Bitmap displayBmp = Bitmap.createScaledBitmap(original, original.getWidth() / 2, original.getHeight() / 2, true);
                filteredResults.add(new DashboardItem(displayBmp, bestMatchText, maxScore));
                runOnUiThread(() -> adapter.notifyItemInserted(filteredResults.size() - 1));
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to process image: " + uri, e);
        }
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
                JSONArray tokensJson = obj.getJSONArray("tokens");
                long[] tokens = new long[tokensJson.length()];
                for (int j = 0; j < tokensJson.length(); j++) tokens[j] = tokensJson.getLong(j);
                dataset.add(new DatasetItem(obj.getString("text"), tokens, i));
            }
        } catch (Exception e) {
            Log.e(TAG, "Error loading dataset", e);
        }
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

    private static class DashboardItem {
        Bitmap bitmap;
        String topMatch;
        float score;
        DashboardItem(Bitmap bitmap, String topMatch, float score) {
            this.bitmap = bitmap;
            this.topMatch = topMatch;
            this.score = score;
        }
    }

    private static class DashboardAdapter extends RecyclerView.Adapter<DashboardAdapter.ViewHolder> {
        private final List<DashboardItem> items;
        DashboardAdapter(List<DashboardItem> items) { this.items = items; }

        @NonNull
        @Override
        public ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_dashboard_result, parent, false);
            return new ViewHolder(view);
        }

        @Override
        public void onBindViewHolder(@NonNull ViewHolder holder, int position) {
            DashboardItem item = items.get(position);
            holder.imageView.setImageBitmap(item.bitmap);
            holder.tvText.setText(item.topMatch);
            holder.tvScore.setText(String.format("Top Score: %.4f", item.score));
        }

        @Override
        public int getItemCount() { return items.size(); }

        static class ViewHolder extends RecyclerView.ViewHolder {
            ImageView imageView;
            TextView tvText, tvScore;
            ViewHolder(View v) {
                super(v);
                imageView = v.findViewById(R.id.iv_dashboard_image);
                tvText = v.findViewById(R.id.tv_dashboard_text);
                tvScore = v.findViewById(R.id.tv_dashboard_score);
            }
        }
    }
}
