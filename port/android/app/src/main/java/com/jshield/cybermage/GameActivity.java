package com.jshield.cybermage;

import android.os.Build;
import android.os.Bundle;
import android.view.WindowManager;
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
        /* Touch-only phones cannot play with DOS keyboard controls; classic stays
           available with --es controls classic. Game rules are unaffected. */
        return new String[] {
            "--data", new java.io.File(getExternalFilesDir(null), "data").getAbsolutePath(),
            "--controls", controls == null ? "modern" : controls,
            "--renderer", renderer == null ? "gl" : renderer,
            "--display", display == null ? "fit" : display
        };
    }
}
