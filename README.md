# Open Hearing DSP

个人音频实验。用 Android 麦克风做实时监听，再加上均衡、压缩和多段动态。**不是医疗器械，不能替代助听器。** dBFS 不能换成耳机声压级。

界面和文档是中文。工程是 Kotlin、Jetpack Compose Material 3、C++17、NDK、Google Oboe 1.11.0。

## 现在有什么

底部是 **监听｜测量｜我的预设**。原来的监测、均衡、动态、设备和实验都在右上角 **高级设置**，没有删。

监听默认固定手机内置麦克风（立体声双麦、双麦合成或仅顶部），输出跟随系统，但扬声器仍要单独确认，路由变化会在原生侧停掉。安全限幅不能关。测量用更严的 -18 dBFS 上限。自动均衡和压缩来自同一份 C++ 拟合，每个参数带来源标签。频谱降噪默认关。

声学延迟和这台小米 15 Pro 上的听感都还没测。dBFS 不是耳机声压级。

说明在 `docs/`：算法与拟合、校准与输入、DSP 与延迟、测试记录、需求对照、小米 15 Pro 指南、已知问题。

## 构建

```bash
bash scripts/setup-android-sdk.sh
bash scripts/run-host-tests.sh
export ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
./gradlew :app:assembleDebug :app:testDebugUnitTest :app:lintDebug
```

Debug APK：`app/build/outputs/apk/debug/app-debug.apk`（arm64-v8a）。

说明在 `docs/`：环境、DSP 与延迟、测试记录、需求对照、小米 15 Pro 指南、已知问题。Oboe 源码在 `third_party/oboe`。SDK 不进仓库，用脚本按版本安装。
