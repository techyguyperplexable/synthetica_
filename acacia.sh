#!/bin/bash

# --- Configuration ---
KERNEL_ROOT=$(pwd)
KERNEL_NAME="eros"
DATE=$(date +"%Y%m%d")
LOG_FILE="$KERNEL_ROOT/build.log"
LAST_SHA_FILE="$KERNEL_ROOT/.acacia_last_sha"

# Directories
TOOLCHAIN_PARENT_DIR="$KERNEL_ROOT/toolchains"
LLVM_DIR="$TOOLCHAIN_PARENT_DIR/clang-r530567"
OUT_DIR="$KERNEL_ROOT/out"
ANYKERNEL_DIR="$KERNEL_ROOT/AnyKernel3" 

# --- Build Prompt Selection ---
info() { echo -e "\n\e[1;36m==>\e[0m \e[1m$1\e[0m"; }

# Check for non-interactive mode (pass SELINUX and VARIANT as env vars)
if [ -n "$ACACIA_SELINUX" ] && [ -n "$ACACIA_VARIANT" ]; then
    SELINUX_CHOICE="$ACACIA_SELINUX"
    VARIANT_CHOICE="$ACACIA_VARIANT"
else
    # 1. SELinux Mode
    info "Select SELinux mode:"
    echo "1) Enforcing"
    echo "2) Permissive"
    read -p "Choice [1/2]: " SELINUX_CHOICE

    # 2. Build Variant (Oplus vs Normal)
    info "Select Build Variant:"
    echo "1) Normal"
    echo "2) Oplus"
    read -p "Choice [1/2]: " VARIANT_CHOICE
fi

case "$SELINUX_CHOICE" in
    2)
        SELINUX_MODE="permissive"
        SELINUX_CONFIG="vendor/samsung/permissive.config"
        ;;
    *)
        SELINUX_MODE="enforcing"
        SELINUX_CONFIG="vendor/samsung/enforcing.config"
        ;;
esac

case "$VARIANT_CHOICE" in
    2)
        IS_OPLUS=true
        EXTRA_CONFIG="vendor/oplus.config" 
        ;;
    *)
        IS_OPLUS=false
        EXTRA_CONFIG=""
        ;;
esac

info "Building Variant: $([ "$IS_OPLUS" = true ] && echo "Oplus" || echo "Normal") | SELinux: $SELINUX_MODE" 

# --- Telegram Functions ---
tg_msg() {
    [ -z "$TG_BOT_TOKEN" ] && return
    curl -s -X POST "https://api.telegram.org/bot$TG_BOT_TOKEN/sendMessage" \
        -d chat_id="$TG_CHAT_ID" \
        -d text="$1" \
        -d parse_mode="Markdown" > /dev/null
}

