package io.github.hpvr.quest;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/** Read-only installer check in the application's own storage context. */
public final class ImportAccessReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        setResultCode(Activity.RESULT_CANCELED);
        setResultData("HPVR_DATA_ACCESS=FAIL");
        if (!"io.github.hpvr.quest.VERIFY_DATA".equals(intent.getAction())) return;
        String paths = intent.getStringExtra("paths");
        if (paths == null || paths.length() > 8192) return;
        String[] entries = paths.split("\\|", -1);
        if (entries.length == 0 || entries.length > 24) return;
        try {
            File external = context.getExternalFilesDir(null);
            if (external == null) return;
            File root = new File(external, "HP").getCanonicalFile();
            for (String relative : entries) {
                if (!relative.matches("(system|Maps|Textures|Sounds|Music|Cache)/[A-Za-z0-9_./-]+")) return;
                for (String part : relative.split("/", -1)) {
                    if (part.isEmpty() || part.equals(".") || part.equals("..")) return;
                }
                File file = new File(root, relative);
                if (!file.getAbsolutePath().equals(file.getCanonicalPath()) || !file.isFile()) return;
                try (FileInputStream stream = new FileInputStream(file)) {
                    if (stream.read() == -1) return;
                }
            }
            setResultCode(Activity.RESULT_OK);
            setResultData("HPVR_DATA_ACCESS=PASS files=" + entries.length);
        } catch (IOException | SecurityException failure) {
            // No file contents or private paths are returned to the caller.
        }
    }
}
