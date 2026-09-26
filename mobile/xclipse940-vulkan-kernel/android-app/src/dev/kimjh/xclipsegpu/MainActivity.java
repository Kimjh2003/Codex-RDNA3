package dev.kimjh.xclipsegpu;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.Arrays;

public final class MainActivity extends Activity {
    private static final String TAG = "XclipseGpuDemo";
    static { System.loadLibrary("xclipse_demo"); }
    private TextView status;
    private Button runButton;
    private volatile boolean running;

    private native int runNative(String astc, String shader, String golden,
                                 String outRgba, String outWords);

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(32, 48, 32, 32);
        layout.setGravity(Gravity.CENTER_HORIZONTAL);
        status = new TextView(this);
        status.setTextSize(18);
        status.setText("Xclipse 940 Vulkan compute demo\nReady; auto-run in 5 seconds");
        runButton = new Button(this);
        runButton.setText("Run ASTC + SVE/GPU verification");
        runButton.setOnClickListener(v -> runDemo());
        layout.addView(status);
        layout.addView(runButton);
        setContentView(layout);
        new Handler(Looper.getMainLooper()).postDelayed(this::runDemo, 5000);
    }

    private File copyAsset(String name) throws Exception {
        File file = new File(getFilesDir(), name);
        try (InputStream in = getAssets().open(name);
             FileOutputStream out = new FileOutputStream(file)) {
            byte[] buffer = new byte[65536];
            int count;
            while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
        }
        return file;
    }

    private static byte[] sha256(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new java.io.FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int count;
            while ((count = in.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        return digest.digest();
    }

    private void setStatus(String value) {
        Log.i(TAG, value);
        runOnUiThread(() -> status.setText(value));
    }

    private void runDemo() {
        if (running) return;
        running = true;
        runButton.setEnabled(false);
        setStatus("Running on device...");
        new Thread(() -> {
            try {
                File astc = copyAsset("source.astc");
                File shader = copyAsset("fused_astc_u64.spv");
                File golden = copyAsset("golden.rgba");
                File wordsExpected = copyAsset("expected_word_planes.bin");
                File outRgba = new File(getFilesDir(), "output.rgba");
                File outWords = new File(getFilesDir(), "output.words");
                if (outRgba.exists() && !outRgba.delete()) throw new Exception("Cannot reset RGBA output");
                if (outWords.exists() && !outWords.delete()) throw new Exception("Cannot reset word output");
                int rc = runNative(astc.getAbsolutePath(), shader.getAbsolutePath(),
                                   golden.getAbsolutePath(), outRgba.getAbsolutePath(),
                                   outWords.getAbsolutePath());
                if (rc != 0) {
                    setStatus("GPU run failed: exit=" + rc);
                    return;
                }
                boolean rgbaOk = Arrays.equals(sha256(golden), sha256(outRgba));
                boolean wordsOk = Arrays.equals(sha256(wordsExpected), sha256(outWords));
                setStatus("GPU run exit=" + rc + "\nASTC RGBA SHA-256=" + (rgbaOk ? "PASS" : "FAIL")
                        + "\nSVE/GPU word planes SHA-256=" + (wordsOk ? "PASS" : "FAIL"));
            } catch (Exception e) {
                setStatus("FAIL: " + e);
                Log.e(TAG, "Demo failed", e);
            } finally {
                running = false;
                runOnUiThread(() -> runButton.setEnabled(true));
            }
        }, "xclipse-demo-worker").start();
    }
}
