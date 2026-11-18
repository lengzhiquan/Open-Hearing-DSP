#!/usr/bin/env bash
# Pins the SDK pieces this project was built with. Requires curl and unzip.
set -euo pipefail
SDK="${ANDROID_HOME:-$HOME/android-sdk}"
mkdir -p "$SDK/cmdline-tools"
if [[ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" && ! -x "$SDK/cmdline-tools/latest/bin/android" ]]; then
  tmp="$(mktemp -d)"
  curl -fL -o "$tmp/cmdtools.zip" "https://dl.google.com/android/repository/commandlinetools-linux-16111833_latest.zip"
  unzip -q "$tmp/cmdtools.zip" -d "$SDK/cmdline-tools"
  rm -rf "$SDK/cmdline-tools/latest"
  mv "$SDK/cmdline-tools/cmdline-tools" "$SDK/cmdline-tools/latest"
fi
export ANDROID_HOME="$SDK"
android="$SDK/cmdline-tools/latest/bin/android"
"$android" sdk install --sdk "$SDK" \
  "platform-tools" \
  "platforms;android-35" \
  "platforms;android-36" \
  "build-tools;35.0.1" \
  "build-tools;36.0.0" \
  "ndk;28.2.13676358" \
  "cmake;3.22.1"
echo "sdk.dir=$SDK" > local.properties
echo "SDK ready at $SDK"
