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
 *  Версия: 1.4 для ESP32
 *
 *  https://www.youtube.com/@VadRov
 *  https://dzen.ru/vadrov
 *  https://vk.com/vadrov
 *  https://t.me/vadrov_channel
 *
 */

#include <string.h>
#include "soc/gpio_struct.h"
#include "soc/spi_reg.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "fonts.h"

#define malloc(par1) 		heap_caps_malloc(par1, MALLOC_CAP_8BIT)
#define calloc(par1, par2) 	heap_caps_calloc(par1, par2, MALLOC_CAP_8BIT)
#define free(par1)			heap_caps_free(par1)
#define LCD_SWAP_BYTES

#define ABS(x) ((x) > 0 ? (x) : -(x))

#define min(x1,x2)	(x1 < x2 ? x1 : x2)
#define max(x1,x2)	(x1 > x2 ? x1 : x2)

#define min3(x1,x2,x3)	min(min(x1,x2),x3)
#define max3(x1,x2,x3)	max(max(x1,x2),x3)

#define LCD_CS_LOW		reset_pin(lcd->spi_data.cs_pin);
#define LCD_CS_HI		set_pin(lcd->spi_data.cs_pin);
#define LCD_DC_LOW		reset_pin(lcd->spi_data.dc_pin);
#define LCD_DC_HI		set_pin(lcd->spi_data.dc_pin);

LCD_Handler *LCD = 0; //display list

static inline void set_pin(int pin_num)
{
	if (pin_num >= 0) {
		if (pin_num < 32) {
			GPIO.out_w1ts = 1UL << pin_num;
		}
		else {
			GPIO.out1_w1ts.data = 1UL << (32 - pin_num);
		}
	}
}

static inline void reset_pin(int pin_num)
{
	if (pin_num >= 0) {
		if (pin_num < 32) {
			GPIO.out_w1tc = 1UL << pin_num;
		}
		else {
			GPIO.out1_w1tc.data = 1UL << (32 - pin_num);
		}
	}
}

static inline uint32_t read_pin(int pin_num)
{
	if (pin_num >= 0) {
		volatile uint32_t *out = &GPIO.out;
		if (pin_num > 31) {
			pin_num -= 32;
			out = &GPIO.out1.val;
		}
		return *out & (1UL << pin_num);
	}
	return 0;
}

IRAM_ATTR void LCD_TC_Callback(void *arg)
{
	spi_dev_t *spi = (spi_dev_t*)arg;
	LCD_Handler *lcd = LCD;
	while (lcd) {
		if (spi == lcd->spi_data.spi) {
			if (lcd->spi_data.cs_pin >= 0) {
				if (read_pin(lcd->spi_data.cs_pin)) {
					lcd	= (LCD_Handler*)lcd->next;
					continue;
				}
			}
			//Clear all interrupt status bits and disable all spi interrupts.
			spi->slave.val &= ~0x3ff;
			if (!lcd->cs_control) {
			//	while (spi->ext2.st) ; //spi idle state
				LCD_CS_HI
			}
			break;
		}
		lcd	= (LCD_Handler*)lcd->next;
	}
}

inline void LCD_SetCS(LCD_Handler *lcd)
{
	LCD_CS_HI
}

inline void LCD_ResCS(LCD_Handler *lcd)
{
	LCD_CS_LOW
}

inline void LCD_SetDC(LCD_Handler *lcd)
{
	LCD_DC_HI
}

inline void LCD_ResDC(LCD_Handler *lcd)
{
	LCD_DC_LOW
}

//интерпретатор строк с управлящими кодами: "команда", "данные", "пауза", "завершение пакета"
void LCD_String_Interpretator(LCD_Handler* lcd, const uint8_t *str)
{
	spi_dev_t *spi = lcd->spi_data.spi;
	while (spi->cmd.usr) ;

	//Clear all interrupt status bits and disable all spi interrupts.
	lcd->spi_data.spi->slave.val &= ~0x3ff;

	if (!lcd->cs_control) LCD_CS_LOW
	uint8_t cmd, par_num;
	while (1) {
		cmd = *str++;
		par_num = *str++;
		if (par_num == 255) break; 						//eof command string
		if (par_num >= 20)	{							//pause, if the number of parameters >= 20
			vTaskDelay(par_num / portTICK_PERIOD_MS);
			continue;
		}
		while (spi->cmd.usr) ;
		//------------- send command -----------
		LCD_DC_LOW
		spi->user.usr_mosi_highpart = 0;
		spi->mosi_dlen.usr_mosi_dbitlen = 8 - 1;
		//---- copy command to SPI data registers ----
		spi->data_buf[0] = cmd;
		spi->cmd.usr = 1;
		if (!par_num) continue;
		while (spi->cmd.usr) ;
		//------ copy parameters to SPI data registers -------
		memcpy((void*)&spi->data_buf[8], str, par_num);
		str += par_num;
		while (spi->cmd.usr) ;
		//---------- send parameters -----------
		LCD_DC_HI
		spi->user.usr_mosi_highpart = 1;
		spi->mosi_dlen.usr_mosi_dbitlen = par_num * 8 - 1;
		spi->cmd.usr = 1;
	}
	while (spi->cmd.usr) ;
	spi->user.usr_mosi_highpart = 0;
	if (!lcd->cs_control) LCD_CS_HI
}

