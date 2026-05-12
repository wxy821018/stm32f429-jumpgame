$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dst  = Join-Path $root 'vendor\STM32CubeF4'

if (-not (Test-Path (Join-Path $root 'vendor'))) {
    New-Item -ItemType Directory -Path (Join-Path $root 'vendor') | Out-Null
}

if (-not (Test-Path $dst)) {
    git clone --depth=1 --filter=blob:none --sparse `
        https://github.com/STMicroelectronics/STM32CubeF4.git $dst
}

Set-Location $dst

git sparse-checkout disable
git sparse-checkout init --no-cone
git sparse-checkout set `
    'Drivers/CMSIS/Device/ST/STM32F4xx/*' `
    'Drivers/CMSIS/Include/*' `
    'Drivers/STM32F4xx_HAL_Driver/*' `
    'Drivers/BSP/STM32F429I-Discovery/*' `
    'Drivers/BSP/Components/Common/*' `
    'Drivers/BSP/Components/ili9341/*' `
    'Utilities/Fonts/*'
git checkout

git submodule update --init --depth=1 -- `
    Drivers/CMSIS/Device/ST/STM32F4xx `
    Drivers/STM32F4xx_HAL_Driver `
    Drivers/BSP/STM32F429I-Discovery `
    Drivers/BSP/Components/Common `
    Drivers/BSP/Components/ili9341
