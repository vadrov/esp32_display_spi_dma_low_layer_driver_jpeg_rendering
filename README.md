 Copyright (C) 2019-2024, VadRov, all right reserved.
 
 - ESP32 Low-level driver for spi displays (esp-idf-v5.1.2)
 - Optimized JPEG decoder.
 - Demonstration of line-by-line graphics rendering running on two cpu cores.

 ```c
//--------------------------------------------- User defines ---------------------------------------------
#define CS_PIN   		17		// -1 if not used
#define DC_PIN   		21
#define RST_PIN  		19
#define BCKL_PIN 		5
#define SPI_			SPI3 	//SPI2 (GPIO13 -> MOSI, GPIO14 -> CLK)
								//SPI3 (GPIO23 -> MOSI, GPIO18 -> CLK)
#define DMA_ch			1 		//DMA channel 1 or 2, 0 - if DMA not used

#define ACT_DISPLAY		ST7789  //ST7789 or ILI9341
#define HI_SPEED				//if uncommented f_clk spi = 80 MHz, else 40 MHz
//--------------------------------------------------------------------------------------------------------
```

 https://www.youtube.com/@VadRov
 https://dzen.ru/vadrov
 https://vk.com/vadrov
 https://t.me/vadrov_channel