void LCD_HardWareReset (LCD_Handler* lcd)
{
	reset_pin(lcd->spi_data.reset_pin);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    set_pin(lcd->spi_data.reset_pin);
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

void LCD_SetOrientation(LCD_Handler* lcd, LCD_PageOrientation orientation)
{
	if (!lcd->SetOrientation_callback) return;
	uint16_t max_res = max(lcd->Width, lcd->Height);
	uint16_t min_res = min(lcd->Width, lcd->Height);
	if (orientation == PAGE_ORIENTATION_PORTRAIT || orientation == PAGE_ORIENTATION_PORTRAIT_MIRROR) {
		lcd->Width = min_res;
		lcd->Height = max_res;
		lcd->Width_Controller = lcd->w_cntrl;
		lcd->Height_Controller = lcd->h_cntrl;
		if (orientation == PAGE_ORIENTATION_PORTRAIT) {
			lcd->x_offs = lcd->w_offs;
			lcd->y_offs = lcd->h_offs;
		}
		else {
			lcd->x_offs = lcd->Width_Controller - lcd->Width - lcd->w_offs;
			lcd->y_offs = lcd->Height_Controller - lcd->Height - lcd->h_offs;
		}
	}
	else if (orientation == PAGE_ORIENTATION_LANDSCAPE || orientation == PAGE_ORIENTATION_LANDSCAPE_MIRROR)	{
		lcd->Width = max_res;
		lcd->Height = min_res;
		lcd->Width_Controller = lcd->h_cntrl;
		lcd->Height_Controller = lcd->w_cntrl;
		if (orientation == PAGE_ORIENTATION_LANDSCAPE) {
			lcd->x_offs = lcd->h_offs;
			lcd->y_offs = lcd->Height_Controller - lcd->Height - lcd->w_offs;
		}
		else {
			lcd->x_offs = lcd->Width_Controller - lcd->Width - lcd->h_offs;
			lcd->y_offs = lcd->w_offs;
		}
	}
	else return;
	LCD_String_Interpretator(lcd, lcd->SetOrientation_callback(orientation));
	lcd->Orientation = orientation;
}

//создание обработчика дисплея и добавление его в список дисплеев lcds
//возвращает указатель на созданный обработчик либо 0 при неудаче
LCD_Handler* LCD_DisplayAdd(LCD_Handler *lcds,
							uint16_t resolution1,
							uint16_t resolution2,
							uint16_t width_controller,
							uint16_t height_controller,
							int w_offs,
							int h_offs,
							DisplayInitCallback init,
							DisplaySetWindowCallback set_win,
							DisplaySleepInCallback sleep_in,
							DisplaySleepOutCallback sleep_out,
							DisplaySetOrientationCallback set_orientation,
							void *connection_data,
							LCD_BackLight_data bkl_data
					   )
{
	LCD_Handler* lcd = (LCD_Handler*)calloc(1, sizeof(LCD_Handler));
	if (!lcd) return 0;
	//инициализация данных подключения
	lcd->spi_data = *((LCD_SPI_Connected_data*)connection_data);
	//----------- внутренние служебные переменные ------------
	lcd->w_offs = w_offs;
	lcd->h_offs = h_offs;
	lcd->w_cntrl = width_controller;
	lcd->h_cntrl = height_controller;
	//настройка ориентации дисплея и смещения начала координат
	uint16_t max_res = max(resolution1, resolution2);
	uint16_t min_res = min(resolution1, resolution2);
	//------------ default setting screen page orientation -------------
	LCD_PageOrientation orientation = PAGE_ORIENTATION_PORTRAIT;
	lcd->Width = min_res;
	lcd->Height = max_res;
	lcd->Width_Controller = width_controller;
	lcd->Height_Controller = height_controller;
	lcd->x_offs = w_offs;
	lcd->y_offs = h_offs;
	//------------------------------------------------------------------
	if (lcd->Width_Controller < lcd->Width ||
		lcd->Height_Controller < lcd->Height ||
		init == NULL ||
		set_win == NULL )	{
		LCD_Delete(lcd);
		return 0;
	}
	lcd->Orientation = orientation;
	lcd->Init_callback = init;
	lcd->SetActiveWindow_callback = set_win;
	lcd->SleepIn_callback = sleep_in;
	lcd->SleepOut_callback = sleep_out;
	lcd->SetOrientation_callback = set_orientation;
	lcd->bkl_data = bkl_data;
	lcd->display_number = 0;
	lcd->next = 0;
	lcd->prev = 0;
	lcd->dma_descriptor_link = 0;
	if (lcd->spi_data.dma_channel) {
		int all_descr_lnk = (2 * lcd->Width * lcd->Height) / 4092;
		if ((2 * lcd->Width * lcd->Height) % 4092) all_descr_lnk++;
		lcd->dma_descriptor_link = (LCD_DMA_descriptor_link*)calloc(all_descr_lnk, sizeof(LCD_DMA_descriptor_link));
	}
	if (!lcds) {
		return lcd;
	}
	LCD_Handler *prev = lcds;
	while (prev->next) {
		prev = (LCD_Handler *)prev->next;
		lcd->display_number++;
	}
	lcd->prev = (void*)prev;
	prev->next = (void*)lcd;
	return lcd;
}

//удаляет дисплей
void LCD_Delete(LCD_Handler* lcd)
{
	if (lcd) {
		if (lcd->dma_descriptor_link) {
			free(lcd->dma_descriptor_link);
		}
		free(lcd);
	}
}

//инициализирует дисплей
void LCD_Init(LCD_Handler* lcd)
{
	LCD_HardWareReset(lcd);
	LCD_String_Interpretator(lcd, lcd->Init_callback());
	LCD_SetOrientation(lcd, lcd->Orientation);
	LCD_SetBackLight(lcd, lcd->bkl_data.bk_percent);
}

//возвращает яркость подсветки, %
inline uint8_t LCD_GetBackLight(LCD_Handler* lcd)
{
	return lcd->bkl_data.bk_percent;
}

//возвращает ширину дисплея, пиксели
inline uint16_t LCD_GetWidth(LCD_Handler* lcd)
{
	return lcd->Width;
}

//возвращает высоту дисплея, пиксели
inline uint16_t LCD_GetHeight(LCD_Handler* lcd)
{
	return lcd->Height;
}

//возвращает статус дисплея: занят либо свободен (требуется для отправки новых данных на дисплей)
//дисплей занят, если занято spi, к которому он подключен
inline LCD_State LCD_GetState(LCD_Handler* lcd)
{
	if (lcd->spi_data.spi->cmd.usr) {
		return LCD_STATE_BUSY;
	}
	return LCD_STATE_READY;
}

//backlighting (no PWM yet)
void LCD_SetBackLight(LCD_Handler* lcd, uint8_t bk_percent)
{
	if (bk_percent > 100) {
		bk_percent = 100;
	}
	lcd->bkl_data.bk_percent = bk_percent;
	//
	if (lcd->bkl_data.blk_pin >= 0) {
		if (bk_percent) {
			set_pin(lcd->bkl_data.blk_pin);
		}
		else {
			reset_pin(lcd->bkl_data.blk_pin);
		}
	}
}

//sleep mode in
void LCD_SleepIn(LCD_Handler* lcd)
{
	//подсветка без PWM (просто вкл./выкл.)
	if (lcd->bkl_data.blk_pin >= 0) {
		//backlight off
		reset_pin(lcd->bkl_data.blk_pin);
	}
	if (lcd->SleepIn_callback) {
		LCD_String_Interpretator(lcd, lcd->SleepIn_callback());
	}
}

//sleep mode out
void LCD_SleepOut(LCD_Handler* lcd)
{
	if (lcd->SleepOut_callback) {
		LCD_String_Interpretator(lcd, lcd->SleepOut_callback());
	}
	//backlight on
	LCD_SetBackLight(lcd, lcd->bkl_data.bk_percent);
}

void LCD_SetActiveWindow(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	LCD_String_Interpretator(lcd, lcd->SetActiveWindow_callback(x1 + lcd->x_offs, y1 + lcd->y_offs, x2 + lcd->x_offs, y2 + lcd->y_offs));
}

//В тесте 87 fps при spi_clock = 80 MHz (240 x 240 x 2)
void LCD_WriteDataDMA(LCD_Handler *lcd, uint16_t *data, uint32_t len)
{
	if (!len) return;
	if (lcd->spi_data.dma_channel) { //if DMA available
		spi_dev_t *spi = lcd->spi_data.spi;
		while (spi->cmd.usr) ;
		len *= 2;
		uint8_t *data_ptr = (uint8_t*)data;
		uint32_t all_dscr_lnk = len / 4092;
		if (len % 4092) all_dscr_lnk++;

		//Set the length of the sent data and run spi with DMA.
		spi->mosi_dlen.usr_mosi_dbitlen = len * 8 - 1;

		for (int i = 0; i < all_dscr_lnk; i++) {
			lcd->dma_descriptor_link[i].owner = 1;
			if (i == all_dscr_lnk - 1) {
				lcd->dma_descriptor_link[i].eof = 1;
				lcd->dma_descriptor_link[i].next_link = 0;
			}
			else {
				lcd->dma_descriptor_link[i].eof = 0;
				lcd->dma_descriptor_link[i].next_link = &lcd->dma_descriptor_link[i+1];
			}
			lcd->dma_descriptor_link[i].reserved = 0;
			lcd->dma_descriptor_link[i].lenght = len > 4092 ? 4092 : (len + 15) & 4092;
			lcd->dma_descriptor_link[i].size = len > 4092 ? 4092 : (len + 15) & 4092;
			lcd->dma_descriptor_link[i].buf = data_ptr;
			len -= 4092;
			data_ptr += 4092;
		}
		spi->dma_out_link.addr = ((uint32_t)&lcd->dma_descriptor_link[0]) & 0xFFFFF; //First link addr

		//Reset the DMA state machine and FIFO parameters.
		spi->dma_conf.val = SPI_AHBM_RST | SPI_AHBM_FIFO_RST | SPI_OUT_RST | SPI_IN_RST; //Reset: sets and then clears (in next string) the corresponding bits
		spi->dma_conf.val = SPI_OUT_EOF_MODE | SPI_OUT_DATA_BURST_EN | SPI_OUTDSCR_BURST_EN; //reset complete, set burst, set eof mode

		spi->dma_out_link.start = 1; //Start to use outlink descriptor

		while (!(spi->dma_rx_status & (1UL << 30))) ;  //Waiting for the FIFO of the DMA buffer to be
											   	   	   //filled with the first data packet from memory (bit 30 SPI_DMA_RSTATUS_REG)

		//Clear all interrupt status bits and enable only the "spi operation done" interrupt.
		uint32_t int_en_done = spi->slave.val;
		spi->slave.val = (int_en_done & (~0x3ff)) | (1UL << 9);

		spi->cmd.usr = 1;
	}
	else { //If DMA not available
		LCD_WriteData(lcd, data, len);
	}
}

//В тесте 74 fps при spi_clock = 80 MHz (240 x 240 x 2)
void LCD_WriteData(LCD_Handler *lcd, uint16_t *data, uint32_t len)
{
	if (!len) return;
	spi_dev_t *spi = lcd->spi_data.spi;
	while (spi->cmd.usr) ;
	volatile uint32_t *ptr_buf;
	uint32_t *ptr_data = (uint32_t *)data;
	if (len > 31) {
		spi->mosi_dlen.usr_mosi_dbitlen = 16 * 4 * 8 - 1;
		while(len > 31) {
			ptr_buf = &spi->data_buf[0];
			while (spi->cmd.usr) ;
			for (int i = 0; i < 16; i++) {		//64 bytes transaction assembly
				*(ptr_buf++) = *(ptr_data++);
			}
			spi->cmd.usr = 1;
			len -= 32;
		}
	}
	if (len) {
		while (spi->cmd.usr) ;
		spi->mosi_dlen.usr_mosi_dbitlen = len * 2 * 8 - 1;
		ptr_buf = &spi->data_buf[0];
		int j = (len * 2) / 4;
		j += ((len * 2) % 4) ? 1 : 0;
		while (j--) {
			*(ptr_buf++) = *(ptr_data++);
		}
		spi->cmd.usr = 1;
	}
	while (spi->cmd.usr) ;
}

void LCD_FillWindow(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
	uint16_t tmp;
	if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
	if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }
	if (x1 > lcd->Width - 1 || y1 > lcd->Height - 1) return;
	if (x2 > lcd->Width - 1)  x2 = lcd->Width - 1;
	if (y2 > lcd->Height - 1) y2 = lcd->Height - 1;
	uint32_t len = (x2 - x1 + 1) * (y2 - y1 + 1);
	LCD_SetActiveWindow(lcd, x1, y1, x2, y2);
	LCD_CS_LOW
	LCD_DC_HI
	uint16_t color16 = lcd->fill_color = LCD_Color_24b_to_16b(lcd, color);
	spi_dev_t *spi = lcd->spi_data.spi;
	if (!lcd->spi_data.dma_channel) { //DMA disable
		uint32_t color32 = color16 | (color16 << 16);
		volatile uint32_t *ptr_buf;
		if (len > 31) {
			spi->mosi_dlen.usr_mosi_dbitlen = 16 * 4 * 8 - 1;
			while(len > 31) {
				ptr_buf = &spi->data_buf[0];
				while (spi->cmd.usr) ;
				for (int i = 0; i < 16; i++) {
					*(ptr_buf++) = color32;
				}
				spi->cmd.usr = 1;
				len -= 32;
			}
		}
		if (len) {
			while (spi->cmd.usr) ;
			spi->mosi_dlen.usr_mosi_dbitlen = len * 2 * 8 - 1;
			ptr_buf = &spi->data_buf[0];
			int j = (len * 2) / 4;
			j += ((len * 2) % 4) ? 1 : 0;
			for (int i = 0; i < j; i++) {
				*(ptr_buf++) = color32;
			}
			spi->cmd.usr = 1;
		}
		while (spi->cmd.usr) ;
		LCD_CS_HI
	}
	else { //DMA enable
		lcd->fill_color = color16 | (color16 << 16);
		//Setting DMA link parameters
		lcd->dma_descriptor_link[0].owner = 1;
		lcd->dma_descriptor_link[0].eof = 0;
		lcd->dma_descriptor_link[0].reserved = 0;
		lcd->dma_descriptor_link[0].lenght = 4;
		lcd->dma_descriptor_link[0].size = 4;
		lcd->dma_descriptor_link[0].buf = (uint8_t*)&lcd->fill_color;
		lcd->dma_descriptor_link[0].next_link = &lcd->dma_descriptor_link[0]; //Pointer to next link in list ---> link points to itself

		spi->dma_out_link.addr = ((uint32_t)&lcd->dma_descriptor_link[0]) & 0xFFFFF; //First link addr

		//Reset the DMA state machine and FIFO parameters.
		spi->dma_conf.val = SPI_AHBM_RST | SPI_AHBM_FIFO_RST | SPI_OUT_RST | SPI_IN_RST; //Reset: sets and then clears (in next string) the corresponding bits
		spi->dma_conf.val = SPI_OUT_EOF_MODE | SPI_OUT_DATA_BURST_EN | SPI_OUTDSCR_BURST_EN; //reset complete, set burst, set eof mode

		//Start to use outlink descriptor
		spi->dma_out_link.start = 1;

		while (!(spi->dma_rx_status & (1UL << 30))) ;  //Waiting for the FIFO of the DMA buffer to be
													   //filled with the first data packet from memory (bit 30 SPI_DMA_RSTATUS_REG)

		//Clear all interrupt status bits and enable only the "spi operation done" interrupt.
		uint32_t int_en_done = spi->slave.val;
		spi->slave.val = (int_en_done & (~0x3ff)) | (1UL << 9);

		spi->mosi_dlen.usr_mosi_dbitlen = len * 16 - 1; 	//Length of sent data in bits
		//Start spi with DMA
		spi->cmd.usr = 1;
	}
}

