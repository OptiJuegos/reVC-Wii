# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

# Uncomment this to preserve the line number information for
# debugging stack traces.
# -keepattributes SourceFile,LineNumberTable

# If you keep the line number information, uncomment this to
# hide the original source file name.
-renamesourcefileattribute SourceFile

-dontusemixedcaseclassnames
-dontskipnonpubliclibraryclasses
-verbose

# If you want to enable optimization, you should include the comenta o proguard pra ve
# following:
 -optimizations !code/simplification/arithmetic,!code/simplification/cast,!field/*,!class/merging/*
 -optimizationpasses 5
 -allowaccessmodification

# Specify the package name to apply string encryption
 -keeppackagenames com.revc.game

-keepattributes Signature
-keepattributes *Annotation*


-keep,includedescriptorclasses,allowoptimization class org.libsdl.app.SDLInputConnection {
    void nativeCommitText(java.lang.String, int);
    void nativeGenerateScancodeForUnichar(char);
}

-keep,includedescriptorclasses class org.libsdl.app.SDLActivity {
    # for some reason these aren't compatible with allowoptimization modifier
    boolean supportsRelativeMouse();
    void setWindowStyle(boolean);
}

-keep,includedescriptorclasses,allowoptimization class org.libsdl.app.SDLActivity {
    java.lang.String nativeGetHint(java.lang.String); # Java-side doesn't use this, so it gets minified, but C-side still tries to register it
    boolean onNativeSoftReturnKey();
    void onNativeKeyboardFocusLost();
    boolean isScreenKeyboardShown();
    android.util.DisplayMetrics getDisplayDPI();
    java.lang.String clipboardGetText();
    boolean clipboardHasText();
    void clipboardSetText(java.lang.String);
    int createCustomCursor(int[], int, int, int, int);
    void destroyCustomCursor(int);
    android.content.Context getContext();
    boolean getManifestEnvironmentVariables();
    android.view.Surface getNativeSurface();
    void initTouch();
    boolean isAndroidTV();
    boolean isChromebook();
    boolean isDeXMode();
    boolean isTablet();
    void manualBackButton();
    int messageboxShowMessageBox(int, java.lang.String, java.lang.String, int[], int[], java.lang.String[], int[]);
    void minimizeWindow();
    int openURL(java.lang.String);
    void requestPermission(java.lang.String, int);
    int showToast(java.lang.String, int, int, int, int);
    boolean sendMessage(int, int);
    boolean setActivityTitle(java.lang.String);
    boolean setCustomCursor(int);
    void setOrientation(int, int, boolean, java.lang.String);
    boolean setRelativeMouseEnabled(boolean);
    boolean setSystemCursor(int);
    boolean shouldMinimizeOnFocusLoss();
    boolean showTextInput(int, int, int, int);
}

-keep,includedescriptorclasses,allowoptimization class org.libsdl.app.HIDDeviceManager {
    boolean initialize(boolean, boolean);
    boolean openDevice(int);
    int sendOutputReport(int, byte[]);
    int sendFeatureReport(int, byte[]);
    boolean getFeatureReport(int, byte[]);
    void closeDevice(int);
}

-keep,includedescriptorclasses,allowoptimization class org.libsdl.app.SDLAudioManager {
    int[] getAudioOutputDevices();
    int[] getAudioInputDevices();
    int[] audioOpen(int, int, int, int, int);
    void audioWriteFloatBuffer(float[]);
    void audioWriteShortBuffer(short[]);
    void audioWriteByteBuffer(byte[]);
    void audioClose();
    int[] captureOpen(int, int, int, int, int);
    int captureReadFloatBuffer(float[], boolean);
    int captureReadShortBuffer(short[], boolean);
    int captureReadByteBuffer(byte[], boolean);
    void captureClose();
    void audioSetThreadPriority(boolean, int);
    native int nativeSetupJNI();
    native void removeAudioDevice(boolean, int);
    native void addAudioDevice(boolean, int);
}

-keep,includedescriptorclasses,allowoptimization class org.libsdl.app.SDLControllerManager {
    void pollInputDevices();
    void pollHapticDevices();
    void hapticRun(int, float, int);
    void hapticStop(int);
}

 -dontwarn com.revc.game.MainActivity*
 -keep class com.revc.game.MainActivity { *; }

 -dontwarn androidx.**
 -keep class androidx.** { *; }

 -dontwarn org.ini4j.**
 -keep class org.ini4j.** { *; }

 -repackageclasses 'a'

 -keepattributes Exceptions, InnerClasses, Signature, Deprecated, SourceFile,
 LineNumberTable, *Annotation*, EnclosingMethod
 -dontwarn android.webkit.JavascriptInterface

 -dontwarn org.jetbrains.annotations.**

-assumenosideeffects class android.util.Log {
    public static *** d(...);
    public static *** v(...);
    public static *** w(...);
    public static *** i(...);
}
