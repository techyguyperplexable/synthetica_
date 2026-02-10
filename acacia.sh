#!/bin/bash
set -e

export LC_ALL=C
export KBUILD_BUILD_TIMESTAMP=$(date -u "+%a %b %d %H:%M:%S UTC %Y")
export KBUILD_BUILD_USER="Z3phery"
export KBUILD_BUILD_HOST="Archlinux"

if [ -f .gitmodules ]; then
  UNINITIALIZED_SUBMODULES=$(git submodule status | grep '^-' || true)
  
  if [ -n "$UNINITIALIZED_SUBMODULES" ]; then
    echo "The following submodules are missing or uninitialized:"
    echo "$UNINITIALIZED_SUBMODULES"
    echo "Initializing and cloning submodules..."
    git submodule update --init --recursive
    if [ $? -eq 0 ]; then
      echo "Submodules initialized and cloned successfully."
    else
      echo "Failed to clone submodules. Please check your repository configuration."
      exit 1
    fi
  else
    echo "All submodules are already initialized."
  fi
else
  echo "No submodules found in this repository."
fi

VALID_MODELS=("bloomxq" "c1q" "c2q" "f2q" "gts7l" "gts7lwifi" "gts7xl" "gts7xlwifi" "r8q" "x1q" "y2q" "z3q")

EUR_MODELS=("r8q" "gts7l" "gts7lwifi" "gts7xl" "gts7xlwifi" "f2q" "bloomxq")

VALID_REGIONS=("eur" "kor" "chn" "usa")

prompt() {
    echo "=============================================="
    echo "$1"
    echo "=============================================="
    shift
    for option in "$@"; do
        echo "$option"
    done
    echo "=============================================="
}

validate_choice() {
    local choice="$1"
    shift
    local valid_values=("$@")
    
    for value in "${valid_values[@]}"; do
        if [[ "$choice" == "$value" ]]; then
            return 0
        fi
    done
    
    return 1
}

prompt "Which project do you want to build?" "${VALID_MODELS[@]}"
read -p " - Enter your choice: " model_choice
model_choice=$(echo "$model_choice" | tr '[:upper:]' '[:lower:]')

if ! validate_choice "$model_choice" "${VALID_MODELS[@]}"; then
    echo "Invalid model choice! Exiting."
    exit 1
fi

if [[ "$region_choice" == "eur" && " ${EUR_MODELS[*]} " =~ " $model_choice " ]]; then
    echo "=============================================="
    echo "Error: This project doesn't support the EUR region."
    echo "=============================================="
    exit 1
fi

yes_no_prompt() {
    local var_name=$1
    local message=$2
    prompt "$message" "yes" "no (default)"
    read -p " - Enter your choice: " choice
    choice=$(echo "$choice" | tr '[:upper:]' '[:lower:]')
    
    if [[ "$choice" == "y" || "$choice" == "yes" ]]; then
        eval "$var_name=true"
    else
        eval "$var_name=false"
    fi
}

yes_no_prompt "PERMISSIVE" "Would you like to force Selinux to permissive?"

echo "=============================================="
echo "Configuration Summary:"
echo "Model: $model_choice"
echo "Region: ${region_choice:-default}"
echo "=============================================="

PRODUCT_OUT=out
KERNEL_DIR=$(pwd)
BUILD_ROOT_DIR=$KERNEL_DIR/..
KERNEL_OUT_DIR=$PRODUCT_OUT/obj/KERNEL_OBJ
ANYKERNEL_DIR="$KERNEL_DIR/AnyKernel3"

if ! [ -d "$KERNEL_OUT_DIR" ]; then
    echo "Creating output directory: $KERNEL_OUT_DIR"
    mkdir -p "$KERNEL_OUT_DIR" || { echo "Error: Failed to create KERNEL_OUT_DIR. Exiting."; exit 1; }
fi

MODEL=$model_choice
REGION=$region_choice
CHIPSET_NAME=kona
KERNEL_ARCH=arm64

export PROJECT_NAME="${MODEL}"
[ -z "${PLATFORM_VERSION}" ] && export PLATFORM_VERSION=11

KERNEL_DEFCONFIG="vendor/${CHIPSET_NAME}-queenX_defconfig"
COMMON_DEFCONFIG="vendor/samsung/kona-sec-common.config"

if [ -n "$REGION" ]; then
    PROJECT_CONFIG="vendor/samsung/${MODEL}_${REGION}.config"
else
    PROJECT_CONFIG="vendor/samsung/${MODEL}.config"
fi

if [ "$PERMISSIVE" = true ]; then
    SLNX_DEFCONFIG="vendor/permissive.config"