/*
 * Выводит в заданную область дисплея блок памяти (изображение) по адресу в data:
 * x, y - координата левого верхнего угла области дисплея;
 * w, h - ширина и высота области дисплея;
 * data - указатель на блок памяти (изображение) для вывода на дисплей;
 * dma_use_flag - флаг, определяющий задействование DMA (0 - без DMA, !=0 - с DMA)
 */
void LCD_DrawImage(LCD_Handler* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *data, uint8_t dma_use_flag)
{
	if ((x > lcd->Width - 1) || (y > lcd->Height - 1) || x + w > lcd->Width || y + h > lcd->Height) return;
	LCD_SetActiveWindow(lcd, x, y, x + w - 1, y + h - 1);
	LCD_CS_LOW
	LCD_DC_HI
	if (dma_use_flag && lcd->spi_data.dma_channel) {
		LCD_WriteDataDMA(lcd, data, w * h);
	}
	else {
		LCD_WriteData(lcd, data, w * h);
		LCD_CS_HI
	}
}

/* Закрашивает весь дисплей заданным цветом */
void LCD_Fill(LCD_Handler* lcd, uint32_t color)
{
	LCD_FillWindow(lcd, 0, 0, lcd->Width - 1, lcd->Height - 1, color);
}

/* Рисует точку в заданных координатах */
void LCD_DrawPixel(LCD_Handler* lcd, int16_t x, int16_t y, uint32_t color)
{
	if (x > lcd->Width - 1 || y > lcd->Height - 1 || x < 0 || y < 0) return;
	LCD_SetActiveWindow(lcd, x, y, x, y);
	LCD_CS_LOW
	LCD_DC_HI
	spi_dev_t *spi = lcd->spi_data.spi;
	spi->mosi_dlen.usr_mosi_dbitlen = 16 - 1;
	spi->data_buf[0] = LCD_Color_24b_to_16b(lcd, color);
	spi->cmd.usr = 1;
	while (spi->cmd.usr) ;
	LCD_CS_HI
}

