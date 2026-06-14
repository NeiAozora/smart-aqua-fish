#!/bin/bash
# setup-cordova.sh – SOURCE this script (source setup-cordova.sh)

echo "=========================================="
echo "  Cordova + Android 10 Setup"
echo "=========================================="

# ----- 1. Find REAL JAVA_HOME (JDK 11/17 recommended, but works with 23) -----
find_real_java_home() {
    local java_cmd=$(which java)
    # Resolve symlinks using readlink -f
    if command -v readlink >/dev/null && readlink -f "$java_cmd" >/dev/null 2>&1; then
        java_cmd=$(readlink -f "$java_cmd")
    else
        while [ -L "$java_cmd" ]; do
            local target=$(readlink "$java_cmd")
            if [[ "$target" != /* ]]; then
                target="$(dirname "$java_cmd")/$target"
            fi
            java_cmd="$target"
        done
    fi
    # Remove /bin/java or /bin/java.exe
    if [[ "$java_cmd" == */bin/java ]]; then
        echo "$(dirname "$(dirname "$java_cmd")")"
    elif [[ "$java_cmd" == */bin/java.exe ]]; then
        echo "$(dirname "$(dirname "$java_cmd")")"
    else
        # Fallback: search common locations
        for d in /c/Program\ Files/Java/* /c/Program\ Files\ \(x86\)/Java/*; do
            if [ -f "$d/bin/javac" ]; then
                echo "$d"
                return
            fi
        done
        echo ""
    fi
}

JAVA_HOME=$(find_real_java_home)
if [ -z "$JAVA_HOME" ]; then
    echo "❌ Could not detect JAVA_HOME. Please install JDK 11 or 17."
    return 1
fi
export JAVA_HOME
echo "✅ JAVA_HOME = $JAVA_HOME"

# Verify javac
if [ ! -f "$JAVA_HOME/bin/javac" ]; then
    echo "❌ javac not found. JAVA_HOME is incorrect."
    return 1
fi

# ----- 2. Set Android SDK paths (persistent) -----
export ANDROID_HOME="/d/Software/Android Studio"
export ANDROID_SDK_ROOT="$ANDROID_HOME"   # Cordova prefers this
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/tools:$ANDROID_HOME/tools/bin:$PATH"

echo "✅ ANDROID_HOME = $ANDROID_HOME"
echo "✅ ANDROID_SDK_ROOT = $ANDROID_SDK_ROOT"

# ----- 3. Locate sdkmanager.bat -----
SDKMANAGER=""
if [ -f "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager.bat" ]; then
    SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager.bat"
elif [ -f "$ANDROID_HOME/tools/bin/sdkmanager.bat" ]; then
    SDKMANAGER="$ANDROID_HOME/tools/bin/sdkmanager.bat"
else
    echo "❌ sdkmanager.bat not found. Install Android SDK Command-line Tools."
    return 1
fi
echo "✅ SDK Manager: $SDKMANAGER"

# Function to run sdkmanager (works in Git Bash)
run_sdkmanager() {
    # Convert POSIX path to Windows path for cmd.exe
    local win_path=$(echo "$1" | sed 's|/d/|D:\\|' | sed 's|/|\\|g')
    shift
    cmd /c "$win_path" "$@" 2>&1
}

# ----- 4. Install Android 10 (API 29) components -----
echo "📦 Installing Android 10 (API 29) components..."
run_sdkmanager "$SDKMANAGER" --update
run_sdkmanager "$SDKMANAGER" "platform-tools" "platforms;android-29" "build-tools;29.0.3" "build-tools;30.0.3"

echo "✅ Accepting licenses..."
# Auto-accept all licenses (suppress interactive prompts)
yes | run_sdkmanager "$SDKMANAGER" --licenses 2>/dev/null

# ----- 5. Cordova project setup -----
PROJECT_DIR="/d/Projects/web/smartfishfarm/SmartAquaFish"
if [ ! -d "$PROJECT_DIR" ]; then
    echo "❌ Project directory not found: $PROJECT_DIR"
    return 1
fi
cd "$PROJECT_DIR" || return 1

echo "📱 Setting up Cordova Android platform..."
cordova platform remove android 2>/dev/null
cordova platform add android@10.0.0

# Update config.xml for Android 10 (API 29) – also set compileSdkVersion
if [ -f "config.xml" ]; then
    if ! grep -q "android-targetSdkVersion" config.xml; then
        cp config.xml config.xml.backup
        # Insert after <widget> tag
        sed -i '/<widget/a\  <platform name="android">\n    <preference name="android-minSdkVersion" value="21" />\n    <preference name="android-targetSdkVersion" value="29" />\n    <preference name="android-compileSdkVersion" value="29" />\n  </platform>' config.xml
        echo "✅ config.xml updated for Android 10 (API 29)"
    else
        echo "✅ config.xml already contains Android preferences"
    fi
fi

# ----- 6. Write environment variables to ~/.bashrc (optional) -----
BASHRC="$HOME/.bashrc"
if ! grep -q "JAVA_HOME=" "$BASHRC" 2>/dev/null; then
    echo "💾 Adding environment variables to $BASHRC (persistent)"
    echo "export JAVA_HOME=\"$JAVA_HOME\"" >> "$BASHRC"
    echo "export ANDROID_HOME=\"$ANDROID_HOME\"" >> "$BASHRC"
    echo "export ANDROID_SDK_ROOT=\"$ANDROID_HOME\"" >> "$BASHRC"
    echo "export PATH=\"\$JAVA_HOME/bin:\$ANDROID_HOME/platform-tools:\$ANDROID_HOME/cmdline-tools/latest/bin:\$PATH\"" >> "$BASHRC"
    echo "✅ Environment variables added. Restart Git Bash or run 'source ~/.bashrc' to apply."
else
    echo "⚠️  Environment variables already present in $BASHRC (skipped)"
fi

# ----- 7. Final check -----
echo ""
echo "=========================================="
cordova requirements
echo "=========================================="
echo "✅ Setup complete!"
echo ""
echo "Next steps:"
echo "  1. Restart Git Bash OR run: source ~/.bashrc"
echo "  2. cd $PROJECT_DIR"
echo "  3. cordova build android"
echo "  4. cordova run android"
echo "=========================================="