fi

if [ ! -d "/home/ignacio/toolchains/clang-r536225/bin" ]; then
    echo "Error: AOSP toolchain directories not found. Exiting."
    exit 1
fi

PATH="/home/ignacio/toolchains/clang-r536225/bin:${PATH}"
KERNEL_LLVM_BIN="/home/ignacio/toolchains/clang-r536225/bin/clang"

export CC="ccache clang"
export LLVM=1
export LLVM_IAS=1
export DTC_OVERLAY_TEST_EXT="$KERNEL_DIR/tools/ufdt_apply_overlay"

BUILD_JOB_NUMBER=$(grep -c processor /proc/cpuinfo)

FUNC_BUILD_KERNEL() {
    local __dts_dir="${KERNEL_OUT_DIR}/arch/${KERNEL_ARCH}/boot/dts"

    echo ""
    echo "=============================================="
    echo "Starting: FUNC_BUILD_KERNEL"
    echo "=============================================="
    echo "Build Info"
    echo "=============================================="
    echo "Project: $PROJECT_NAME"
    echo "Common config: $KERNEL_DEFCONFIG"
    echo "Project config: $PROJECT_CONFIG"
    echo "Extra included configs: "$KSU_DEFCONFIG $SLNX_DEFCONFIG""
    echo "Output directory: $PRODUCT_OUT"
    echo "=============================================="
    echo ""

    make -C "$KERNEL_DIR" O="$KERNEL_OUT_DIR" $KERNEL_MAKE_PARAM ARCH="$KERNEL_ARCH" \
        $KERNEL_DEFCONFIG \
        $COMMON_DEFCONFIG \
        $PROJECT_CONFIG \
        $KSU_DEFCONFIG \
        $SLNX_DEFCONFIG

    make -C "$KERNEL_DIR" O="$KERNEL_OUT_DIR" -j"$BUILD_JOB_NUMBER" $KERNEL_MAKE_PARAM ARCH="$KERNEL_ARCH"

    cat "$__dts_dir/vendor/qcom"/*.dtb > "$PRODUCT_OUT/dtb.img"

    rm -rf "$__dts_dir/samsung/*"

    cp "$KERNEL_OUT_DIR/arch/arm64/boot/dtbo.img" "$PRODUCT_OUT"

    rsync -cv "$KERNEL_OUT_DIR/arch/arm64/boot/Image" "$PRODUCT_OUT/Image"

    ls -al "$PRODUCT_OUT/Image"

    echo ""
    echo "================================="
    echo "Ending: FUNC_BUILD_KERNEL"
    echo "================================="
    echo ""
}

FUNC_MAKE_ZIP() {
    local __dts_dir="${KERNEL_OUT_DIR}/arch/${KERNEL_ARCH}/boot/dts"
    
    echo "=============================================="
    echo "Preparing zip..."
    echo "=============================================="

    if [ ! -d "$ANYKERNEL_DIR" ]; then
        echo "Error: AnyKernel3 directory not found at $ANYKERNEL_DIR"
        exit 1
    fi

    rm -f "$ANYKERNEL_DIR/Image" "$ANYKERNEL_DIR/kona.dtb" "$ANYKERNEL_DIR/dtbo.img" "$ANYKERNEL_DIR"/*.zip

    cp "$PRODUCT_OUT/Image" "$ANYKERNEL_DIR/Image"
    cp "$PRODUCT_OUT/dtbo.img" "$ANYKERNEL_DIR/dtbo.img"

    if [ -d "$__dts_dir/vendor/qcom" ]; then
        cat "$__dts_dir/vendor/qcom"/*.dtb > "$ANYKERNEL_DIR/kona.dtb"
        echo "kona.dtb generated from vendor/qcom dtbs."
    else
        echo "Warning: No DTBs found to generate kona.dtb"
    fi

    build_date=$(date +"%Y%m%d")
    gitsha=$(git rev-parse --short HEAD)
    
    ZIP_NAME="queenX-perf-UI-${MODEL}-${gitsha}-${build_date}.zip"

    cd "$ANYKERNEL_DIR" || exit 1
    
    echo -e "Zipping: $ZIP_NAME"

    zip -r9 "$ZIP_NAME" . -x ".git*" -x "README.md" -x "*placeholder" -x ".gitignore" -x ".github"

    mv "$ZIP_NAME" ../
    
    echo " "
    echo "Build finished successfully: $ZIP_NAME"
    cd "$KERNEL_DIR"
}

(
    FUNC_BUILD_KERNEL
    FUNC_MAKE_ZIP
)
