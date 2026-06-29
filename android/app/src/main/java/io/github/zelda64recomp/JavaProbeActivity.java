package io.github.zelda64recomp;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public class JavaProbeActivity extends Activity {
    private TextView info;
    private int tapCount = 0;
    private boolean sdlLoaded = false;
    private boolean mainLoaded = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(56, 56, 56, 56);
        root.setBackgroundColor(Color.rgb(18, 12, 28));

        TextView title = new TextView(this);
        title.setText("Zelda64 Java Probe");
        title.setTextColor(Color.WHITE);
        title.setTextSize(28.0f);
        title.setGravity(Gravity.CENTER);

        info = new TextView(this);
        info.setText("Java activity loaded. SDL/native not started.");
        info.setTextColor(Color.LTGRAY);
        info.setTextSize(18.0f);
        info.setGravity(Gravity.CENTER);

        Button tapButton = makeButton("Tap Test");
        tapButton.setOnClickListener(v -> {
            tapCount++;
            setInfo("Java tap OK: " + tapCount);
        });

        Button sdlButton = makeButton("Load SDL2");
        sdlButton.setOnClickListener(v -> loadSdl2());

        Button mainButton = makeButton("Load main");
        mainButton.setOnClickListener(v -> {
            loadSdl2();
            if (!sdlLoaded) {
                return;
            }
            try {
                System.loadLibrary("main");
                mainLoaded = true;
                setInfo("Loaded SDL2 and main.");
            } catch (Throwable t) {
                setInfo("main load failed: " + t.getClass().getSimpleName() + ": " + t.getMessage());
            }
        });

        root.addView(title, matchWrap());
        root.addView(info, withMargins(matchWrap(), 0, 28, 0, 28));
        root.addView(tapButton, withMargins(matchWrap(), 0, 8, 0, 8));
        root.addView(sdlButton, withMargins(matchWrap(), 0, 8, 0, 8));
        root.addView(mainButton, withMargins(matchWrap(), 0, 8, 0, 8));

        setContentView(root);
    }

    private Button makeButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextSize(20.0f);
        button.setAllCaps(false);
        return button;
    }

    private void loadSdl2() {
        if (sdlLoaded) {
            setInfo(mainLoaded ? "SDL2 and main already loaded." : "SDL2 already loaded.");
            return;
        }

        try {
            System.loadLibrary("SDL2");
            sdlLoaded = true;
            setInfo("Loaded SDL2.");
        } catch (Throwable t) {
            setInfo("SDL2 load failed: " + t.getClass().getSimpleName() + ": " + t.getMessage());
        }
    }

    private void setInfo(String text) {
        info.setText(text);
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        );
    }

    private LinearLayout.LayoutParams withMargins(LinearLayout.LayoutParams params, int left, int top, int right, int bottom) {
        params.setMargins(left, top, right, bottom);
        return params;
    }
}