/*
 * Рисует линию по координатам двух точек
 * Горизонтальные и вертикальные линии рисуются очень быстро
 */
void LCD_DrawLine(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color)
{
	if(x0 == x1 || y0 == y1) {
		int16_t tmp;
		if (x0 > x1) { tmp = x0; x0 = x1; x1 = tmp; }
		if (y0 > y1) { tmp = y0; y0 = y1; y1 = tmp; }
		if (x1 < 0 || x0 > lcd->Width - 1)  return;
		if (y1 < 0 || y0 > lcd->Height - 1) return;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		LCD_FillWindow(lcd, x0, y0, x1, y1, color);
		return;
	}
	int16_t swap;
    uint16_t steep = ABS(y1 - y0) > ABS(x1 - x0);
    if (steep) {
		swap = x0;
		x0 = y0;
		y0 = swap;

		swap = x1;
		x1 = y1;
		y1 = swap;
    }

    if (x0 > x1) {
		swap = x0;
		x0 = x1;
		x1 = swap;

		swap = y0;
		y0 = y1;
		y1 = swap;
    }

    int16_t dx, dy;
    dx = x1 - x0;
    dy = ABS(y1 - y0);

    int16_t err = dx / 2;
    int16_t ystep;

    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }

    for (; x0<=x1; x0++) {
        if (steep) {
            LCD_DrawPixel(lcd, y0, x0, color);
        } else {
            LCD_DrawPixel(lcd, x0, y0, color);
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

/* Рисует прямоугольник по координатам левого верхнего и правого нижнего углов */
void LCD_DrawRectangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t color)
{
	LCD_DrawLine(lcd, x1, y1, x2, y1, color);
	LCD_DrawLine(lcd, x1, y2, x2, y2, color);
	LCD_DrawLine(lcd, x1, y1, x1, y2, color);
	LCD_DrawLine(lcd, x2, y1, x2, y2, color);
}

/* Рисует закрашенный прямоугольник по координатам левого верхнего и правого нижнего углов */
void LCD_DrawFilledRectangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t color)
{
	int16_t tmp;
	if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
	if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }
	if (x2 < 0 || x1 > lcd->Width - 1)  return;
	if (y2 < 0 || y1 > lcd->Height - 1) return;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	LCD_FillWindow(lcd, x1, y1, x2, y2, color);
}

