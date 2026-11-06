package com.jshield.cybermage;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The APK contains no game assets. The game reads app-specific external storage,
 * filled either by adb push or by copying a user-chosen folder (storage access
 * framework, no broad storage permission).
 */
public final class DataActivity extends Activity {
    private static final int PICK_FOLDER = 1;
    private static final String[] WITNESSES = {"CM_HI.EXE", "HIPAK.RSC", "STRINGS.MSG"};

    private File data;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        openData();
    }

    private void fail(String title, String message) {
        new AlertDialog.Builder(this)
            .setTitle(title)
            .setMessage(message)
            .setPositiveButton("Close", (dialog, which) -> finish())
            .setCancelable(false).show();
    }

    private boolean ready() {
        for (String name : WITNESSES) {
            if (!new File(data, name).isFile()) return false;
        }
        return true;
    }

    private void openData() {
        File external = getExternalFilesDir(null);
        if (external == null) {
            fail("Storage unavailable", "External app storage is unavailable. Mount storage and reopen CyberMage.");
            return;
        }
        data = new File(external, "data");
        if (!data.isDirectory() && !data.mkdirs()) {
            fail("Cannot create data folder", data.getAbsolutePath());
            return;
        }
        if (ready()) {
            Intent game = new Intent(this, GameActivity.class);
            String controls = getIntent().getStringExtra("controls");
            if (controls != null) game.putExtra("controls", controls);
            startActivity(game);
            finish();
            return;
        }
        new AlertDialog.Builder(this)
            .setTitle("Choose your CyberMage data")
            .setMessage("Supply your installed retail game folder, including CM_HI.EXE. "
                + "No game files are included in this APK.\n\n"
                + "Import: pick the folder (e.g. Download/CyberMage); its files are copied into the app.\n\n"
                + "Or with adb:\nadb push orig/retail/. " + data.getAbsolutePath() + "/")
            .setPositiveButton("Import folder", (dialog, which) -> pickFolder())
            .setNeutralButton("Retry", (dialog, which) -> openData())
            .setNegativeButton("Close", (dialog, which) -> finish())
            .setCancelable(false).show();
    }

    private void pickFolder() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_FOLDER);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent intent) {
        if (request != PICK_FOLDER) return;
        if (result != RESULT_OK || intent == null || intent.getData() == null) {
            openData();
            return;
        }
        Uri tree = intent.getData();
        AlertDialog busy = new AlertDialog.Builder(this)
            .setTitle("Importing")
            .setMessage("Copying game files…")
            .setCancelable(false).show();
        new Thread(() -> {
            String error = null;
            int copied = 0;
            try {
                copied = copyTree(tree);
            } catch (IOException | RuntimeException e) {
                error = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String failure = error;
            final int count = copied;
            runOnUiThread(() -> {
                busy.dismiss();
                if (failure != null) {
                    fail("Import failed", failure);
                } else if (!ready()) {
                    fail("Import incomplete", count + " files copied, but the folder lacks CM_HI.EXE, HIPAK.RSC or STRINGS.MSG.");
                } else {
                    openData();
                }
            });
        }).start();
    }

    /** Copies the regular files directly inside the chosen folder; names keep their case. */
    private int copyTree(Uri tree) throws IOException {
        ContentResolver resolver = getContentResolver();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
            tree, DocumentsContract.getTreeDocumentId(tree));
        String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
        };
        int copied = 0;
        byte[] buffer = new byte[1 << 16];
        try (Cursor c = resolver.query(children, columns, null, null, null)) {
            if (c == null) throw new IOException("cannot list chosen folder");
            while (c.moveToNext()) {
                String name = c.getString(1);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) continue;
                if (name == null || name.isEmpty() || name.contains("/") || name.startsWith(".")) continue;
                Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0));
                File part = new File(data, name + ".part");
                try (InputStream in = resolver.openInputStream(doc);
                     OutputStream out = new FileOutputStream(part)) {
                    if (in == null) throw new IOException("cannot open " + name);
                    for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
                }
                if (!part.renameTo(new File(data, name))) throw new IOException("cannot store " + name);
                copied++;
            }
        }
        return copied;
    }
}
