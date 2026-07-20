#!/usr/bin/env bash
# Linux port of setup-vendor.ps1 — sparse-clone the needed subset of STM32CubeF4
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
dst="$root/vendor/STM32CubeF4"

mkdir -p "$root/vendor"

if [ ! -d "$dst" ]; then
    git clone --depth=1 --filter=blob:none --sparse \
        https://github.com/STMicroelectronics/STM32CubeF4.git "$dst"
fi

cd "$dst"

git sparse-checkout disable
git sparse-checkout init --no-cone
git sparse-checkout set \
    'Drivers/CMSIS/Device/ST/STM32F4xx/*' \
    'Drivers/CMSIS/Include/*' \
    'Drivers/STM32F4xx_HAL_Driver/*' \
    'Drivers/BSP/STM32F429I-Discovery/*' \
    'Drivers/BSP/Components/Common/*' \
    'Drivers/BSP/Components/ili9341/*' \
    'Utilities/Fonts/*'
git checkout

git submodule update --init --depth=1 -- \
    Drivers/CMSIS/Device/ST/STM32F4xx \
    Drivers/STM32F4xx_HAL_Driver \
    Drivers/BSP/STM32F429I-Discovery \
    Drivers/BSP/Components/Common \
    Drivers/BSP/Components/ili9341
