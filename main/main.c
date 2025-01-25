/*
 *  Author: VadRov
 *  Copyright (C) 2019-2024, VadRov, all right reserved.
 *
 *  ESP32 low layer driver for spi displays (esp-idf-v5.4)
 *  Optimized JPEG decoder.
 *  Demonstration of line-by-line graphics rendering running on two cpu cores.
 *
 *  https://www.youtube.com/@VadRov
 *  https://dzen.ru/vadrov
 *  https://vk.com/vadrov
 *  https://t.me/vadrov_channel
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "soc/spi_reg.h"
#include "soc/spi_struct.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_periph.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/dport_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/dport_access.h"
#include "driver/gpio.h"


#define ST7789		0
#define ILI9341		1

/*--------------------------------------------- User defines ---------------------------------------------*/
#define CS_PIN   		17	/* -1 if not used */
#define DC_PIN   		21
#define RST_PIN  		19
#define BCKL_PIN 		5
#define SPI_			SPI3 	/* SPI2 (GPIO13 -> MOSI, GPIO14 -> CLK)
					   SPI3 (GPIO23 -> MOSI, GPIO18 -> CLK) */
#define DMA_ch			1 	/* DMA channel 1 or 2, 0 - if DMA not used */

#define ACT_DISPLAY		ST7789  /* ST7789 or ILI9341 */
#define HI_SPEED			/* if uncommented f_clk spi = 80 MHz, else 40 MHz */

#define RENDER_USE_TWO_CORES 	/* Use two esp32 cores for graphics rendering. Comment out if using one core.*/
#define RENDER_BUFFER_LINES	8 
/*--------------------------------------------------------------------------------------------------------*/

/* 0...3, select in table:
SPI mode		0		1		2		3
polarity 	   low     low	   high    high
phase	  	  1 edge  2 edge  1 edge  2 edge
for st7789 - mode 2, for ili9341 - mode 0    */

//display driver
#include "display.h"
#if ACT_DISPLAY == ST7789
#define SPI_mode		2
#include "st7789.h"
#elif ACT_DISPLAY == ILI9341
#define SPI_mode		0
#include "ili9341.h"
#endif

//MicroGL2D library
#include "microgl2d.h"
//textures
#include "textures.h"
//jpeg decoder
#include "jpeg_chan.h"

extern const uint8_t image1_jpg_start[] asm("_binary_image1_jpg_start");
extern const uint8_t image1_jpg_end[] asm("_binary_image1_jpg_end");

static uint16_t *render_buf1, *render_buf2; //render buffers

#ifdef RENDER_USE_TWO_CORES
typedef struct {
	MGL_OBJ *obj;
	int x0, y0, x1, y1;
	uint16_t *data;
} render_parameters;

render_parameters render1_par, render2_par;
SemaphoreHandle_t renderSemaphore;

static void Render_task(void *param)
{
	render_parameters *par = (render_parameters*)param;
	MGL_RenderObjects(par->obj, par->x0, par->y0, par->x1, par->y1, par->data);
	xSemaphoreGive(renderSemaphore);
	vTaskDelete(NULL);
}
#endif

