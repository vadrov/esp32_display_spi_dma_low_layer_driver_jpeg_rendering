/*
 *	Драйвер управления дисплеями по SPI
 *  Author: VadRov
 *  Copyright (C) 2019 - 2022, VadRov, all right reserved.
 *
 *  Допускается свободное распространение.
 *  При любом способе распространения указание автора ОБЯЗАТЕЛЬНО.
 *  В случае внесения изменений и распространения модификаций указание первоначального автора ОБЯЗАТЕЛЬНО.
 *  Распространяется по типу "как есть", то есть использование осуществляется на свой страх и риск.
 *  Автор не предоставляет никаких гарантий.
 *
 *  Версия: 1.4 ESP32
 *
 *  https://www.youtube.com/@VadRov
 *  https://dzen.ru/vadrov
 *  https://vk.com/vadrov
 *  https://t.me/vadrov_channel
 */

#include "ili9341.h"
#include "display.h"
#include "esp_system.h"

static const uint8_t ili9341_init_str[] = {
			ILI9341_DISPLAYOFF, 0,
	 		ILI9341_POWERCONTROLB, 4, 0, 0x00, 0x83, 0x30,
			ILI9341_POWERSEQCONTROL, 4, 0x64, 0x03, 0x12, 0x81,
			ILI9341_DRIVERTIMCONTROLA, 3, 0x85, 0x01, 0x79,
			ILI9341_POWERCONTROLA, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
			ILI9341_PUMPRATIOCONTROL, 1, 0x20,
			ILI9341_DRIVERTIMCONTROLC, 2, 0x00, 0x00,
			ILI9341_POWERCONTROL1, 1, 0x26,
			ILI9341_POWERCONTROL2, 1, 0x11,
			ILI9341_VCOMCONTROL1, 2, 0x35, 0x3E,
			ILI9341_VCOMCONTROL2, 1, 0xBE,
			ILI9341_PIXELFORMAT, 1, ILI9341_COLOR_MODE_16bit,
			ILI9341_FRAMECONTROLNORMAL, 2, 0x00, 0x1B,
			ILI9341_ENABLE3G, 1, 0x08,
			ILI9341_GAMMASET, 1, 0x01,
			ILI9341_POSITIVEGAMMCORR, 15, 0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0x87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00,
			ILI9341_NEGATIVEGAMMCORR, 15, 0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F,
			ILI9341_ENTRYMODE, 1, 0x07,
			ILI9341_DISPLAYFUNC, 4, 0x0A, 0x82, 0x27, 0x00,
			ILI9341_TEARINGEFFECTON, 1, 1,
			ILI9341_SLEEPOUT, 0,
			0, 50,					//pause 50 ms
			ILI9341_DISPLAYON, 0,
			0, 120,					//pause 120 ms
			0, 255					//eof
		};

DRAM_ATTR static uint8_t ili9341_setwin_str[]  = {	ILI9341_COLADDRSET,  4, 0, 0, 0, 0,	//x0, x1
							 						ILI9341_PAGEADDRSET, 4, 0, 0, 0, 0,	//y0, y1
													ILI9341_MEMORYWRITE, 0,
													0, 255				//eof
												};

static const uint8_t ili9341_sleepin_str[]  = {	ILI9341_SLEEPIN, 0,
												0, 30,				//pause 30 ms
												0, 255				//eof
										  	  };

static const uint8_t ili9341_sleepout_str[] = {	ILI9341_SLEEPOUT, 0,
												0, 120,				//pause 120 ms
												0, 255				//eof
										  	  };

static uint8_t ili9341_orientation_str[] = { ILI9341_MEMCONTROL, 1, 0,	//mem_config (memory data access config)
											 0, 255					//eof string
											};

static uint8_t ILI9341_MemoryDataAccessControlConfig(uint8_t mirror_x, uint8_t mirror_y, uint8_t exchange_xy, uint8_t mirror_color, uint8_t refresh_v, uint8_t refresh_h)
{
	uint8_t mem_config = 0;
	if (mirror_x)		mem_config |= ILI9341_MADCTL_MX;
	if (mirror_y)		mem_config |= ILI9341_MADCTL_MY;
	if (exchange_xy)	mem_config |= ILI9341_MADCTL_MV;
	if (mirror_color)	mem_config |= ILI9341_MADCTL_BGR;
	if (refresh_v)		mem_config |= ILI9341_MADCTL_ML;
	if (refresh_h)		mem_config |= ILI9341_MADCTL_MH;
	return mem_config;
}

static uint8_t orient_to_memconfig(LCD_PageOrientation orientation)
{
	uint8_t mem_config = 0;
	if (orientation == PAGE_ORIENTATION_PORTRAIT) 				mem_config = ILI9341_MemoryDataAccessControlConfig(1, 0, 0, 1, 0, 0);
	else if (orientation == PAGE_ORIENTATION_PORTRAIT_MIRROR) 	mem_config = ILI9341_MemoryDataAccessControlConfig(0, 1, 0, 1, 1, 1);
	else if (orientation == PAGE_ORIENTATION_LANDSCAPE) 		mem_config = ILI9341_MemoryDataAccessControlConfig(0, 0, 1, 1, 0, 1);
	else if (orientation == PAGE_ORIENTATION_LANDSCAPE_MIRROR) 	mem_config = ILI9341_MemoryDataAccessControlConfig(1, 1, 1, 1, 1, 0);
	return mem_config;
}

const uint8_t* ILI9341_Init(void)
{
	return ili9341_init_str;
}

inline const uint8_t* ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	*((uint16_t*)&ili9341_setwin_str[2])  = (x0 >> 8) | (x0 << 8);
	*((uint16_t*)&ili9341_setwin_str[4])  = (x1 >> 8) | (x1 << 8);
	*((uint16_t*)&ili9341_setwin_str[8])  = (y0 >> 8) | (y0 << 8);
	*((uint16_t*)&ili9341_setwin_str[10]) = (y1 >> 8) | (y1 << 8);
	return ili9341_setwin_str;
}

const uint8_t* ILI9341_SleepIn(void)
{
	return ili9341_sleepin_str;
}

const uint8_t* ILI9341_SleepOut(void)
{
	return ili9341_sleepout_str;
}

const uint8_t* ILI9341_SetOrientation(LCD_PageOrientation orientation)
{
	ili9341_orientation_str[2] = orient_to_memconfig(orientation);
	return ili9341_orientation_str;
}
