# Add project specific ProGuard rules here.
# By default, the flags in this file are appended to flags specified
# in the Android SDK tools.

# Keep JNI native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep the CCPlayer JNI wrapper
-keep class com.ccplayer.CCPlayer { *; }