static void Render2D (LCD_Handler *lcd, MGL_OBJ *obj, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, int x_c, int y_c)
{
	int lines = y1 - y0 + 1;
	uint32_t w = x1 - x0 + 1;
	uint16_t *render_ptr = render_buf1;
	uint8_t use_dma = (lcd->spi_data.dma_channel) ? 1 : 0;
	LCD_SetActiveWindow(lcd, x0, y0, x1, y1);
	lcd->cs_control = lcd->dc_control = 1;
	LCD_ResCS(lcd);
	LCD_SetDC(lcd);
	uint32_t r_lines;
	while (lines) {
		r_lines = lines < RENDER_BUFFER_LINES ? lines : RENDER_BUFFER_LINES;
#ifndef RENDER_USE_TWO_CORES
		MGL_RenderObjects(obj, x_c, y_c, x_c + w - 1, y_c + r_lines - 1, render_ptr);
#else
		if (r_lines == 1) {
			MGL_RenderObjects(obj, x_c, y_c, x_c + w - 1, y_c, render_ptr);
		}
		else {
			uint32_t r_lines_c1, r_lines_c0;
			r_lines_c0 = r_lines / 2;
			r_lines_c1 = r_lines - r_lines_c0;
			render1_par.obj = obj;
			render1_par.data = render_ptr;
			render1_par.x0 = x_c;
			render1_par.y0 = y_c;
			render1_par.x1 = x_c + w - 1;
			render1_par.y1 = y_c + r_lines_c1 - 1;
			render2_par.obj = obj;
			render2_par.data = render_ptr + r_lines_c1 * w;
			render2_par.x0 = x_c;
			render2_par.y0 = y_c + r_lines_c1;
			render2_par.x1 = x_c + w - 1;
			render2_par.y1 = y_c + r_lines - 1;
			xTaskCreatePinnedToCore(Render_task, "render_core0", 2000, (void*)&render1_par, 4, NULL, 0);
		    xTaskCreatePinnedToCore(Render_task, "render_core1", 2000, (void*)&render2_par, 4, NULL, 1);
		    xSemaphoreTake(renderSemaphore, portMAX_DELAY);
		    xSemaphoreTake(renderSemaphore, portMAX_DELAY);
		}
#endif
		if (use_dma) {
			LCD_WriteDataDMA(lcd, render_ptr, r_lines * w);
			render_ptr = (render_ptr == render_buf1) ? render_buf2 : render_buf1;
		}
		else {
			LCD_WriteData(lcd, render_ptr, r_lines * w);
		}
		lines -= r_lines;
		y_c += r_lines;
	}
	lcd->cs_control = lcd->dc_control = 0;
	if (use_dma) {
		return;
	}
	LCD_SetCS(lcd);
}

