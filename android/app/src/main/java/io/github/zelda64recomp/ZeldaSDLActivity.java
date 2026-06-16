package io.github.zelda64recomp;

import android.Manifest;
import android.app.Activity;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public class ZeldaSDLActivity extends SDLActivity implements SensorEventListener {
    private static final String TAG = "ZeldaSDLActivity";
    private static final int ANDROID_SENSOR_ACCEL = 0;
    private static final int ANDROID_SENSOR_GYRO = 1;
    private static final int TOUCH_CONTROLLER_ATTACH_RETRY_LIMIT = 20;
    private static final long TOUCH_CONTROLLER_ATTACH_RETRY_DELAY_MS = 250;
    private static final int REQUEST_OPEN_FILE = 0x5A64;
    private static final int REQUEST_STORAGE_PERMISSION = 0x5A65;
    private static final int REQUEST_OPEN_MOD_FILES = 0x5A66;
    private static final String PUBLIC_FOLDER_NAME = "Zelda64";
    private static final String PREFS_NAME = "io.github.zelda64recomp.prefs";
    private static final String PREF_TOUCH_CONTROLS_DISABLED = "touchControlsDisabled";
    private static final String PREF_TOUCH_CONTROLS_HIDDEN = "touchControlsHidden";
    private static final String[] USER_DATA_SUBDIRS = {
            "mods",
            "mod_config",
            "roms",
            "saves"
    };
    private static final String BUNDLED_MODS_ASSET_DIR = "bundled_mods";
    private static final String[] BUNDLED_ANDROID_MODS = {
            "ProxyMM_KV.nrm",
            "ProxyRecomp_KV005.so",
            "yazmt_mm_playermodelmanager_fsmodels.nrm",
            "yazmt_mm_playermodelmanager_fsmodels_extlib.so"
    };
    private static ZeldaSDLActivity currentActivity;

    public static native void nativeSetAndroidSurfaceReady(boolean ready);
    public static native void nativeSetAppAudioActive(boolean active);
    public native boolean attachController();
    public native void detachController();
    public native void setButton(int button, boolean value);
    public native void setAxis(int axis, short value);
    public native void submitAndroidMotionSensor(int sensorType, float x, float y, float z, long timestampNs);
    private static native void nativeOnFileDialogResult(boolean success, String path);
    private static native void nativeOnFileDialogMultipleResult(boolean success, String[] paths);

    private SharedPreferences preferences;
    private boolean activityResumed;
    private boolean windowFocused;
    private boolean appAudioActive;
    private boolean usingPublicDataDir;
    private boolean storageSettingsRequested;
    private boolean publicStorageReadyNotified;
    private File appDataDir;
    private File nativeModLibDir;
    private View overlayView;
    private ViewGroup buttonGroup;
    private Button buttonToggle;
    private FrameLayout leftJoystick;
    private ImageView leftJoystickKnob;
    private boolean touchControllerAttached;
    private SensorManager sensorManager;
    private Sensor accelerometerSensor;
    private Sensor gyroscopeSensor;
    private boolean motionSensorsRegistered;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private int touchControllerAttachRetries;
    private final Runnable touchControllerAttachRetry = this::retryTouchControllerAttach;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.i(TAG, "onCreate begin");
        currentActivity = this;
        lockLandscape();
        preferences = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        setupMotionSensors();

        File programDir = new File(getFilesDir(), "program");
        appDataDir = resolveAppDataDir();
        nativeModLibDir = new File(getCodeCacheDir(), "native_mods");
        if (!programDir.exists() && !programDir.mkdirs()) {
            Log.w(TAG, "Failed to create program dir: " + programDir);
        }
        if (!nativeModLibDir.exists() && !nativeModLibDir.mkdirs()) {
            Log.w(TAG, "Failed to create native mod library dir: " + nativeModLibDir);
        }
        prepareAppDataDir(appDataDir);

        try {
            Log.i(TAG, "extracting bundled program assets");
            copyAssetTree("program", programDir);
            seedBundledAndroidMods(appDataDir);
            Log.i(TAG, "bundled program assets extracted");
        } catch (IOException e) {
            Log.e(TAG, "Failed to extract bundled program assets", e);
        }

        Log.i(TAG, "calling SDLActivity.onCreate");
        super.onCreate(savedInstanceState);
        Log.i(TAG, "SDLActivity.onCreate returned");
        lockLandscape();
        applyImmersiveFullscreen();
        setupControllerOverlay();
        attachTouchController();

        nativeSetenv("APP_PROGRAM_PATH", programDir.getAbsolutePath());
        nativeSetenv("APP_FOLDER_PATH", appDataDir.getAbsolutePath());
        nativeSetenv("APP_NATIVE_LIBS_PATH", nativeModLibDir.getAbsolutePath());
        nativeSetenv("APP_ANDROID_VERSION_NAME", getAndroidVersionName());
        Log.i(TAG, "APP_PROGRAM_PATH=" + programDir.getAbsolutePath());
        Log.i(TAG, "APP_FOLDER_PATH=" + appDataDir.getAbsolutePath());
        Log.i(TAG, "APP_NATIVE_LIBS_PATH=" + nativeModLibDir.getAbsolutePath());
        Log.i(TAG, "APP_ANDROID_VERSION_NAME=" + getAndroidVersionName());

        if (!usingPublicDataDir) {
            requestPublicStorageAccess();
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    @Override
    protected void onResume() {
        Log.i(TAG, "onResume begin");
        super.onResume();
        lockLandscape();
        applyImmersiveFullscreen();
        activityResumed = true;
        updateAppAudioActive();
        notifyIfPublicStorageNowAvailable();
        registerMotionSensors();
        if (!touchControllerAttached) {
            scheduleTouchControllerAttachRetry();
        }
        Log.i(TAG, "onResume end");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE_PERMISSION) {
            notifyIfPublicStorageNowAvailable();
        }
    }

    @Override
    protected void onPause() {
        activityResumed = false;
        updateAppAudioActive();
        unregisterMotionSensors();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        Log.i(TAG, "onWindowFocusChanged=" + hasFocus);
        windowFocused = hasFocus;
        if (hasFocus) {
            applyImmersiveFullscreen();
        }
        updateAppAudioActive();
        super.onWindowFocusChanged(hasFocus);
    }

    @Override
    protected void onDestroy() {
        if (currentActivity == this) {
            currentActivity = null;
        }
        if (touchControllerAttached) {
            detachController();
            touchControllerAttached = false;
        }
        mainHandler.removeCallbacks(touchControllerAttachRetry);
        super.onDestroy();
    }

    private void setupMotionSensors() {
        sensorManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        if (sensorManager == null) {
            Log.w(TAG, "Sensor manager is not available");
            return;
        }

        accelerometerSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        gyroscopeSensor = sensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        Log.i(TAG, "Android motion sensors accel=" + (accelerometerSensor != null) + " gyro=" + (gyroscopeSensor != null));
    }

    private void registerMotionSensors() {
        if (sensorManager == null || motionSensorsRegistered) {
            return;
        }

        boolean registeredAny = false;
        if (accelerometerSensor != null) {
            registeredAny |= sensorManager.registerListener(this, accelerometerSensor, SensorManager.SENSOR_DELAY_GAME);
        }
        if (gyroscopeSensor != null) {
            registeredAny |= sensorManager.registerListener(this, gyroscopeSensor, SensorManager.SENSOR_DELAY_GAME);
        }
        motionSensorsRegistered = registeredAny;
    }

    private void unregisterMotionSensors() {
        if (sensorManager == null || !motionSensorsRegistered) {
            return;
        }

        sensorManager.unregisterListener(this);
        motionSensorsRegistered = false;
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.sensor == null) {
            return;
        }

        int sensorType = event.sensor.getType();
        if (sensorType != Sensor.TYPE_ACCELEROMETER && sensorType != Sensor.TYPE_GYROSCOPE) {
            return;
        }

        float[] values = orientMotionValues(event.values);
        if (sensorType == Sensor.TYPE_ACCELEROMETER) {
            submitAndroidMotionSensor(ANDROID_SENSOR_ACCEL, -values[0], values[1], values[2], event.timestamp);
        } else {
            submitAndroidMotionSensor(ANDROID_SENSOR_GYRO, values[0], values[1], values[2], event.timestamp);
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    private float[] orientMotionValues(float[] values) {
        int rotation = getWindowManager().getDefaultDisplay().getRotation();
        float x;
        float y;
        switch (rotation) {
            case Surface.ROTATION_90:
                x = -values[1];
                y = values[0];
                break;
            case Surface.ROTATION_270:
                x = values[1];
                y = -values[0];
                break;
            case Surface.ROTATION_180:
                x = -values[0];
                y = -values[1];
                break;
            case Surface.ROTATION_0:
            default:
                x = values[0];
                y = values[1];
                break;
        }

        return new float[] { x, y, values[2] };
    }

    private void attachTouchController() {
        touchControllerAttached = attachController();
        if (!touchControllerAttached) {
            Log.w(TAG, "Touch controller attach failed; controls will retry on first touch");
            scheduleTouchControllerAttachRetry();
        } else {
            touchControllerAttachRetries = 0;
            mainHandler.removeCallbacks(touchControllerAttachRetry);
        }
    }

    private void ensureTouchControllerAttached() {
        if (!touchControllerAttached) {
            attachTouchController();
        }
    }

    private void scheduleTouchControllerAttachRetry() {
        mainHandler.removeCallbacks(touchControllerAttachRetry);
        if (touchControllerAttached || touchControllerAttachRetries >= TOUCH_CONTROLLER_ATTACH_RETRY_LIMIT) {
            return;
        }
        mainHandler.postDelayed(touchControllerAttachRetry, TOUCH_CONTROLLER_ATTACH_RETRY_DELAY_MS);
    }

    private void retryTouchControllerAttach() {
        if (touchControllerAttached) {
            return;
        }
        touchControllerAttachRetries++;
        touchControllerAttached = attachController();
        if (!touchControllerAttached) {
            scheduleTouchControllerAttachRetry();
        } else {
            touchControllerAttachRetries = 0;
        }
    }

    private void setupControllerOverlay() {
        LayoutInflater inflater = (LayoutInflater) getSystemService(LAYOUT_INFLATER_SERVICE);
        overlayView = inflater.inflate(R.layout.touchcontrol_overlay, null);
        FrameLayout.LayoutParams layoutParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT);
        overlayView.setLayoutParams(layoutParams);
        addContentView(overlayView, layoutParams);

        buttonGroup = overlayView.findViewById(R.id.button_group);
        buttonToggle = overlayView.findViewById(R.id.buttonToggle);
        leftJoystick = overlayView.findViewById(R.id.left_joystick);
        leftJoystickKnob = overlayView.findViewById(R.id.left_joystick_knob);

        addButtonTouchListener(overlayView.findViewById(R.id.buttonA), ControllerButtons.BUTTON_A);
        addButtonTouchListener(overlayView.findViewById(R.id.buttonB), ControllerButtons.BUTTON_X);
        addButtonTouchListener(overlayView.findViewById(R.id.buttonX), ControllerButtons.BUTTON_B);
        addButtonTouchListener(overlayView.findViewById(R.id.buttonY), ControllerButtons.BUTTON_Y);
        addButtonTouchListener(overlayView.findViewById(R.id.buttonL), ControllerButtons.BUTTON_LB);
        addAxisButtonTouchListener(overlayView.findViewById(R.id.buttonR), ControllerButtons.AXIS_RT, Short.MAX_VALUE);
        addAxisButtonTouchListener(overlayView.findViewById(R.id.buttonZ), ControllerButtons.AXIS_LT, Short.MAX_VALUE);
        addButtonTouchListener(overlayView.findViewById(R.id.buttonStart), ControllerButtons.BUTTON_START);
        addButtonTouchListener(overlayView.findViewById(R.id.buttonBack), ControllerButtons.BUTTON_BACK);

        addAxisButtonTouchListener(overlayView.findViewById(R.id.buttonDpadUp), ControllerButtons.AXIS_RY, Short.MIN_VALUE);
        addAxisButtonTouchListener(overlayView.findViewById(R.id.buttonDpadDown), ControllerButtons.AXIS_RY, Short.MAX_VALUE);
        addAxisButtonTouchListener(overlayView.findViewById(R.id.buttonDpadLeft), ControllerButtons.AXIS_RX, Short.MIN_VALUE);
        addAxisButtonTouchListener(overlayView.findViewById(R.id.buttonDpadRight), ControllerButtons.AXIS_RX, Short.MAX_VALUE);

        setupJoystick();
        setupToggleButton();
        applyTouchControlsVisibility();
    }

    private void setupToggleButton() {
        buttonToggle.setOnClickListener((view) -> {
            boolean hidden = buttonGroup.getVisibility() == View.VISIBLE;
            preferences.edit().putBoolean(PREF_TOUCH_CONTROLS_HIDDEN, hidden).apply();
            applyTouchControlsVisibility();
        });
    }

    private void applyTouchControlsVisibility() {
        if (buttonGroup == null) {
            return;
        }
        boolean disabled = preferences.getBoolean(PREF_TOUCH_CONTROLS_DISABLED, false);
        overlayView.setVisibility(disabled ? View.GONE : View.VISIBLE);

        boolean hidden = preferences.getBoolean(PREF_TOUCH_CONTROLS_HIDDEN, false);
        buttonGroup.setVisibility(hidden ? View.INVISIBLE : View.VISIBLE);
    }

    public boolean areTouchControlsDisabledFromNative() {
        return preferences != null && preferences.getBoolean(PREF_TOUCH_CONTROLS_DISABLED, false);
    }

    public void setTouchControlsDisabledFromNative(boolean disabled) {
        if (preferences != null) {
            preferences.edit().putBoolean(PREF_TOUCH_CONTROLS_DISABLED, disabled).apply();
        }
        runOnUiThread(this::applyTouchControlsVisibility);
    }

    private void addButtonTouchListener(Button button, int buttonNum) {
        button.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    ensureTouchControllerAttached();
                    setButton(buttonNum, true);
                    button.setPressed(true);
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    setButton(buttonNum, false);
                    button.setPressed(false);
                    return true;
                default:
                    return true;
            }
        });
    }

    private void addAxisButtonTouchListener(Button button, int axis, short value) {
        button.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    ensureTouchControllerAttached();
                    setAxis(axis, value);
                    button.setPressed(true);
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    setAxis(axis, (short) 0);
                    button.setPressed(false);
                    return true;
                default:
                    return true;
            }
        });
    }

    private void setupJoystick() {
        leftJoystick.post(() -> {
            final float joystickCenterX = leftJoystick.getWidth() / 2.0f;
            final float joystickCenterY = leftJoystick.getHeight() / 2.0f;
            final float maxRadius = leftJoystick.getWidth() / 2.0f - leftJoystickKnob.getWidth() / 2.0f;

            leftJoystick.setOnTouchListener((view, event) -> {
                switch (event.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                    case MotionEvent.ACTION_MOVE: {
                        ensureTouchControllerAttached();
                        float deltaX = event.getX() - joystickCenterX;
                        float deltaY = event.getY() - joystickCenterY;
                        float distance = (float) Math.sqrt(deltaX * deltaX + deltaY * deltaY);
                        if (distance > maxRadius && distance > 0.0f) {
                            float scale = maxRadius / distance;
                            deltaX *= scale;
                            deltaY *= scale;
                        }

                        leftJoystickKnob.setX(joystickCenterX + deltaX - leftJoystickKnob.getWidth() / 2.0f);
                        leftJoystickKnob.setY(joystickCenterY + deltaY - leftJoystickKnob.getHeight() / 2.0f);

                        setAxis(ControllerButtons.AXIS_LX, (short) (deltaX / maxRadius * Short.MAX_VALUE));
                        setAxis(ControllerButtons.AXIS_LY, (short) (deltaY / maxRadius * Short.MAX_VALUE));
                        return true;
                    }
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        leftJoystickKnob.setX(joystickCenterX - leftJoystickKnob.getWidth() / 2.0f);
                        leftJoystickKnob.setY(joystickCenterY - leftJoystickKnob.getHeight() / 2.0f);
                        setAxis(ControllerButtons.AXIS_LX, (short) 0);
                        setAxis(ControllerButtons.AXIS_LY, (short) 0);
                        return true;
                    default:
                        return true;
                }
            });
        });
    }

    public static void openFileDialog() {
        ZeldaSDLActivity activity = currentActivity;
        if (activity == null) {
            Log.w(TAG, "openFileDialog requested without an active activity");
            nativeOnFileDialogResult(false, "");
            return;
        }

        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                        "application/octet-stream",
                        "application/x-n64-rom",
                        "application/vnd.nintendo.snes.rom"
                });
                activity.startActivityForResult(intent, REQUEST_OPEN_FILE);
            } catch (Exception e) {
                Log.e(TAG, "Failed to launch file picker", e);
                nativeOnFileDialogResult(false, "");
            }
        });
    }

    public static void openModFileDialog() {
        ZeldaSDLActivity activity = currentActivity;
        if (activity == null) {
            Log.w(TAG, "openModFileDialog requested without an active activity");
            nativeOnFileDialogMultipleResult(false, new String[0]);
            return;
        }

        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                        "application/zip",
                        "application/octet-stream",
                        "application/x-zip-compressed"
                });
                activity.startActivityForResult(intent, REQUEST_OPEN_MOD_FILES);
            } catch (Exception e) {
                Log.e(TAG, "Failed to launch mod file picker", e);
                nativeOnFileDialogMultipleResult(false, new String[0]);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_OPEN_FILE) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                handlePickedFile(data.getData());
            } else {
                nativeOnFileDialogResult(false, "");
            }
            return;
        }

        if (requestCode == REQUEST_OPEN_MOD_FILES) {
            if (resultCode == Activity.RESULT_OK && data != null) {
                handlePickedModFiles(data);
            } else {
                nativeOnFileDialogMultipleResult(false, new String[0]);
            }
            return;
        }

        super.onActivityResult(requestCode, resultCode, data);
    }

    private void lockLandscape() {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    private void applyImmersiveFullscreen() {
        Window window = getWindow();
        if (window == null) {
            return;
        }
        View decorView = window.getDecorView();
        if (decorView == null) {
            return;
        }

        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = decorView.getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }

        decorView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    private String getAndroidVersionName() {
        try {
            PackageInfo packageInfo = getPackageManager().getPackageInfo(getPackageName(), 0);
            if (packageInfo.versionName != null) {
                return packageInfo.versionName;
            }
        } catch (PackageManager.NameNotFoundException e) {
            Log.w(TAG, "Failed to read package version", e);
        }
        return "0.1.0-beta.1";
    }

    private void updateAppAudioActive() {
        boolean active = activityResumed && windowFocused;
        if (active != appAudioActive) {
            appAudioActive = active;
            nativeSetAppAudioActive(active);
        }
    }

    private void copyAssetTree(String assetPath, File outputDir) throws IOException {
        AssetManager assetManager = getAssets();
        String[] children = assetManager.list(assetPath);
        if (children == null || children.length == 0) {
            copyAssetFile(assetPath, outputDir);
            return;
        }

        if (!outputDir.exists() && !outputDir.mkdirs()) {
            throw new IOException("Failed to create directory " + outputDir);
        }

        for (String child : children) {
            String childAssetPath = assetPath + "/" + child;
            File childOutput = new File(outputDir, child);
            copyAssetTree(childAssetPath, childOutput);
        }
    }

    private void copyAssetFile(String assetPath, File outputFile) throws IOException {
        File parent = outputFile.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("Failed to create directory " + parent);
        }

        try (InputStream input = getAssets().open(assetPath);
             FileOutputStream output = new FileOutputStream(outputFile)) {
            byte[] buffer = new byte[64 * 1024];
            int bytesRead;
            while ((bytesRead = input.read(buffer)) != -1) {
                output.write(buffer, 0, bytesRead);
            }
        }
    }

    private File resolveAppDataDir() {
        File publicDir = getPublicDataDir();
        if (canUsePublicDataDir(publicDir)) {
            usingPublicDataDir = true;
            Log.i(TAG, "Using public app data dir: " + publicDir.getAbsolutePath());
            return publicDir;
        }

        usingPublicDataDir = false;
        File privateDir = new File(getFilesDir(), "data");
        Log.w(TAG, "Using private app data dir until storage access is granted: " + privateDir.getAbsolutePath());
        return privateDir;
    }

    private File getPublicDataDir() {
        return new File(Environment.getExternalStorageDirectory(), PUBLIC_FOLDER_NAME);
    }

    private boolean canUsePublicDataDir(File publicDir) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
            return false;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && Build.VERSION.SDK_INT < Build.VERSION_CODES.R
                && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            return false;
        }

        return publicDir.exists() || publicDir.mkdirs();
    }

    private void requestPublicStorageAccess() {
        if (storageSettingsRequested) {
            return;
        }
        storageSettingsRequested = true;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Toast.makeText(this, "Grant all files access to use /sdcard/Zelda64 for mods and saves.", Toast.LENGTH_LONG).show();
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            } catch (Exception e) {
                Log.w(TAG, "Unable to open app-specific all files access settings", e);
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQUEST_STORAGE_PERMISSION);
        }
    }

    private void prepareAppDataDir(File dataDir) {
        if (!dataDir.exists() && !dataDir.mkdirs()) {
            Log.w(TAG, "Failed to create app data dir: " + dataDir);
            return;
        }

        for (String subdir : USER_DATA_SUBDIRS) {
            File child = new File(dataDir, subdir);
            if (!child.exists() && !child.mkdirs()) {
                Log.w(TAG, "Failed to create app data subdir: " + child);
            }
        }
    }

    private void seedBundledAndroidMods(File dataDir) throws IOException {
        File modsDir = new File(dataDir, "mods");
        if (!modsDir.exists() && !modsDir.mkdirs()) {
            throw new IOException("Failed to create mods dir: " + modsDir);
        }

        for (String filename : BUNDLED_ANDROID_MODS) {
            File outputFile = new File(modsDir, filename);
            copyAssetFile(BUNDLED_MODS_ASSET_DIR + "/" + filename, outputFile);
        }
    }

    private File getRomDir() {
        if (appDataDir == null) {
            appDataDir = resolveAppDataDir();
            prepareAppDataDir(appDataDir);
        }
        return new File(appDataDir, "roms");
    }

    private File getModImportDir() {
        File importDir = new File(getCacheDir(), "mod_imports");
        if (!importDir.exists() && !importDir.mkdirs()) {
            Log.w(TAG, "Failed to create mod import dir: " + importDir);
        }
        return importDir;
    }

    private void notifyIfPublicStorageNowAvailable() {
        if (usingPublicDataDir) {
            return;
        }

        File publicDir = getPublicDataDir();
        if (!canUsePublicDataDir(publicDir)) {
            return;
        }
        if (publicStorageReadyNotified) {
            return;
        }
        publicStorageReadyNotified = true;

        prepareAppDataDir(publicDir);
        Toast.makeText(this, "Zelda64 folder is ready. Restart the app to use it for mods and saves.", Toast.LENGTH_LONG).show();
        Log.i(TAG, "Public storage became available: " + publicDir.getAbsolutePath());
    }

    private void handlePickedFile(Uri uri) {
        try {
            File romDir = getRomDir();
            if (!romDir.exists() && !romDir.mkdirs()) {
                throw new IOException("Failed to create ROM dir: " + romDir);
            }

            String displayName = getDisplayName(uri);
            File outputFile = uniqueFileForName(romDir, sanitizeFileName(displayName));

            try (InputStream input = getContentResolver().openInputStream(uri);
                 FileOutputStream output = new FileOutputStream(outputFile)) {
                if (input == null) {
                    throw new IOException("Content resolver returned null stream for " + uri);
                }

                byte[] buffer = new byte[1024 * 1024];
                int bytesRead;
                while ((bytesRead = input.read(buffer)) != -1) {
                    output.write(buffer, 0, bytesRead);
                }
            }

            Log.i(TAG, "Copied selected file to " + outputFile.getAbsolutePath());
            nativeSetenv("APP_PENDING_ROM_PATH", outputFile.getAbsolutePath());
            nativeOnFileDialogResult(true, outputFile.getAbsolutePath());
        } catch (Exception e) {
            Log.e(TAG, "Failed to import selected file", e);
            nativeOnFileDialogResult(false, "");
        }
    }

    private void handlePickedModFiles(Intent data) {
        try {
            File importDir = getModImportDir();
            java.util.ArrayList<String> paths = new java.util.ArrayList<>();
            ClipData clipData = data.getClipData();

            if (clipData != null) {
                for (int i = 0; i < clipData.getItemCount(); i++) {
                    Uri uri = clipData.getItemAt(i).getUri();
                    if (uri != null) {
                        paths.add(copyPickedModFile(uri, importDir));
                    }
                }
            } else if (data.getData() != null) {
                paths.add(copyPickedModFile(data.getData(), importDir));
            }

            if (paths.isEmpty()) {
                nativeOnFileDialogMultipleResult(false, new String[0]);
                return;
            }

            String[] pathArray = paths.toArray(new String[0]);
            nativeSetenv("APP_PENDING_MOD_PATHS", joinPaths(pathArray));
            nativeOnFileDialogMultipleResult(true, pathArray);
        } catch (Exception e) {
            Log.e(TAG, "Failed to import selected mod file(s)", e);
            nativeOnFileDialogMultipleResult(false, new String[0]);
        }
    }

    private String copyPickedModFile(Uri uri, File importDir) throws IOException {
        String displayName = getDisplayName(uri);
        File outputFile = uniqueFileForName(importDir, sanitizeFileName(displayName));

        try (InputStream input = getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(outputFile)) {
            if (input == null) {
                throw new IOException("Content resolver returned null stream for " + uri);
            }

            byte[] buffer = new byte[1024 * 1024];
            int bytesRead;
            while ((bytesRead = input.read(buffer)) != -1) {
                output.write(buffer, 0, bytesRead);
            }
        }

        Log.i(TAG, "Copied selected mod file to " + outputFile.getAbsolutePath());
        return outputFile.getAbsolutePath();
    }

    private static String joinPaths(String[] paths) {
        StringBuilder builder = new StringBuilder();
        for (String path : paths) {
            if (path == null || path.isEmpty()) {
                continue;
            }
            if (builder.length() > 0) {
                builder.append('\n');
            }
            builder.append(path);
        }
        return builder.toString();
    }

    private String getDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (nameIndex >= 0) {
                    String name = cursor.getString(nameIndex);
                    if (name != null && !name.isEmpty()) {
                        return name;
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Unable to query display name for " + uri, e);
        }

        String fallback = uri.getLastPathSegment();
        return fallback == null || fallback.isEmpty() ? "selected.rom" : fallback;
    }

    private static String sanitizeFileName(String name) {
        String sanitized = name.replaceAll("[\\\\/:*?\"<>|]", "_").trim();
        return sanitized.isEmpty() ? "selected.rom" : sanitized;
    }

    private static File uniqueFileForName(File dir, String name) {
        File candidate = new File(dir, name);
        if (!candidate.exists()) {
            return candidate;
        }

        int dot = name.lastIndexOf('.');
        String base = dot > 0 ? name.substring(0, dot) : name;
        String extension = dot > 0 ? name.substring(dot) : "";
        int index = 1;
        do {
            candidate = new File(dir, base + "-" + index + extension);
            index++;
        } while (candidate.exists());
        return candidate;
    }
}
