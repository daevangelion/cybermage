package com.jshield.cybermage;

import android.os.Build;
import android.os.Bundle;
import android.view.WindowManager;
import android.content.pm.PackageManager;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(attributes);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    @Override
    protected String[] getArguments() {
        String controls = getIntent().getStringExtra("controls");
        String renderer = getIntent().getStringExtra("renderer");
        String display = getIntent().getStringExtra("display");
        String view = getIntent().getStringExtra("view");
        boolean tv = getPackageManager().hasSystemFeature(PackageManager.FEATURE_LEANBACK);
        String mode = controls == null ? "modern" : controls;
        /* Touch-only phones cannot play with DOS keyboard controls; classic stays
           available with --es controls classic. TV remotes get the remote
           profile on top of modern controls. Game rules are unaffected. */
        String[] args = {
            "--data", new java.io.File(getExternalFilesDir(null), "data").getAbsolutePath(),
            "--controls", mode,
            "--renderer", renderer == null ? "gl" : renderer,
            "--display", display == null ? "fit" : display,
            "--view-size", view == null ? "0" : view,
            "--remote"
        };
        return tv && mode.equals("modern") ? args : java.util.Arrays.copyOf(args, args.length - 1);
    }
}