/* Рисует треугольник по координатам трех точек */
void LCD_DrawTriangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint32_t color)
{
	LCD_DrawLine(lcd, x1, y1, x2, y2, color);
	LCD_DrawLine(lcd, x2, y2, x3, y3, color);
	LCD_DrawLine(lcd, x3, y3, x1, y1, color);
}

/* Виды пересечений отрезков */
typedef enum {
	LINES_NO_INTERSECT = 0, //не пересекаются
	LINES_INTERSECT,		//пересекаются
	LINES_MATCH				//совпадают (накладываются)
} INTERSECTION_TYPES;

/*
 * Определение вида пересечения и координат (по оси х) пересечения отрезка с координатами (x1,y1)-(x2,y2)
 * с горизонтальной прямой y = y0
 * Возвращает один из видов пересечения типа INTERSECTION_TYPES, а в переменных x_min, x_max - координату
 * либо диапазон пересечения (если накладываются).
 * В match инкрементирует количество накладываний (считаем результаты со всех нужных вызовов)
 */
static INTERSECTION_TYPES LinesIntersection(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t y0, int16_t *x_min, int16_t *x_max, uint8_t *match)
{
	if (y1 == y2) { //Частный случай - отрезок параллелен оси х
		if (y0 == y1) { //Проверка на совпадение
			*x_min = min(x1, x2);
			*x_max = max(x1, x2);
			(*match)++;
			return LINES_MATCH;
		}
		return LINES_NO_INTERSECT;
	}
	if (x1 == x2) { //Частный случай - отрезок параллелен оси y
		if (min(y1, y2) <= y0 && y0 <= max(y1, y2)) {
			*x_min = *x_max = x1;
			return LINES_INTERSECT;
		}
		return LINES_NO_INTERSECT;
	}
	//Определяем точку пересечения прямых (уравнение прямой получаем из координат точек, задающих отрезок)
	*x_min = *x_max = (x2 - x1) * (y0 - y1) / (y2 - y1) + x1;
	if (min(x1, x2) <= *x_min && *x_min <= max(x1, x2)) { //Если координата x точки пересечения принадлежит отрезку,
		return LINES_INTERSECT;							  //то есть пересечение
	}
	return LINES_NO_INTERSECT;
}

