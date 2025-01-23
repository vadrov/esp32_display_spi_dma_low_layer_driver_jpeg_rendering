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

#include "st7789.h"
#include "esp_system.h"

static const uint8_t st7789_init_str[] = {
			ST7789_DISPOFF, 0,
			ST7789_COLMOD, 1, ST7789_COLOR_MODE_16bit,
			ST7789_PORCTRL, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
			ST7789_GCTRL, 1, 0x35,
		    ST7789_VCOMS, 1, 0x19,
		    ST7789_LCMCTRL, 1, 0x2C,
		    ST7789_VDVVRHEN, 2, 0x01, 0xFF,
		    ST7789_VRHS, 1, 0x12,
		    ST7789_VDVS, 1, 0x20,
		    ST7789_FRCTRL2, 1, 0x0F,
		    ST7789_PWCTRL1, 2, 0xA4, 0xA1,
			ST7789_PVGAMCTRL, 14, 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23,
			ST7789_NVGAMCTRL, 14, 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23,
			ST7789_INVON, 0,
			ST7789_SLPOUT, 0,
			0, 50,						//pause 50 ms
			ST7789_NORON, 0,
			ST7789_DISPON, 0,
			0, 150,						//pause 150 ms
			0, 255						//eof string
	};

DRAM_ATTR static uint8_t st7789_setwin_str[]  = {	ST7789_CASET, 4, 0, 0, 0, 0,	//x0, x1
								 					ST7789_RASET, 4, 0, 0, 0, 0,	//y0, y1
													ST7789_RAMWR, 0,
													0, 255							//end command string
												};

static const uint8_t st7789_sleepin_str[]  = {	ST7789_SLPIN, 0,
												0, 30,				//pause 30 ms
												0, 255				//eof string
											};

static const uint8_t st7789_sleepout_str[] = {	ST7789_SLPOUT, 0,
												0, 120,				//pause 120 ms
												0, 255				//eof string
											};

static uint8_t st7789_orientation_str[] = {	ST7789_MADCTL, 1, 0,	//mem_config (memory data access config)
											0, 255					//eof string
										};

static uint8_t ST7789_MemoryDataAccessControlConfig(uint8_t mirror_x, uint8_t mirror_y, uint8_t exchange_xy, uint8_t mirror_color, uint8_t refresh_v, uint8_t refresh_h)
{
	uint8_t mem_config = 0;
	if (mirror_x) 		mem_config |= ST7789_MADCTL_MX;
	if (mirror_y) 		mem_config |= ST7789_MADCTL_MY;
	if (exchange_xy) 	mem_config |= ST7789_MADCTL_MV;
	if (mirror_color) 	mem_config |= ST7789_MADCTL_BGR;
	if (refresh_v)		mem_config |= ST7789_MADCTL_ML;
	if (refresh_h)		mem_config |= ST7789_MADCTL_MH;
	return mem_config;
}

static uint8_t orient_to_memconfig(LCD_PageOrientation orientation)
{
	uint8_t mem_config = 0;
	if (orientation == PAGE_ORIENTATION_PORTRAIT) 				mem_config = ST7789_MemoryDataAccessControlConfig(0, 0, 0, 0, 0, 0);
	else if (orientation == PAGE_ORIENTATION_PORTRAIT_MIRROR) 	mem_config = ST7789_MemoryDataAccessControlConfig(1, 1, 0, 0, 1, 1);
	else if (orientation == PAGE_ORIENTATION_LANDSCAPE) 		mem_config = ST7789_MemoryDataAccessControlConfig(1, 0, 1, 0, 0, 1);
	else if (orientation == PAGE_ORIENTATION_LANDSCAPE_MIRROR) 	mem_config = ST7789_MemoryDataAccessControlConfig(0, 1, 1, 0, 1, 0);
	return mem_config;
}

const uint8_t* ST7789_Init(void)
{
	return st7789_init_str;
}

inline const uint8_t* ST7789_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	*((uint16_t*)&st7789_setwin_str[2]) = (x0 >> 8) | (x0 << 8);
	*((uint16_t*)&st7789_setwin_str[4]) = (x1 >> 8) | (x1 << 8);
	*((uint16_t*)&st7789_setwin_str[8]) = (y0 >> 8) | (y0 << 8);
	*((uint16_t*)&st7789_setwin_str[10]) = (y1 >> 8) | (y1 << 8);
	return st7789_setwin_str;
}

const uint8_t* ST7789_SleepIn(void)
{
	return st7789_sleepin_str;
}

const uint8_t* ST7789_SleepOut(void)
{
	return st7789_sleepout_str;
}

const uint8_t* ST7789_SetOrientation(LCD_PageOrientation orientation)
{
	st7789_orientation_str[2] = orient_to_memconfig(orientation);
	return st7789_orientation_str;
}
