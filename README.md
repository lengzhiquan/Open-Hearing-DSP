# Open Hearing DSP

个人音频实验。用 Android 麦克风做实时监听，再加上均衡、压缩和多段动态。**不是医疗器械，不能替代助听器。** dBFS 不能换成耳机声压级。

界面和文档是中文。工程是 Kotlin、Jetpack Compose Material 3、C++17、NDK、Google Oboe 1.11.0。

## 现在有什么

五个页面：监测、均衡、动态、设备、实验。

监听由输出回调驱动，输入非阻塞读进固定环。启动静音，路由没核对不出声，扬声器必须单独确认，拔出或改路由会在原生侧停掉。安全限幅不能关。默认没有降噪、语音检测或回声消除。

均衡、压缩、4 到 8 段分频、预设和 JSON 导入导出都在这个 APK 里。声学延迟只能手填。真机测试没有做。

## 构建

```bash
bash scripts/setup-android-sdk.sh
bash scripts/run-host-tests.sh
export ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
./gradlew :app:assembleDebug :app:testDebugUnitTest :app:lintDebug
```

Debug APK：`app/build/outputs/apk/debug/app-debug.apk`（arm64-v8a）。

说明在 `docs/`：环境、DSP 与延迟、测试记录、需求对照、小米 15 Pro 指南、已知问题。Oboe 源码在 `third_party/oboe`。SDK 不进仓库，用脚本按版本安装。
