# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "F:/Espressif/frameworks/esp-idf-v5.1.2/components/bootloader/subproject"
  "F:/esp-workspace/esp32_display_spi_dma/build/bootloader"
  "F:/esp-workspace/esp32_display_spi_dma/build/bootloader-prefix"
  "F:/esp-workspace/esp32_display_spi_dma/build/bootloader-prefix/tmp"
  "F:/esp-workspace/esp32_display_spi_dma/build/bootloader-prefix/src/bootloader-stamp"
  "F:/esp-workspace/esp32_display_spi_dma/build/bootloader-prefix/src"
  "F:/esp-workspace/esp32_display_spi_dma/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "F:/esp-workspace/esp32_display_spi_dma/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "F:/esp-workspace/esp32_display_spi_dma/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