void demo(void *par)
{
	LCD_Handler *lcd = (LCD_Handler*)par;
#ifdef RENDER_USE_TWO_CORES
	renderSemaphore = xSemaphoreCreateCounting(2, 0);
#endif
	//gradients
	MGL_GRADIENT *grad1 = MGL_GradientCreate(MGL_GRADIENT_LINEAR);
	MGL_GradientAddColor(grad1, 0,   COLOR_WHITE,    1);
	MGL_GradientAddColor(grad1, 50,  COLOR_DARKGREY, 1);
	MGL_GradientAddColor(grad1, 100, COLOR_WHITE,    1);
	MGL_GradientSetDeg(grad1, 0);

	MGL_GRADIENT *grad2 = MGL_GradientCreate(MGL_GRADIENT_LINEAR);
	MGL_GradientAddColor(grad2, 0, COLOR_BLUE, 1);
	MGL_GradientAddColor(grad2, 100, COLOR_WHITE, 1);
	MGL_GradientSetDeg(grad2, 0);

	MGL_GRADIENT *gradt = MGL_GradientCreate(MGL_GRADIENT_LINEAR);
	MGL_GradientAddColor(gradt, 0, COLOR_BLUE, 1);
	MGL_GradientAddColor(gradt, 100, COLOR_CYAN, 1);
	MGL_GradientSetDeg(gradt, 0);

	//backgroung
	MGL_GRADIENT *grad_fon = MGL_GradientCreate(MGL_GRADIENT_LINEAR);
	MGL_GradientSetDeg(grad_fon, 0);
	MGL_GradientAddColor(grad_fon, 0,   0xFF0000, 1);
	MGL_GradientAddColor(grad_fon, 25,  0x00FFFF, 1);
	MGL_GradientAddColor(grad_fon, 40,  0xFFFFFF, 1);
	MGL_GradientAddColor(grad_fon, 50,  0x00FF00, 1);
	MGL_GradientAddColor(grad_fon, 60,  COLOR_ORANGE, 1);
	MGL_GradientAddColor(grad_fon, 75,  0xFFFF00, 1);
	MGL_GradientAddColor(grad_fon, 100, 0x0000FF, 1);
	MGL_OBJ *rect = MGL_ObjectAdd(0, MGL_OBJ_TYPE_FILLRECTANGLE);
	MGL_SetRectangle(rect, 0, 0, lcd->Width-1, lcd->Height-1, COLOR_WHITE);
	MGL_ObjectSetGradient(rect, grad_fon);
	//MGL_TEXTURE texture_youtube = {(MGL_IMAGE *)&image_youtube, 0, 0/*MGL_TEXTURE_REPEAT_X | MGL_TEXTURE_REPEAT_Y*/};
	//MGL_ObjectSetTexture(rect, &texture_youtube);

	//info window
	MGL_OBJ *obj1 = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_FILLRECTANGLE);
	MGL_SetRectangle(obj1, 0, 0, 200, 14, COLOR_BLUE);
	MGL_ObjectSetGradient(obj1, grad2);
	MGL_OBJ *obj2 = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_FILLRECTANGLE);
	MGL_SetRectangle(obj2, 0, 14, 200, 100, COLOR_WHITE);
	MGL_ObjectSetGradient(obj2, grad1);
	MGL_ObjectSetTransparency(obj2, 50);
	MGL_OBJ *obj3 = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_FILLRECTANGLE);
	MGL_SetRectangle(obj3, 200-12, 2, 200-4, 12, COLOR_RED);
	MGL_OBJ *obj4 = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_TEXT);
	MGL_SetText(obj4, 200-12, 1, "X", &Font_8x13, 1, COLOR_WHITE);
	MGL_OBJ *obj5 = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_TEXT);
	MGL_SetText(obj5, 4, 1, "Message", &Font_8x13, 0, COLOR_WHITE);
	MGL_OBJ *obj6 = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_TEXT);
	MGL_SetText(obj6, 8, 20, "Hello, YouTube!", &Font_12x20, 1, COLOR_BLACK);
	MGL_ObjectSetGradient(obj6, gradt);
	MGL_OBJ *rect1 = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_RECTANGLE);
	MGL_SetRectangle(rect1, 0, 0, 200, 100, COLOR_BLACK);
	MGL_OBJ *img_obj = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_FILLCIRCLE);
	MGL_SetCircle(img_obj, 30, 70, 20, COLOR_WHITE);
	MGL_TEXTURE texture = {(MGL_IMAGE *)&image_avatar, 0, 0};
	MGL_ObjectSetTexture(img_obj, &texture);

	//horizontal slider
	MGL_GRADIENT *grad3 = MGL_GradientCreate(MGL_GRADIENT_RADIAL);
	MGL_GradientAddColor(grad3, 0, COLOR_WHITE, 1);
	MGL_GradientAddColor(grad3, 100, COLOR_BLUE, 1);
	MGL_GRADIENT *grad4 = MGL_GradientCreate(MGL_GRADIENT_LINEAR);
	MGL_GradientAddColor(grad4, 0, COLOR_CYAN, 1);
	MGL_GradientAddColor(grad4, 100, COLOR_RED, 1);
	MGL_GradientSetDeg(grad4, 90);
	MGL_GRADIENT *grad5 = MGL_GradientCreate(MGL_GRADIENT_LINEAR);
	MGL_GradientAddColor(grad5, 0, COLOR_CYAN, 1);
	MGL_GradientAddColor(grad5, 100, COLOR_RED, 1);
	MGL_OBJ *slider = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_SLIDER);
	MGL_SetSlider(slider, MGL_SLIDER_HORIZONTAL, 0, lcd->Height/2, lcd->Width - 20, lcd->Height/2 + 19, COLOR_LIGHTGREY, 0, 100, 50, "");
	MGL_SetRectangle(((MGL_OBJ_SLIDER*)slider->object)->obj_rectangle1, 0, 0, 0, 0, COLOR_CYAN);
	MGL_ObjectSetGradient(((MGL_OBJ_SLIDER*)slider->object)->obj_rectangle1, grad4);
	MGL_SetRectangle(((MGL_OBJ_SLIDER*)slider->object)->obj_rectangle2, 0, 0, 0, 0, COLOR_DARKGREY);
	MGL_ObjectSetGradient(((MGL_OBJ_SLIDER*)slider->object)->obj_circle, grad3);
	//vertical slider
	MGL_OBJ *slider1 = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_SLIDER);
	MGL_SetSlider(slider1, MGL_SLIDER_VERTICAL, lcd->Width - 20, 0, lcd->Width - 1, lcd->Height - 1, COLOR_LIGHTGREY, 0, 100, 100, "");
	MGL_SetRectangle(((MGL_OBJ_SLIDER*)slider1->object)->obj_rectangle1, 0, 0, 0, 0, COLOR_CYAN);
	MGL_ObjectSetGradient(((MGL_OBJ_SLIDER*)slider1->object)->obj_rectangle1, grad5);
	MGL_SetRectangle(((MGL_OBJ_SLIDER*)slider1->object)->obj_rectangle2, 0, 0, 0, 0, COLOR_DARKGREY);
	MGL_ObjectSetGradient(((MGL_OBJ_SLIDER*)slider1->object)->obj_circle, grad3);

	//mill
	MGL_TEXTURE melnica_tex = {(MGL_IMAGE *)&image_melnica, 0, 0};
	MGL_OBJ *melnica = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_FILLRECTANGLE);
	MGL_SetRectangle(melnica, 0, 0, 60, 90, COLOR_RED);
	MGL_ObjectSetTransparency(melnica, 0);
	MGL_ObjectSetTexture(melnica, &melnica_tex);

	//horse
	MGL_TEXTURE loshad_tex = {(MGL_IMAGE *)&image_loshad, 0, MGL_TEXTURE_FLIP_X};
	MGL_OBJ *loshad = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_FILLCIRCLE);
	MGL_SetCircle(loshad, 100, 60, 30, COLOR_RED);
	MGL_ObjectSetTexture(loshad, &loshad_tex);

	//text - copyright
	MGL_GRADIENT *grad_cprt = MGL_GradientCreate(MGL_GRADIENT_LINEAR);
	MGL_GradientAddColor(grad_cprt, 0, COLOR_WHITE, 1);
	MGL_GradientAddColor(grad_cprt, 100, COLOR_BLUE, 1);
	MGL_GradientSetDeg(grad_cprt, 45);
	MGL_OBJ *text = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_TEXT);
	MGL_SetText(text, (lcd->Width - 14*12)/2 - 1, (lcd->Height - 20)/2 - 1, "(c)2022 VadRov", &Font_12x20, 1, COLOR_RED);
	MGL_ObjectSetGradient(text, grad_cprt);
	MGL_OBJ *text1 = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_TEXT);
	MGL_SetText(text1, (lcd->Width - 8*12)/2 - 1, (lcd->Height - 20)/2 + 20 - 1, "mGL Demo", &Font_12x20, 1, COLOR_WHITE);

	//text "fps = ..." frames in sec
	char fps_s[10] = "FPS = ";
	MGL_OBJ *fps = MGL_ObjectAdd(rect, MGL_OBJ_TYPE_TEXT);
	MGL_SetText(fps, 0, lcd->Height - 21, fps_s, &Font_12x20, 0, COLOR_WHITE);

	int z = 1, z1 = 1, z2 = 1, z3 = 1, z4 = -2, z5 = -1, z6 = -1, step_z6 = 0, zz1 = 1, zz2 = 1;
	uint32_t frame = 0, counter = 0;
	int ticks_in_sec = esp_clk_cpu_freq(); //частота cpu, Гц (соответствует количеству тактов за секунду)
	uint32_t tick = esp_cpu_get_cycle_count();
	while (1)  {
		Render2D(lcd, rect, 0, 0, lcd->Width - 1, lcd->Height - 1, 0, 0);
		MGL_ObjectListMove(obj1, z, z1);
		MGL_ObjectListMove(slider, -z, -z1);

		if (((MGL_OBJ_RECTANGLE*)obj1->object)->x1 <= 0)   z = -z;
		if (((MGL_OBJ_RECTANGLE*)obj1->object)->x2 >= lcd->Width - 1) z = -z;

		if (((MGL_OBJ_RECTANGLE*)obj1->object)->y1 <= 0)   z1 = -z1;
		if (((MGL_OBJ_RECTANGLE*)obj1->object)->y2 >= lcd->Height - 1 - 86) z1 = -z1;

		MGL_ObjectMove(text, 0, -1);
		MGL_ObjectMove(text1, 0, -1);
		if (((MGL_OBJ_TEXT*)text1->object)->y < -30) {
			((MGL_OBJ_TEXT*)text->object)->y = lcd->Height + 30;
			((MGL_OBJ_TEXT*)text1->object)->y = lcd->Height + 50;
		}

		((MGL_OBJ_CIRCLE*)img_obj->object)->r += z2;
		if (((MGL_OBJ_CIRCLE*)img_obj->object)->r < 15 ||
			((MGL_OBJ_CIRCLE*)img_obj->object)->r > 25)
			z2 = -z2;

		img_obj->texture->alpha += 10;
		if (img_obj->texture->alpha > 360) img_obj->texture->alpha %= 360;

		((MGL_OBJ_SLIDER*)slider->object)->value += z3;
		if (((MGL_OBJ_SLIDER*)slider->object)->value <= 0 ||
			((MGL_OBJ_SLIDER*)slider->object)->value >= 100)
			z3 = -z3;

		((MGL_OBJ_SLIDER*)slider1->object)->value += z4;
		if (((MGL_OBJ_SLIDER*)slider1->object)->value <= 0 ||
			((MGL_OBJ_SLIDER*)slider1->object)->value >= 100)
			z4 = -z4;

		loshad_tex.alpha += z5;
		if (!(counter % 5))  {
			MGL_ObjectMove(loshad, -z5, 0);
		}
		if (loshad_tex.alpha == 20 ||
			loshad_tex.alpha == -20) z5 = -z5;

		MGL_ObjectMove(melnica, zz1, zz2);
		if (((MGL_OBJ_RECTANGLE*)melnica->object)->x1 < 0 ||
			((MGL_OBJ_RECTANGLE*)melnica->object)->x1 > lcd->Width - 60)
			zz1 = -zz1;
		if (((MGL_OBJ_RECTANGLE*)melnica->object)->y1 < 0 ||
			((MGL_OBJ_RECTANGLE*)melnica->object)->y1 > lcd->Height - 90)
			zz2 = -zz2;

		grad_fon->deg += 2;
		//texture_youtube.alpha += 2;

		counter++;

		/* calculate fps */
		frame++;
		if (esp_cpu_get_cycle_count() - tick >= ticks_in_sec) {
			utoa(frame, &fps_s[6], 10);
			frame = 0;
			tick = esp_cpu_get_cycle_count();
		}
		MGL_GRADIENT_POINT *points_list = grad_fon->points_list;
		int f = 0;
		while (points_list) {
			if (f) {
				if (points_list->next) {
					points_list->offset += z6;
				}
			}
			else {
				f = 1;
			}
			points_list = (MGL_GRADIENT_POINT*)points_list->next;
		}
		step_z6++;
		if (step_z6 > 20) {
			step_z6 = 0;
			z6 = -z6;
		}
	}
}

