# 构建指南

本文档描述如何编译 FFmpeg 预编译库并构建 Android / iOS 应用。

## 目录布局约定

FFmpeg 预编译产物统一放在 `third_party/` 下（被 `.gitignore` 忽略）：

```
third_party/
├── ffmpeg-kit/              # ffmpeg-kit 源码（脚本自动 clone）
├── android/<abi>/           # Android 各 ABI 的 include/ + lib/*.so
│   ├── arm64-v8a/
│   └── x86_64/
└── ios/                     # iOS 预编译产物
```

内核 `core/CMakeLists.txt` 通过 `FFMPEG_DIR` 定位头文件与库；
Android 下会自动追加 ABI 子目录（`FFMPEG_DIR/<ANDROID_ABI>`）。

## 1. 编译 FFmpeg（依赖 ffmpeg-kit）

两个脚本都依赖 [ffmpeg-kit](https://github.com/arthenica/ffmpeg-kit)，
首次运行会自动 `git clone` 到 `third_party/ffmpeg-kit`。

### Android

```bash
./scripts/build_ffmpeg_android.sh            # 构建 arm64-v8a + x86_64
./scripts/build_ffmpeg_android.sh arm64-v8a  # 仅构建单个 ABI
```

要点：

- 需要 Android NDK + SDK。脚本通过 `ANDROID_SDK_ROOT` 与自动探测的最高版本
  `ANDROID_NDK_ROOT` 定位工具链。
- 调用 ffmpeg-kit 的 `android.sh`，`--enable-gpl --no-archive`，并针对目标 ABI
  `--disable` 其它架构。
- 产物从 `prebuilt/android-<arch>/ffmpeg/` 拷贝到
  `third_party/android/<abi>/{include,lib}`。

### iOS

```bash
./scripts/build_ffmpeg_ios.sh
```

要点：

- 使用 `--min` 精简配置（H.264 + AAC），`--enable-gpl --enable-neon`，
  禁用 armv7/armv7s，仅保留 64 位。
- 产物输出到 `third_party/ios`。

## 2. Android 应用构建

配置见 `android/app/build.gradle`：

- `compileSdk 34` / `minSdk 26` / `targetSdk 34`，Java/Kotlin 目标 17。
- `ndkVersion '27.3.13750724'`。
- `externalNativeBuild` 指向 `src/main/jni/CMakeLists.txt`（CMake 3.22.1），
  `cppFlags "-std=c++17 -O2"`，`abiFilters "arm64-v8a"`，并通过
  `-DFFMPEG_DIR=.../third_party/android/arm64-v8a` 传入 FFmpeg 路径。

构建步骤（在 `android/` 目录下）：

```bash
./gradlew assembleDebug     # 或 assembleRelease
./gradlew installDebug      # 安装到已连接设备
```

Gradle 会自动调用 CMake 构建 native 部分：

1. `jni/CMakeLists.txt` 通过 `add_subdirectory(.../core)` 引入内核，
   编译出静态库 `ccplayer`。
2. 编译 JNI 桥接生成共享库 `ccplayer_jni.so`，链接
   `ccplayer`、`android`、`log`、`aaudio`、`EGL`、`GLESv3`。
3. `jniLibs/arm64-v8a/` 下的 FFmpeg `.so` 一并打包进 APK。

> 运行需要把测试视频放到 `assets/sample.mp4`（或通过 Intent extra
> `video_path` 指定外部路径），`MainActivity` 会将 asset 拷贝到缓存目录播放。

## 3. iOS 应用构建

- 源码位于 `ios/PlayerApp/`：`CCPlayer.h/.mm`（Objective-C++ 桥接）
  与 `PlayerViewController.swift`。
- 需先在 Xcode 工程中链接 `third_party/ios` 下的 FFmpeg 库，以及
  `OpenGLES`、`AudioToolbox` 框架（对应 `core/CMakeLists.txt` 中 `TARGET_IOS` 分支）。
- 使用 Xcode 打开工程后直接构建运行。

## 4. core 静态库（CMake 选项）

`core/CMakeLists.txt` 支持以下选项：

| 选项 | 默认 | 说明 |
|------|------|------|
| `BUILD_TESTS` | OFF | 构建单元测试（`tests/` 目录，轻量 harness，覆盖 Clock / AudioVideoSyncer / CommandQueue，可在桌面环境直接编译运行） |
| `TARGET_ANDROID` | OFF | 链接 Android 平台库（log / aaudio / OpenSLES） |
| `TARGET_IOS` | OFF | 链接 iOS 平台库（OpenGLES / AudioToolbox） |
| `FFMPEG_DIR` | 自动推断 | FFmpeg 头文件与库所在目录 |

链接的基础 FFmpeg 库：`avformat`、`avcodec`、`avutil`、`swresample`。

## 常见问题

- **找不到 FFmpeg 头/库**：确认已运行对应平台的 FFmpeg 构建脚本，且
  `FFMPEG_DIR` 指向含 `include/` 与 `lib/` 的目录；Android 下注意 ABI 子目录。
- **NDK 版本不匹配**：`build.gradle` 固定了 `ndkVersion`，请在 SDK Manager 中安装该版本。
- **无 arm64-v8a 以外的设备**：当前 `abiFilters` 仅含 `arm64-v8a`，如需模拟器
  支持 x86_64，需在 `build.gradle` 中追加并用脚本构建对应 ABI 的 FFmpeg。
