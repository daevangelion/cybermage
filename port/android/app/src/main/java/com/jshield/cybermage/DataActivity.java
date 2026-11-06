package com.jshield.cybermage;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import java.io.File;

/** The APK contains no game assets. App-specific external storage needs no grant. */
public final class DataActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        openData();
    }

    private void openData() {
        File external = getExternalFilesDir(null);
        if (external == null) {
            new AlertDialog.Builder(this)
                .setTitle("Storage unavailable")
                .setMessage("External app storage is unavailable. Mount storage and reopen CyberMage.")
                .setPositiveButton("Close", (dialog, which) -> finish())
                .setCancelable(false).show();
            return;
        }
        File data = new File(external, "data");
        if (!data.isDirectory() && !data.mkdirs()) {
            new AlertDialog.Builder(this)
                .setTitle("Cannot create data folder")
                .setMessage(data.getAbsolutePath())
                .setPositiveButton("Close", (dialog, which) -> finish())
                .setCancelable(false).show();
            return;
        }
        boolean ready = true;
        for (String name : new String[] {"CM_HI.EXE", "HIPAK.RSC", "STRINGS.MSG"}) {
            ready &= new File(data, name).isFile();
        }
        if (ready) {
            startActivity(new Intent(this, GameActivity.class));
            finish();
            return;
        }
        new AlertDialog.Builder(this)
            .setTitle("Choose your CyberMage data")
            .setMessage("Supply your installed retail game folder, including CM_HI.EXE. "
                + "No game files are included in this APK.\n\nCopy the folder contents to:\n"
                + data.getAbsolutePath() + "\n\nUsing adb:\nadb push orig/retail/. "
                + data.getAbsolutePath() + "/\n\nThen select Retry.")
            .setPositiveButton("Retry", (dialog, which) -> openData())
            .setNegativeButton("Close", (dialog, which) -> finish())
            .setCancelable(false).show();
    }
}