tg_start_monitor() {
    [ -z "$TG_BOT_TOKEN" ] && return
    
    # Send initial message
    RES=$(curl -s -X POST "https://api.telegram.org/bot$TG_BOT_TOKEN/sendMessage" \
        -d chat_id="$TG_CHAT_ID" \
        -d text="Build initiated: $KERNEL_NAME" \
        -d parse_mode="Markdown")
        
    TG_LIVE_MSG_ID=$(echo "$RES" | jq -r '.result.message_id')
    
    # Start background loop
    (
        while true; do
            sleep 5
            if [ -f "$LOG_FILE" ]; then
                LOG_TAIL=$(tail -n 10 "$LOG_FILE")
                TIME=$(date +"%H:%M:%S")
                
                # Clean JSON payload
                JSON=$(jq -n \
                    --arg cid "$TG_CHAT_ID" \
                    --arg mid "$TG_LIVE_MSG_ID" \
                    --arg txt "Building... [$TIME]
\`\`\`
$LOG_TAIL
\`\`\`" \
                    '{chat_id: $cid, message_id: $mid, text: $txt, parse_mode: "Markdown"}')

                curl -s -X POST "https://api.telegram.org/bot$TG_BOT_TOKEN/editMessageText" \
                    -H "Content-Type: application/json" \
                    -d "$JSON" > /dev/null
            fi
        done
    ) &
    TG_MONITOR_PID=$!
}

tg_stop_monitor() {
    if [ -n "$TG_MONITOR_PID" ]; then
        kill "$TG_MONITOR_PID" 2>/dev/null
        wait "$TG_MONITOR_PID" 2>/dev/null
    fi
}

tg_upload_log() {
    [ -z "$TG_BOT_TOKEN" ] && return
    tg_msg "Build failed. Uploading log..."
    curl -s -F chat_id="$TG_CHAT_ID" \
         -F document=@"$LOG_FILE" \
         -F caption="Build Log (Failure)" \
         "https://api.telegram.org/bot$TG_BOT_TOKEN/sendDocument" > /dev/null
}

# Trap interrupts to ensure we never hang
trap 'tg_stop_monitor; echo "Build cancelled."; exit 130' INT

# --- Dependencies ---
info "Checking for build dependencies"
DEPS=("curl" "jq" "tar" "zstd" "zip")
for dep in "${DEPS[@]}"; do
    if ! command -v "$dep" &> /dev/null; then
        echo "Error: Required command '$dep' is not installed."
        exit 1
    fi
done

# --- Toolchain Setup ---
info "Setting up toolchain"
mkdir -p "$TOOLCHAIN_PARENT_DIR"
LLVM_PATH="$LLVM_DIR/bin"
CLANG_URL="https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/heads/main/clang-r530567.tar.gz"

if [ ! -f "$LLVM_PATH/clang" ]; then
    info "Downloading Google Clang 19 (r530567)..."
    rm -rf "$LLVM_DIR"
    mkdir -p "$LLVM_DIR"
    
    curl -L "$CLANG_URL" -o "$TOOLCHAIN_PARENT_DIR/clang.tar.gz" || { echo "Error: Download failed."; exit 1; }
    
    info "Extracting toolchain..."
    tar -xf "$TOOLCHAIN_PARENT_DIR/clang.tar.gz" -C "$LLVM_DIR" || { echo "Error: Extraction failed."; exit 1; }
    rm -f "$TOOLCHAIN_PARENT_DIR/clang.tar.gz"
fi

PATH="$LLVM_PATH:$PATH"
HOST_BUILD_ENV="ARCH=arm64 CC=clang CROSS_COMPILE=aarch64-linux-gnu- LLVM=1 LLVM_IAS=1"
KERNEL_MAKE_ENV="DTC_EXT=/usr/bin/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"

# --- Build Start ---
echo "Cleaning..."
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
rm -f "$LOG_FILE"
touch "$LOG_FILE"

# Start Monitor
tg_start_monitor

# Config
info "Generating config..."
# Added EXTRA_CONFIG (oplus.config) to the make command
make O="$OUT_DIR" $HOST_BUILD_ENV vendor/kona-perf_defconfig $SELINUX_CONFIG $EXTRA_CONFIG 2>&1 | tee -a "$LOG_FILE"
if [ ${PIPESTATUS[0]} -ne 0 ]; then tg_stop_monitor; tg_upload_log; exit 1; fi

# Compilation
info "Starting Compilation..."
echo "--- Building DTBO ---"
make -j$(nproc) O="$OUT_DIR" $KERNEL_MAKE_ENV $HOST_BUILD_ENV CC="clang --target=aarch64-linux-gnu" dtbo.img 2>&1 | tee -a "$LOG_FILE"
DTBO_STATUS=${PIPESTATUS[0]}

if [ $DTBO_STATUS -eq 0 ]; then
    echo "--- Building Image ---"
    make -j$(nproc) O="$OUT_DIR" $KERNEL_MAKE_ENV $HOST_BUILD_ENV CC="clang --target=aarch64-linux-gnu" Image 2>&1 | tee -a "$LOG_FILE"
    BUILD_STATUS=${PIPESTATUS[0]}
else
    BUILD_STATUS=1
fi

if [ $BUILD_STATUS -eq 0 ]; then
    tg_stop_monitor
else
    tg_stop_monitor
    echo "=== BUILD FAILED - Last 50 error lines ==="
    grep -i "error:" "$LOG_FILE" | tail -50
    tg_upload_log
    exit 1
fi

# --- Packaging ---
info "Packaging Kernel"

if [ ! -d "$ANYKERNEL_DIR" ]; then
    echo "Error: AnyKernel3 not found."
    exit 1
fi

rm -f "$ANYKERNEL_DIR/Image" "$ANYKERNEL_DIR/dtbo.img" "$ANYKERNEL_DIR/dtb" "$ANYKERNEL_DIR"/*.zip

if [ -f "$OUT_DIR/arch/arm64/boot/Image" ]; then
    cp "$OUT_DIR/arch/arm64/boot/Image" "$ANYKERNEL_DIR/Image"
else
    echo "Error: Image not found."
    exit 1
fi

[ -f "$OUT_DIR/arch/arm64/boot/dtbo.img" ] && cp "$OUT_DIR/arch/arm64/boot/dtbo.img" "$ANYKERNEL_DIR/dtbo.img"
cat "$OUT_DIR"/arch/arm64/boot/dts/vendor/qcom/*.dtb > "$ANYKERNEL_DIR/dtb"

SHORT_SHA=$(git rev-parse --short HEAD)

# Build the Zip suffix based on choices
ZIP_SUFFIX=""
if [ "$IS_OPLUS" = true ]; then
    ZIP_SUFFIX="${ZIP_SUFFIX}-oplus"
fi

if [ "$SELINUX_MODE" = "permissive" ]; then
    ZIP_SUFFIX="${ZIP_SUFFIX}-permissive"
fi

ZIP_NAME="Acacia-${KERNEL_NAME}-${SHORT_SHA}-${DATE}${ZIP_SUFFIX}.zip"

# Zip it
cd "$ANYKERNEL_DIR" || exit 1
zip -r9 "$ZIP_NAME" * -x .git README.md *placeholder
# Move back to root using absolute path
mv "$ZIP_NAME" "$KERNEL_ROOT/"
cd "$KERNEL_ROOT" || exit 1

echo "Build Complete: $ZIP_NAME"

# --- Upload Success ---
if [ -n "$TG_BOT_TOKEN" ]; then
    # Changelog Logic
    if [ -f "$LAST_SHA_FILE" ]; then
        LAST_SHA=$(cat "$LAST_SHA_FILE")
        CHANGELOG=$(git log --pretty=format:"%h: %s" "$LAST_SHA..HEAD")
        [ -z "$CHANGELOG" ] && CHANGELOG="No new commits."
    else
        CHANGELOG=$(git log --pretty=format:"%h: %s" -n 5)
    fi

    CAPTION="Build complete: ${ZIP_NAME}

${CHANGELOG}"

    # Upload Zip (with error checking and retry)
    info "Uploading $ZIP_NAME to Telegram..."
    for i in 1 2 3; do
        UPLOAD_RESULT=$(curl -s --max-time 300 -F chat_id="$TG_CHAT_ID" -F document=@"$ZIP_NAME" -F caption="$CAPTION" "https://api.telegram.org/bot$TG_BOT_TOKEN/sendDocument")
        if echo "$UPLOAD_RESULT" | jq -e '.ok == true' > /dev/null 2>&1; then
            echo "Upload successful!"
            break
        else
            echo "Upload attempt $i failed: $UPLOAD_RESULT"
            [ $i -lt 3 ] && sleep 5
        fi
    done
    
    # Upload Config (with error checking)
    CONFIG_RESULT=$(curl -s --max-time 60 -F chat_id="$TG_CHAT_ID" -F document=@"$OUT_DIR/.config" "https://api.telegram.org/bot$TG_BOT_TOKEN/sendDocument")
    if ! echo "$CONFIG_RESULT" | jq -e '.ok == true' > /dev/null 2>&1; then
        echo "Config upload failed: $CONFIG_RESULT"
    fi

    # Update Hash
    git rev-parse HEAD > "$LAST_SHA_FILE"
fi
