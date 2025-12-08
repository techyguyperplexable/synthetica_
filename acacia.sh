#!/bin/bash

KERNEL_ROOT=$(pwd)
KERNEL_NAME="buckshot"
DATE=$(date +"%Y%m%d")
LOG_FILE="$KERNEL_ROOT/build.log"

TOOLCHAIN_PARENT_DIR="$KERNEL_ROOT/toolchains"
LLVM_DIR="$TOOLCHAIN_PARENT_DIR/neutron-clang"
OUT_DIR="$KERNEL_ROOT/out"
ANYKERNEL_DIR="$KERNEL_ROOT/AnyKernel3" 

# --- Telegram Functions ---

tg_get_msg_id() {
    # Sends initial message and returns the Message ID
    if [ -n "$TG_BOT_TOKEN" ]; then
        RES=$(curl -s -X POST "https://api.telegram.org/bot$TG_BOT_TOKEN/sendMessage" \
            -d chat_id="$TG_CHAT_ID" \
            -d text="🚀 *Initializing Build...*" \
            -d parse_mode="Markdown")
        echo "$RES" | jq -r '.result.message_id'
    fi
}

tg_update_loop() {
    # This runs in the background. 
    # $1 = PID of the make process
    # $2 = Message ID to edit
    MAKE_PID=$1
    MSG_ID=$2

    if [ -z "$TG_BOT_TOKEN" ] || [ -z "$MSG_ID" ]; then return; fi

    while kill -0 "$MAKE_PID" 2>/dev/null; do
        # Grab last 10 lines of log
        LOG_TAIL=$(tail -n 10 "$LOG_FILE")
        
        # Build JSON payload safely using jq to handle special chars/newlines
        # We use a timestamp to force the API to accept the edit (content must change)
        TIMESTAMP=$(date +"%H:%M:%S")
        
        JSON_PAYLOAD=$(jq -n \
            --arg chat_id "$TG_CHAT_ID" \
            --arg msg_id "$MSG_ID" \
            --arg text "🔨 *Building Kernel...* [$TIMESTAMP]
\`\`\`
$LOG_TAIL
\`\`\`" \
            '{chat_id: $chat_id, message_id: $msg_id, text: $text, parse_mode: "Markdown"}')

        curl -s -X POST "https://api.telegram.org/bot$TG_BOT_TOKEN/editMessageText" \
            -H "Content-Type: application/json" \
            -d "$JSON_PAYLOAD" > /dev/null

        # Telegram limits edits. 3-5 seconds is safe.
        sleep 4
    done
}

tg_err() {
    # If error, reply to the live message with the log
    if [ -n "$TG_BOT_TOKEN" ]; then
        curl -s -X POST "https://api.telegram.org/bot$TG_BOT_TOKEN/sendMessage" \
            -d chat_id="$TG_CHAT_ID" \
            -d reply_to_message_id="$LIVE_MSG_ID" \
            -d text="❌ *Build Failed!* Uploading log..." \
            -d parse_mode="Markdown" > /dev/null
        
        curl -s -F chat_id="$TG_CHAT_ID" \
            -F document=@"$LOG_FILE" \
            -F caption="Error Log" \
            "https://api.telegram.org/bot$TG_BOT_TOKEN/sendDocument" > /dev/null
    fi
    exit 1
}

# --- Main Script ---

info() {
    echo -e "\n\e[1;36m==>\e[0m \e[1m$1\e[0m"
}

info "Checking for build dependencies"
DEPS=("curl" "jq" "tar" "zstd" "zip")
missing_deps=0
for dep in "${DEPS[@]}"; do
    if ! command -v "$dep" &> /dev/null; then
        echo -e "\e[1;31mError: Required command '$dep' is not installed.\e[0m"
        missing_deps=1
    fi
done

if [ "$missing_deps" -eq 1 ]; then
    echo -e "\e[1;31mPlease install the missing dependencies and try again.\e[0m"
    exit 1
fi

# Send the initial message and grab its ID
LIVE_MSG_ID=$(tg_get_msg_id)

info "Setting up toolchain"
LLVM_PATH="$LLVM_DIR/bin/"
API_URL="https://api.github.com/repos/Neutron-Toolchains/clang-build-catalogue/releases/latest"
TEMP_ARCHIVE_PATH="$TOOLCHAIN_PARENT_DIR/neutron-clang.tar.zst"

if [ -d "$LLVM_DIR/bin" ]; then
    info "Neutron Clang toolchain found at $LLVM_DIR"
else
    info "Neutron Clang not found. Downloading latest release..."
    mkdir -p "$LLVM_DIR"
    
    DOWNLOAD_URL=$(curl -sL "$API_URL" | \
                   jq -r '.assets[] | select(.name | startswith("neutron-clang-") and endswith(".tar.zst")) | .browser_download_url')

    if [ -z "$DOWNLOAD_URL" ] || [ "$DOWNLOAD_URL" == "null" ]; then
        echo -e "\e[1;31mError: Could not find download URL.\e[0m"
        exit 1
    fi

    echo "Downloading from: $DOWNLOAD_URL"
    if ! curl -L "$DOWNLOAD_URL" -o "$TEMP_ARCHIVE_PATH"; then
        echo -e "\e[1;31mError: Download failed.\e[0m"
        rm -f "$TEMP_ARCHIVE_PATH"
        exit 1
    fi
    
    info "Extracting toolchain..."
    if ! tar -I 'zstd' -xvf "$TEMP_ARCHIVE_PATH" -C "$LLVM_DIR" --strip-components=1; then
        echo -e "\e[1;31mError: Extraction failed.\e[0m"
        rm -rf "$LLVM_DIR"
        exit 1
    fi
    rm -f "$TEMP_ARCHIVE_PATH"
    info "Toolchain installed."
fi

PATH="$LLVM_PATH:$PATH"

HOST_BUILD_ENV="ARCH=arm64 \
                CC=clang \
                CROSS_COMPILE=aarch64-linux-gnu- \
                LLVM=1 \
                LLVM_IAS=1"

KERNEL_MAKE_ENV="DTC_EXT=$KERNEL_ROOT/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"

echo "*****************************************"
echo "  Cleaning Output Directory"
echo "*****************************************"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
rm -f "$LOG_FILE"
touch "$LOG_FILE"

# --- CONFIG STAGE ---
# Run config, logging to file
make O="$OUT_DIR" $HOST_BUILD_ENV vendor/kona-not_defconfig vendor/samsung/kona-sec-not.config vendor/samsung/r8q.config vendor/samsung/nh.config vendor/samsung/lindroid.config >> "$LOG_FILE" 2>&1
if [ $? -ne 0 ]; then tg_err; fi

# --- BUILD STAGE (DTBO + IMAGE) ---
# We combine these into one logging block for smoother Telegram updates

echo "Starting Compilation..."

# Start the actual build in background to capture PID
(
    echo "--- Building DTBO ---"
    make -j$(nproc) O="$OUT_DIR" $KERNEL_MAKE_ENV $HOST_BUILD_ENV CC="clang --target=aarch64-linux-gnu" dtbo.img
    
    if [ $? -eq 0 ]; then
        echo "--- Building Image ---"
        make -j$(nproc) O="$OUT_DIR" $KERNEL_MAKE_ENV $HOST_BUILD_ENV CC="clang --target=aarch64-linux-gnu" Image
    else
        exit 1
    fi
) >> "$LOG_FILE" 2>&1 &

BUILD_PID=$!

# Start the Telegram Updater in parallel
tg_update_loop "$BUILD_PID" "$LIVE_MSG_ID"

# Wait for build to finish
wait $BUILD_PID
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    tg_err
fi

# --- PACKAGING ---
info "Packaging Kernel"

if [ ! -d "$ANYKERNEL_DIR" ]; then
    echo -e "\e[1;31mError: AnyKernel3 directory not found at $ANYKERNEL_DIR\e[0m"
    exit 1
fi

rm -f "$ANYKERNEL_DIR/Image"
rm -f "$ANYKERNEL_DIR/dtbo.img"
rm -f "$ANYKERNEL_DIR/dtb"
rm -f "$ANYKERNEL_DIR"/*.zip

if [ -f "$OUT_DIR/arch/arm64/boot/Image" ]; then
    cp "$OUT_DIR/arch/arm64/boot/Image" "$ANYKERNEL_DIR/Image"
else
    echo -e "\e[1;31mError: Image not found. Build failed?\e[0m"
    exit 1
fi

if [ -f "$OUT_DIR/arch/arm64/boot/dtbo.img" ]; then
    cp "$OUT_DIR/arch/arm64/boot/dtbo.img" "$ANYKERNEL_DIR/dtbo.img"
fi

cat "$OUT_DIR"/arch/arm64/boot/dts/vendor/qcom/*.dtb > "$ANYKERNEL_DIR/dtb"

gitsha=$(git rev-parse --short HEAD)
ZIP_NAME="Acacia-${KERNEL_NAME}-${gitsha}-${DATE}.zip"

cd "$ANYKERNEL_DIR" || exit 1
zip -r9 "$ZIP_NAME" * -x .git README.md *placeholder
mv "$ZIP_NAME" "$KERNEL_ROOT/"

echo "*****************************************"
echo " Build Complete!"
echo " Zip located at: $KERNEL_ROOT/$ZIP_NAME"
echo "*****************************************"

if [ -n "$TG_BOT_TOKEN" ]; then
    # Delete the "Live" message or edit it to say done (Optional, here we just upload result)
    # We upload the file now
    LOG=$(git log --pretty=format:"%h: %s" -n 5)
    
    curl -s -F chat_id="$TG_CHAT_ID" \
         -F document=@"$KERNEL_ROOT/$ZIP_NAME" \
         -F caption="✅ *Build Complete!* $ZIP_NAME"$'"'\\n\\n'"'"$LOG" \
         -F parse_mode="Markdown" \
         "https://api.telegram.org/bot$TG_BOT_TOKEN/sendDocument" > /dev/null
         
    curl -s -F chat_id="$TG_CHAT_ID" \
         -F document=@"$OUT_DIR/.config" \
         "https://api.telegram.org/bot$TG_BOT_TOKEN/sendDocument" > /dev/null
fi
