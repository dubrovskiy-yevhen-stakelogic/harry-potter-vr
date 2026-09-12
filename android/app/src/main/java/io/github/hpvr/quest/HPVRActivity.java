package io.github.hpvr.quest;

import android.Manifest;
import android.app.NativeActivity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;

/** Platform permission and licensed speech-model staging only; no audio recording. */
public final class HPVRActivity extends NativeActivity {
    private volatile String voiceModelPath = "";
    private volatile boolean permissionPending;
    private static final int MAX_VOICE_FILE_BYTES = 8 * 1024 * 1024;

    // InputStream.readAllBytes requires API 33; Quest support starts at API 32.
    private static byte[] readVoiceAsset(InputStream stream) throws java.io.IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        byte[] block = new byte[8192];
        int count;
        while ((count = stream.read(block)) != -1) {
            if (count == 0) continue;
            if (bytes.size() > MAX_VOICE_FILE_BYTES - count) {
                throw new java.io.IOException("Voice asset exceeds the fixed model limit");
            }
            bytes.write(block, 0, count);
        }
        return bytes.toByteArray();
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        // Small licensed model, independent of game preparation and VR rendering.
        new Thread(() -> {
            try {
                // A new model-specific cache leaves previous model files and all saves untouched.
                // Android may expose app storage through /data/user/0 aliases.
                // Resolve that OS-owned base first, then reject links below it.
                File root = new File(getFilesDir().getCanonicalFile(), "Voice/sherpa-onnx-1.13.7");
                if (!root.getCanonicalPath().equals(root.getAbsolutePath()))
                    throw new java.io.IOException("Voice model cache must not contain symbolic links");
                String[] files = {"encoder.int8.onnx", "decoder.onnx", "joiner.int8.onnx",
                    "tokens.txt", "flipendo.keywords", "MODEL-README.md", "LICENSE-model.txt",
                    "LICENSE-sherpa-onnx.txt", "LICENSE-onnxruntime.txt", "NOTICE-onnxruntime-third-party.txt",
                    "LICENSE-json.txt", "LICENSE-kaldi-decoder.txt", "LICENSE-kaldi-native-fbank.txt",
                    "LICENSE-kaldifst.txt", "LICENSE-kissfft.txt", "NOTICE-kissfft.txt", "LICENSE-openfst.txt",
                    "LICENSE-simple-sentencepiece.txt", "EIGEN-COPYING.APACHE.txt", "EIGEN-COPYING.BSD.txt",
                    "EIGEN-COPYING.MINPACK.txt", "EIGEN-COPYING.MPL2.txt", "EIGEN-COPYING.README.txt",
                    "EIGEN-LICENSE.txt"};
                for (String relative : files) {
                    byte[] bytes;
                    try (InputStream stream = getAssets().open("hpvr-voice/" + relative)) {
                        bytes = readVoiceAsset(stream);
                    }
                    File target = new File(root, relative);
                    if (!target.getCanonicalPath().equals(target.getAbsolutePath()))
                        throw new java.io.IOException("Voice asset must not be a symbolic link");
                    MessageDigest digest = MessageDigest.getInstance("SHA-256");
                    if (target.isFile() && target.length() == bytes.length && MessageDigest.isEqual(digest.digest(bytes),
                            digest.digest(Files.readAllBytes(target.toPath())))) continue;
                    File parent = target.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) throw new java.io.IOException("Voice directory");
                    File pending = File.createTempFile("model-", ".tmp", parent);
                    try {
                        try (FileOutputStream out = new FileOutputStream(pending)) {
                            out.write(bytes); out.getFD().sync();
                        }
                        Files.move(pending.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING,
                            StandardCopyOption.ATOMIC_MOVE);
                    } finally { if (pending.exists()) pending.delete(); }
                }
                voiceModelPath = root.getAbsolutePath();
                Log.i("HPVR.Quest", "[hpvr.quest.voice] model=READY engine=SHERPA_ONNX_1_13_7 network=NONE recording_files=NONE");
            } catch (Exception error) { Log.e("HPVR.Quest", "Voice model unavailable", error); }
        }, "HPVR-Voice-Model").start();
    }
    public String getVoiceModelPath() { return voiceModelPath; }
    public boolean isVoicePermissionGranted() {
        return checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
    }
    /** Called only after the player enables VOICE CAST in the VR menu. */
    public void requestVoicePermission() {
        runOnUiThread(() -> {
            if (!isVoicePermissionGranted() && !permissionPending) {
                permissionPending = true;
                requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, 44);
            }
        });
    }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == 44) permissionPending = false;
    }
}