/*
 * SPI initialization
 * Parameters:
 * spi: SPI1, SPI2 or SPI3
 * spi_mode - configure polarity: CPOL, CPHA
 * dma_channel: 0 - no DMA, 1 - channel 1, 2 - channel 2
 * Return: 0 - no error, 1 - error.
 */
static int SPI_Init (spi_dev_t *spi, uint32_t spi_mode, uint32_t dma_channel)
{
	if (spi == &SPI3) { //Enabling SPI3 via IOMUX
		DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_SPI3_CLK_EN); //SPI3 clock enable
    	DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_SPI3_RST);  //SPI3 reset
    	//esp32 fast VSPI (SPI3) (up to 80 MHz), uses IOMUX (GPIO23 -> MOSI, GPIO18 - CLK)
    	//------------------------------ MOSI line (GPIO23) ----------------------------------
    	GPIO.func_in_sel_cfg[VSPID_IN_IDX].sig_in_sel = 0; //gpio pin via io_mux
    	//io_mux input enable, pull_up, strength default = 20mA, function 1 (spi)
    	SET_PERI_REG_MASK(IO_MUX_GPIO23_REG, FUN_IE | FUN_PU | (2 << FUN_DRV_S) | (1 << MCU_SEL_S));
    	GPIO.func_out_sel_cfg[23].oen_sel = 0; //use output enable signal from peripheral
    	GPIO.func_out_sel_cfg[23].oen_inv_sel = 0; //signal inversion -> disabled
    	//------------------------------ CLK line (GPIO18) ----------------------------------
    	GPIO.func_in_sel_cfg[VSPICLK_IN_IDX].sig_in_sel = 0;
    	SET_PERI_REG_MASK(IO_MUX_GPIO18_REG, FUN_IE | FUN_PU | (2 << FUN_DRV_S) | (1 << MCU_SEL_S)); //FUN = 1 (spi)
    	GPIO.func_out_sel_cfg[18].oen_sel = 0;
    	GPIO.func_out_sel_cfg[18].oen_inv_sel = 0;
    	if (dma_channel) {
    		//SPI_DMA clock enable
    		DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_SPI_DMA_CLK_EN);
    		//SPI_DMA reset
    		DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_SPI_DMA_RST);
			//Set DMA channel for SPI
   			DPORT_SET_PERI_REG_BITS(DPORT_SPI_DMA_CHAN_SEL_REG, DPORT_SPI3_DMA_CHAN_SEL_V, dma_channel, DPORT_SPI3_DMA_CHAN_SEL_S);
   			//Adding an interrupt handler, disabling all interrupts, clearing interrupt status flags.
   			esp_intr_alloc(ETS_SPI3_INTR_SOURCE, ESP_INTR_FLAG_LOWMED, LCD_TC_Callback, (void*)&SPI3, NULL);
   			SPI3.slave.val &= ~0x3ff;
    	}
	}
	else if (spi == &SPI2) { //Enabling SPI3 via IOMUX
		DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_SPI2_CLK_EN);
		DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_SPI2_RST);
		//esp32 fast HSPI (SPI2) (up to 80 MHz), uses IOMUX (GPIO13 -> MOSI, GPIO14 - CLK)
		//------------------------------ MOSI line (GPIO13) ----------------------------------
		GPIO.func_in_sel_cfg[HSPID_IN_IDX].sig_in_sel = 0;
		SET_PERI_REG_MASK(IO_MUX_GPIO13_REG, FUN_IE | FUN_PU | (2 << FUN_DRV_S) | (1 << MCU_SEL_S));
		GPIO.func_out_sel_cfg[13].oen_sel = 0;
		GPIO.func_out_sel_cfg[13].oen_inv_sel = 0;
		//------------------------------ CLK line (GPIO14) ----------------------------------
		GPIO.func_in_sel_cfg[HSPICLK_IN_IDX].sig_in_sel = 0;
		SET_PERI_REG_MASK(IO_MUX_GPIO14_REG, FUN_IE | FUN_PU | (2 << FUN_DRV_S) | (1 << MCU_SEL_S));
		GPIO.func_out_sel_cfg[14].oen_sel = 0;
		GPIO.func_out_sel_cfg[14].oen_inv_sel = 0;
		if (dma_channel) {
			DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_SPI_DMA_CLK_EN);
			DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_SPI_DMA_RST);
			DPORT_SET_PERI_REG_BITS(DPORT_SPI_DMA_CHAN_SEL_REG, DPORT_SPI2_DMA_CHAN_SEL_V, dma_channel, DPORT_SPI2_DMA_CHAN_SEL_S);
			esp_intr_alloc(ETS_SPI2_INTR_SOURCE, ESP_INTR_FLAG_LOWMED, LCD_TC_Callback, (void*)&SPI2, NULL);
			SPI2.slave.val &= ~0x3ff;
		}
	}
	else {
		return 1; //SPI selection error
	}

	if (spi_mode > 3) return 2; //SPI mode selection error

	//Configure CPOL, CPHA
	uint8_t polarity_modes[] = {0, 0, 0, 1, 1, 0, 1, 1};
	spi->pin.ck_idle_edge = polarity_modes[2 * spi_mode];
	spi->user.ck_out_edge = polarity_modes[2 * spi_mode + 1];

	spi->slave.val = 0; 			//SPI master ON
									//disable all interrupts
									//clear all interrupt status bits

	spi->ctrl.val = 0;				//MSB first for transmitted data
									//MSB first for received data

	spi->user.val = SPI_USR_MOSI |	//Enable the write-data phase of an operation
					SPI_DOUTDIN;	//Enable full duplex communication
									//Disable the read-data phase of an operation
									//MOSI data is stored in SPI_W0 - SPI_W15 of the SPI buffer
									//Three-line half-duplex communication OFF
									//Little-endian for transmitted data
									//Little-endian for received data
									//Disables address phase
									//Disables command phase
									//Disables dymmy phase
									//Disables a delay between CS active edge
									//Disables a delay between the end of a transmission and CS being

	spi->ctrl2.val = 0;				//clear all delays

	//Setting SPI clock. Default SPI clock is 40 MHz
	spi->clock.clk_equ_sysclk = 0;	//0 - SPI clock is divided from system clock, 1 - SPI clock is equal to system clock
	spi->clock.clkdiv_pre = 0;		//if clk_equ_sysclk == 1 spi_clock = APB_clock (80 MHz)
	spi->clock.clkcnt_l = 1;		//if clk_equ_sysclk == 0 spi_clock = APB_clock / ((clkcnt_n + 1)*(clkdiv_pre + 1))
	spi->clock.clkcnt_n = 1;		//clkcnt_n = clkcnt_l
	spi->clock.clkcnt_h = 0;		//Attetion!!! clkcnt_h = (clkcnt_n + 1) / 2 - 1, allways clkcnt_n >= 1

	return 0; //no error
}