/* Рисует закрашенный треугольник по координатам трех точек */
void LCD_DrawFilledTriangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint32_t color)
{
	//Сортируем координаты в порядке возрастания y
	int16_t tmp;
	if (y1 > y2) {
		tmp = y1; y1 = y2; y2 = tmp;
		tmp = x1; x1 = x2; x2 = tmp;
	}
	if (y1 > y3) {
		tmp = y1; y1 = y3; y3 = tmp;
		tmp = x1; x1 = x3; x3 = tmp;
	}
	if (y2 > y3) {
		tmp = y2; y2 = y3; y3 = tmp;
		tmp = x2; x2 = x3; x3 = tmp;
	}
	//Проверяем, попадает ли треугольник в область вывода
	if (y1 > lcd->Height - 1 ||	y3 < 0) return;
	int16_t xmin = min3(x1, x2, x3);
	int16_t xmax = max3(x1, x2, x3);
	if (xmax < 0 || xmin > lcd->Width - 1) return;
	uint8_t c_mas, match;
	int16_t x_mas[8], x_min, x_max;
	//"Обрезаем" координаты, выходящие за рабочую область дисплея
	int16_t y_start = y1 < 0 ? 0: y1;
	int16_t y_end = y3 > lcd->Height - 1 ? lcd->Height - 1: y3;
	//Проходим в цикле по точкам диапазона координаты y и ищем пересечение отрезка y = y[i] (где y[i]=y1...y3, 1)
	//со сторонами треугольника
	for (int16_t y = y_start; y < y_end; y++) {
		c_mas = match = 0;
		if (LinesIntersection(x1, y1, x2, y2, y, &x_mas[c_mas], &x_mas[c_mas + 1], &match)) {
			c_mas += 2;
		}
		if (LinesIntersection(x2, y2, x3, y3, y, &x_mas[c_mas], &x_mas[c_mas + 1], &match)) {
			c_mas += 2;
		}
		if (LinesIntersection(x3, y3, x1, y1, y, &x_mas[c_mas], &x_mas[c_mas + 1], &match)) {
			c_mas += 2;
		}
		if (!c_mas) continue;
		x_min = x_max = x_mas[0];
		while (c_mas) {
			x_min = min(x_min, x_mas[c_mas - 2]);
			x_max = max(x_max, x_mas[c_mas - 1]);
			c_mas -= 2;
		}
		LCD_DrawLine(lcd, x_min, y, x_max, y, color);
	}
}

