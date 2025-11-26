#!/bin/bash

# --- Configuration & Paths ---
KERNEL_ROOT=$(pwd)
KERNEL_NAME="Acacia"
DATE=$(date +"%Y%m%d")

# Directories relative to where the script is run
TOOLCHAIN_PARENT_DIR="$KERNEL_ROOT/toolchains"
LLVM_DIR="$TOOLCHAIN_PARENT_DIR/neutron-clang"
OUT_DIR="$KERNEL_ROOT/out"
# Assumes you have cloned AnyKernel3 into this folder name
ANYKERNEL_DIR="$KERNEL_ROOT/AnyKernel3" 

# --- Helper for logging ---
info() {
    echo -e "\n\e[1;36m==>\e[0m \e[1m$1\e[0m"
}

# --- Dependency Check ---
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

# --- Toolchain Installation ---
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

# --- Environment Setup ---

# Clean PATH to avoid duplicates
PATH="$LLVM_PATH:$PATH"

HOST_BUILD_ENV="ARCH=arm64 \
                CC=clang \
                CROSS_COMPILE=aarch64-linux-gnu- \
                LLVM=1 \
                LLVM_IAS=1"

KERNEL_MAKE_ENV="DTC_EXT=$KERNEL_ROOT/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"

# --- Build Start ---
echo "*****************************************"
echo "  Cleaning Output Directory"
echo "*****************************************"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Generate Defconfig
make O="$OUT_DIR" $HOST_BUILD_ENV vendor/kona-not_defconfig vendor/samsung/kona-sec-not.config vendor/samsung/r8q.config vendor/samsung/nh.config

echo "*****************************************"
echo "  Building Device Tree (DTBO)"
echo "*****************************************"

make -j$(nproc) O="$OUT_DIR" $KERNEL_MAKE_ENV $HOST_BUILD_ENV \
    CC="clang --target=aarch64-linux-gnu" dtbo.img

echo "*****************************************"
echo "  Building Kernel Image"
echo "*****************************************"

make -j$(nproc) O="$OUT_DIR" $KERNEL_MAKE_ENV $HOST_BUILD_ENV \
    CC="clang --target=aarch64-linux-gnu" Image

# --- Packaging ---

info "Packaging Kernel"

if [ ! -d "$ANYKERNEL_DIR" ]; then
    echo -e "\e[1;31mError: AnyKernel3 directory not found at $ANYKERNEL_DIR\e[0m"
    echo "Please clone your device's AnyKernel3 repo into this directory."
    exit 1
fi

# 1. Clean previous build artifacts from AnyKernel3 (but keep the scripts!)
rm -f "$ANYKERNEL_DIR/Image"
rm -f "$ANYKERNEL_DIR/dtbo.img"
rm -f "$ANYKERNEL_DIR/dtb"
rm -f "$ANYKERNEL_DIR"/*.zip

# 2. Copy new artifacts
if [ -f "$OUT_DIR/arch/arm64/boot/Image" ]; then
    cp "$OUT_DIR/arch/arm64/boot/Image" "$ANYKERNEL_DIR/Image"
else
    echo -e "\e[1;31mError: Image not found. Build failed?\e[0m"
    exit 1
fi

if [ -f "$OUT_DIR/arch/arm64/boot/dtbo.img" ]; then
    cp "$OUT_DIR/arch/arm64/boot/dtbo.img" "$ANYKERNEL_DIR/dtbo.img"
fi

# Concatenate DTBs
cat "$OUT_DIR"/arch/arm64/boot/dts/vendor/qcom/*.dtb > "$ANYKERNEL_DIR/dtb"

# 3. Zip it up
gitsha=$(git rev-parse --short HEAD)
ZIP_NAME="not_kernel-${KERNEL_NAME}-${gitsha}-${DATE}.zip"

cd "$ANYKERNEL_DIR" || exit 1

# Zip everything in the folder recursively
zip -r9 "$ZIP_NAME" * -x .git README.md *placeholder

# 4. Move Zip to Root
mv "$ZIP_NAME" "$KERNEL_ROOT/"

echo "*****************************************"
echo " Build Complete!"
echo " Zip located at: $KERNEL_ROOT/$ZIP_NAME"
echo "*****************************************"