static void pin_output_iomux_init(int pin_num, uint32_t level)
{
	if (pin_num < 0 || pin_num >= GPIO_NUM_MAX) return; //Pin number out of range
	if (pin_num >= 34 && pin_num <= 39) return;	//Оutput mode is not available for this pin number
	if (!GPIO_PIN_MUX_REG[pin_num]) return;		//Unused pin
	if (pin_num < 32) {
		GPIO.enable_w1ts = 1UL << pin_num;
	}
	else {
		GPIO.enable1_w1ts.data = 1UL << (pin_num - 32);
	}
	//pull up, fuction = 2 (gpio), strenght = default (20 ma)
	SET_PERI_REG_MASK(GPIO_PIN_MUX_REG[pin_num], FUN_IE | FUN_PU | (2 << FUN_DRV_S) | (2 << MCU_SEL_S));
	GPIO.func_out_sel_cfg[pin_num].oen_sel = 0;
	GPIO.func_out_sel_cfg[pin_num].oen_inv_sel = 0;
	if (level) {
		if (pin_num < 32) {
			GPIO.out_w1ts = 1UL << pin_num;
		}
		else {
			GPIO.out1_w1ts.data = 1UL << (pin_num - 32);
		}
	}
	else {
		if (pin_num < 32) {
			GPIO.out_w1tc = 1UL << pin_num;
		}
		else {
			GPIO.out1_w1tc.data = 1UL << (pin_num - 32);
		}
	}
}