/* Рисует окружность с заданным центром и радиусом */
void LCD_DrawCircle(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t r, uint32_t color)
{
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	LCD_DrawPixel(lcd, x0, y0 + r, color);
	LCD_DrawPixel(lcd, x0, y0 - r, color);
	LCD_DrawPixel(lcd, x0 + r, y0, color);
	LCD_DrawPixel(lcd, x0 - r, y0, color);

	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;

		LCD_DrawPixel(lcd, x0 + x, y0 + y, color);
		LCD_DrawPixel(lcd, x0 - x, y0 + y, color);
		LCD_DrawPixel(lcd, x0 + x, y0 - y, color);
		LCD_DrawPixel(lcd, x0 - x, y0 - y, color);

		LCD_DrawPixel(lcd, x0 + y, y0 + x, color);
		LCD_DrawPixel(lcd, x0 - y, y0 + x, color);
		LCD_DrawPixel(lcd, x0 + y, y0 - x, color);
		LCD_DrawPixel(lcd, x0 - y, y0 - x, color);
	}
}

/* Рисует закрашенную окружность с заданным центром и радиусом */
void LCD_DrawFilledCircle(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t r, uint32_t color)
{
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	LCD_DrawLine(lcd, x0 - r, y0, x0 + r, y0, color);

	while (x < y) {
		if (f >= 0)	{
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;

		LCD_DrawLine(lcd, x0 - x, y0 + y, x0 + x, y0 + y, color);
		LCD_DrawLine(lcd, x0 + x, y0 - y, x0 - x, y0 - y, color);

		LCD_DrawLine(lcd, x0 + y, y0 + x, x0 - y, y0 + x, color);
		LCD_DrawLine(lcd, x0 + y, y0 - x, x0 - y, y0 - x, color);
	}
}

/*
 * Вывод на дисплей символа с кодом в ch, с начальными координатами координатам (x, y), шрифтом font, цветом color,
 * цветом окружения bgcolor.
 * modesym - определяет, как выводить символ:
 *    LCD_SYMBOL_PRINT_FAST - быстрый вывод с полным затиранием знакоместа;
 *    LCD_SYMBOL_PRINT_PSETBYPSET - вывод символа по точкам, при этом цвет окружения bgcolor игнорируется (режим наложения).
 * Ширина символа до 32 пикселей (4 байта на строку). Высота символа библиотекой не ограничивается.
 */
void LCD_WriteChar(LCD_Handler* lcd, uint16_t x, uint16_t y, char ch, FontDef *font, uint32_t txcolor, uint32_t bgcolor, LCD_PrintSymbolMode modesym)
{
	int i, j, k;
	uint32_t tmp = 0;
	const uint8_t *b = font->data;
	uint16_t color;
	uint16_t txcolor16 = LCD_Color_24b_to_16b(lcd, txcolor);
	uint16_t bgcolor16 = LCD_Color_24b_to_16b(lcd, bgcolor);
	ch = ch < font->firstcode || ch > font->lastcode ? 0: ch - font->firstcode;
	int bytes_per_line = ((font->width - 1) >> 3) + 1;
	if (bytes_per_line > 4) { //Поддержка ширины символов до 32 пикселей (4 байта на строку)
		return;
	}
	k = 1 << ((bytes_per_line << 3) - 1);
	b += ch * bytes_per_line * font->height;
	if (modesym == LCD_SYMBOL_PRINT_FAST) { //fast block
		spi_dev_t *spi = lcd->spi_data.spi;
		LCD_SetActiveWindow(lcd, x, y, x + font->width - 1, y + font->height - 1);
		LCD_CS_LOW
		LCD_DC_HI
		uint32_t color32 = 0, m = 0, cnt = 0;
		volatile uint32_t *ptr_buf = &spi->data_buf[0];
		spi->mosi_dlen.usr_mosi_dbitlen = 16 * 4 * 8 - 1;
		spi->user.usr_mosi_highpart = 0;
		for (i = 0; i < font->height; i++) {
			if (bytes_per_line == 1)      { tmp = *((uint8_t*)b);  }
			else if (bytes_per_line == 2) { tmp = *((uint16_t*)b); }
			else if (bytes_per_line == 3) { tmp = (*((uint8_t*)b)) | ((*((uint8_t*)(b + 1))) << 8) |  ((*((uint8_t*)(b + 2))) << 16); }
			else { tmp = *((uint32_t*)b); }
			b += bytes_per_line;
			for (j = 0; j < font->width; j++) {
				color = (tmp << j) & k ? txcolor16: bgcolor16;
				if (!m) {
					color32 = color;
					m++;
				}
				else {
					m = 0;
					color32 |= (color << 16);
					while (spi->cmd.usr) ;		//wait eof transaction
					*(ptr_buf++) = color32;
					cnt++;
					if (cnt == 16) {
						cnt = 0;
						ptr_buf = &spi->data_buf[0];
						spi->cmd.usr = 1;
					}
				}
			}
		}
		while (spi->cmd.usr) ;		//wait eof transaction
		if (cnt || m) {				//sending remaining data
			if (m) *(ptr_buf++) = color32;
			spi->mosi_dlen.usr_mosi_dbitlen = (cnt * 4 + m * 2) * 8 - 1;
			spi->cmd.usr = 1;
		}
		while (spi->cmd.usr) ;		//wait eof transaction
		LCD_CS_HI
	}
	else { //pset by pset
		for (i = 0; i < font->height; i++) {
			if (bytes_per_line == 1) { tmp = *((uint8_t*)b); }
			else if (bytes_per_line == 2) { tmp = *((uint16_t*)b); }
			else if (bytes_per_line == 3) { tmp = (*((uint8_t*)b)) | ((*((uint8_t*)(b + 1))) << 8) |  ((*((uint8_t*)(b + 2))) << 16); }
			else if (bytes_per_line == 4) { tmp = *((uint32_t*)b); }
			b += bytes_per_line;
			for (j = 0; j < font->width; j++) {
				if ((tmp << j) & k) {
					LCD_DrawPixel(lcd, x + j, y + i, txcolor);
				}
			}
		}
	}
}

//вывод строки str текста с позиции x, y, шрифтом font, цветом букв color, цветом окружения bgcolor
//modesym - определяет, как выводить текст:
//LCD_SYMBOL_PRINT_FAST - быстрый вывод с полным затиранием знакоместа
//LCD_SYMBOL_PRINT_PSETBYPSET - вывод по точкам, при этом цвет окружения bgcolor игнорируется (позволяет накладывать надписи на картинки)
void LCD_WriteString(LCD_Handler* lcd, uint16_t x, uint16_t y, const char *str, FontDef *font, uint32_t color, uint32_t bgcolor, LCD_PrintSymbolMode modesym)
{
	while (*str) {
		if (x + font->width > lcd->Width) {
			x = 0;
			y += font->height;
			if (y + font->height > lcd->Height) {
				break;
			}
		}
		LCD_WriteChar(lcd, x, y, *str, font, color, bgcolor, modesym);
		x += font->width;
		str++;
	}
	lcd->AtPos.x = x;
	lcd->AtPos.y = y;
}

inline uint16_t LCD_Color (LCD_Handler *lcd, uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
#ifdef LCD_SWAP_BYTES
	color = (color >> 8) | (color << 8);
#endif
	return color;
}

inline uint16_t LCD_Color_24b_to_16b(LCD_Handler *lcd, uint32_t color)
{
	uint8_t r = (color >> 16) & 0xff;
	uint8_t g = (color >> 8) & 0xff;
	uint8_t b = color & 0xff;
	return LCD_Color(lcd, r, g, b);
}