static void GPIO_Init (void)
{
	pin_output_iomux_init(CS_PIN, 1);
	pin_output_iomux_init(DC_PIN, 1);
	pin_output_iomux_init(RST_PIN, 1);
	pin_output_iomux_init(BCKL_PIN, 0);
}

void demo2 (void *par)
{
	iPicture_jpg file;
	LCD_Handler *lcd = (LCD_Handler *)par;
	char buf[100] = "Decoded in ";
	int i = 5;
	while (i--) {
		file.data = (uint8_t*)image1_jpg_start;
		file.size = image1_jpg_end - image1_jpg_start;
		uint32_t tick = esp_cpu_get_cycle_count();
		LCD_Load_JPG_chan(lcd, 0, 0, lcd->Width, lcd->Height, &file, PICTURE_IN_MEMORY);
		tick = esp_cpu_get_cycle_count() - tick;
		utoa(tick, &buf[11], 10);
		strcat(buf, " ticks");
		LCD_WriteString(lcd, 0, 0, buf, &Font_12x20, COLOR_BLACK, COLOR_WHITE, LCD_SYMBOL_PRINT_FAST);
		printf("\nJPEG decoding time = %lu ticks", tick);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}

void app_main(void)
{
	//------------------------------------- SPI initialization ---------------------------
	GPIO_Init();
	(void)SPI_Init(&SPI_, SPI_mode, DMA_ch); 	//SPI_ = SPI2, SPI3;
											 	//mode = 0, 1, 2, 3;
												//DMA channel: 1 or 2, 0 - if DMA not used
	//------------------- Display initialization -----------------------------------------
	LCD_BackLight_data bl_dat = { .blk_pin = BCKL_PIN,
								  .bk_percent = 75 };

	LCD_SPI_Connected_data spi_dat = { .spi = &SPI_,
									   .dma_channel = DMA_ch,
 									   .reset_pin = RST_PIN,
 									   .dc_pin = DC_PIN,
 									   .cs_pin = CS_PIN  };
#if ACT_DISPLAY == ST7789
	//for display ST7789
	LCD = LCD_DisplayAdd( LCD,
						  240,
						  240,
						  ST7789_CONTROLLER_WIDTH,
						  ST7789_CONTROLLER_HEIGHT,
						  0,
						  0,
						  ST7789_Init,
						  ST7789_SetWindow,
						  ST7789_SleepIn,
						  ST7789_SleepOut,
						  ST7789_SetOrientation,
						  &spi_dat,
						  bl_dat );
#elif ACT_DISPLAY == ILI9341
	//for display ili9341
	LCD = LCD_DisplayAdd( LCD,
						  320,
						  240,
						  ILI9341_CONTROLLER_WIDTH,
						  ILI9341_CONTROLLER_HEIGHT,
						  0,
						  0,
						  ILI9341_Init,
						  ILI9341_SetWindow,
						  ILI9341_SleepIn,
						  ILI9341_SleepOut,
						  ILI9341_SetOrientation,
						  &spi_dat,
						  bl_dat );
#endif

	LCD_Handler *lcd = LCD; //pointer to first display in displays list
	LCD_Init(lcd);
#ifdef HI_SPEED
	//The SPI frequency increases AFTER(!) initialization of the display
	SPI3.clock.clk_equ_sysclk = 1; //SPI_clock = APB_clock = 80 MHz
#endif
	//display orientation
	LCD_SetOrientation(lcd, PAGE_ORIENTATION_PORTRAIT);
	//display fill color
	LCD_Fill(lcd, 0x319bb1);
	LCD_WriteString(lcd, 0, 0, "Hello, world!", &Font_15x25, COLOR_YELLOW, 0x319bb1, LCD_SYMBOL_PRINT_FAST);
	vTaskDelay(2000 / portTICK_PERIOD_MS);

	printf("\nFree memory MALLOC_CAP_8BIT: %u bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

	//jpeg decoding
	demo2(lcd);

	//graphic rendering
	render_buf1 = heap_caps_malloc(RENDER_BUFFER_LINES * lcd->Width * sizeof(uint16_t), MALLOC_CAP_DMA);
	if (lcd->spi_data.dma_channel) {
		render_buf2 = heap_caps_malloc(RENDER_BUFFER_LINES * lcd->Width * sizeof(uint16_t), MALLOC_CAP_DMA);
	}
	xTaskCreatePinnedToCore(demo, "render_task", 8192, (void*)lcd, 4, NULL, 1);
}
