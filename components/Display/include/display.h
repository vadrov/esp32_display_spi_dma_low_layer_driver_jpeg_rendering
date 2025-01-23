/*
 *	Драйвер управления дисплеями по SPI
 *  Author: VadRov
 *  Copyright (C) 2019 - 2024, VadRov, all right reserved.
 *
 *  Допускается свободное распространение.
 *  При любом способе распространения указание автора ОБЯЗАТЕЛЬНО.
 *  В случае внесения изменений и распространения модификаций указание первоначального автора ОБЯЗАТЕЛЬНО.
 *  Распространяется по типу "как есть", то есть использование осуществляется на свой страх и риск.
 *  Автор не предоставляет никаких гарантий.
 *
 *  Версия: 1.4 ESP32-WROOM
 *
 *  https://www.youtube.com/@VadRov
 *  https://dzen.ru/vadrov
 *  https://vk.com/vadrov
 *  https://t.me/vadrov_channel
 */

#ifndef INC_DISPLAY_H_
#define INC_DISPLAY_H_

#include "soc/spi_struct.h"
#include "fonts.h"

//некоторые предопределенные цвета
//формат 0xRRGGBB
#define	COLOR_BLACK			0x000000
#define	COLOR_BLUE			0x0000FF
#define	COLOR_RED			0xFF0000
#define	COLOR_GREEN			0x00FF00
#define COLOR_CYAN			0x00FFFF
#define COLOR_MAGENTA		0xFF00FF
#define COLOR_YELLOW		0xFFFF00
#define COLOR_WHITE			0xFFFFFF
#define COLOR_NAVY			0x000080
#define COLOR_DARKGREEN		0x2F4F2F
#define COLOR_DARKCYAN		0x008B8B
#define COLOR_MAROON		0xB03060
#define COLOR_PURPLE		0x800080
#define COLOR_OLIVE			0x808000
#define COLOR_LIGHTGREY		0xD3D3D3
#define COLOR_DARKGREY		0xA9A9A9
#define COLOR_ORANGE		0xFFA500
#define COLOR_GREENYELLOW	0xADFF2F

//статусы дисплея
typedef enum {
	LCD_STATE_READY,
	LCD_STATE_BUSY,
	LCD_STATE_ERROR,
	LCD_STATE_UNKNOW
} LCD_State;

//ориентация дисплея
typedef enum {
	PAGE_ORIENTATION_PORTRAIT,			//портрет - по умолчанию
	PAGE_ORIENTATION_LANDSCAPE,			//пейзаж
	PAGE_ORIENTATION_PORTRAIT_MIRROR,	//портрет перевернуто
	PAGE_ORIENTATION_LANDSCAPE_MIRROR	//пейзаж перевернуто
} LCD_PageOrientation;

//режимы печати символов
typedef enum {
	LCD_SYMBOL_PRINT_FAST,		//быстрый с затиранием фона
	LCD_SYMBOL_PRINT_PSETBYPSET	//медленный, по точкам, без затирания фона
} LCD_PrintSymbolMode;

//данные spi подключения
typedef struct {
	spi_dev_t *spi;
	uint32_t dma_channel;
	int reset_pin;
	int dc_pin;
	int cs_pin;
} LCD_SPI_Connected_data;

//подсветка
typedef struct {
	int blk_pin;
	uint8_t bk_percent;
} LCD_BackLight_data;

//коллбэки
typedef const uint8_t* (*DisplayInitCallback)(void);
typedef const uint8_t* (*DisplaySetWindowCallback)(uint16_t, uint16_t, uint16_t, uint16_t);
typedef const uint8_t* (*DisplaySleepInCallback)(void);
typedef const uint8_t* (*DisplaySleepOutCallback)(void);
typedef const uint8_t* (*DisplaySetOrientationCallback)(LCD_PageOrientation);

//позиция печати
typedef struct {
	uint16_t x;
	uint16_t y;
} LCD_xy_pos;

typedef struct {
	volatile uint32_t size:       12, //Размер буфера, соответствующий текущему связанному списку. Должен быть выровнен по слову.
					  lenght:     12, //Количество байтов в буфере, соответствующем текущему связанному списку.
						 	 	      //Указывается количество байтов, которые будут переданы в/из буфера, указанного в DW1.
					  reserved:    6, //зарезирвировано -  не записывать в эти биты 1!
					  eof:    	   1, //0 - это не последний элемент в связанном списке, 1 - это последний элемент
					  owner:       1; //0 - operator CPU, 1 - operator DMA
	volatile const uint8_t *buf; 	  //Адрес буфера с данными для текущего элемента связанного списка. Должен быть выровнен по слову.
	void *next_link;				  //Указатель на следующий элемент связанного списка.
									  //0, если это последний элемент списка (бит eof = 1).
} LCD_DMA_descriptor_link;

//обработчик дисплея
typedef struct {
	uint16_t Width_Controller;    	//максимальная ширина матрицы, поддерживаемая контроллером дисплея, пиксели
	uint16_t Height_Controller;		//максимальная высота матрицы, поддерживаемая контроллером дисплея, пиксели
	uint16_t Width;					//фактическая ширина матрицы используемого дисплея, пиксели
	uint16_t Height;				//фактическая высота матрицы используемого дисплея, пиксели
	LCD_PageOrientation Orientation;//ориентация дисплея
	int x_offs;						//смещение по x
	int y_offs;						//смещение по y
	int w_offs, h_offs;
	uint16_t w_cntrl, h_cntrl;
	LCD_xy_pos AtPos;				//текущая позиция печати символа
	DisplayInitCallback Init_callback;					//коллбэк инициализации
	DisplaySetWindowCallback SetActiveWindow_callback;	//коллбэк установки окна вывода
	DisplaySleepInCallback SleepIn_callback;			//коллбэк "входа в сон"
	DisplaySleepOutCallback SleepOut_callback;			//коллбэк "выхода из сна"
	DisplaySetOrientationCallback SetOrientation_callback; //коллбэк установки ориентации страницы
	LCD_SPI_Connected_data spi_data;					//данные подключения по SPI
	LCD_BackLight_data bkl_data;						//данные подсветки
	uint8_t display_number;								//номер дисплея
	LCD_DMA_descriptor_link *dma_descriptor_link;		//связанный список ссылок для DMA дескриптора
	__attribute__((aligned(4)))	uint32_t fill_color;
	uint8_t	cs_control, dc_control;
	void *prev;					//указатель на предыдующий дисплей
	void *next;					//указатель на следующий дисплей
} LCD_Handler;

extern LCD_Handler *LCD;		//указатель на список дисплеев (первый дисплей в списке)

void LCD_SetCS(LCD_Handler *lcd);
void LCD_ResCS(LCD_Handler *lcd);
void LCD_SetDC(LCD_Handler *lcd);
void LCD_ResDC(LCD_Handler *lcd);

void LCD_TC_Callback(void *arg);

//создает обработчик дисплея и добавляет его в список дисплеев
//возвращает указатель на созданный дисплей либо 0 при неудаче
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
					   );

//удаляет обработчик дисплея
void LCD_Delete(LCD_Handler* lcd);
//аппаратный сброс дисплея
void LCD_HardWareReset(LCD_Handler* lcd);
//инициализация дисплея
void LCD_Init(LCD_Handler* lcd);
//интерпретатор командных строк
void LCD_String_Interpretator(LCD_Handler* lcd, const uint8_t *str);
//установка яркости дисплея
void LCD_SetBackLight(LCD_Handler* lcd, uint8_t bk_percent);
//возвращает текущую яркость дисплея
uint8_t LCD_GetBackLight(LCD_Handler* lcd);
//возвращает ширину дисплея, пиксели
uint16_t LCD_GetWidth(LCD_Handler* lcd);
//возвращает высоту дисплея, пиксели
uint16_t LCD_GetHeight(LCD_Handler* lcd);
//возвращает статус дисплея
LCD_State LCD_GetState(LCD_Handler* lcd);
//переводит дисплей в режим сна
void LCD_SleepIn(LCD_Handler* lcd);
//выводит дисплей из режима сна
void LCD_SleepOut(LCD_Handler* lcd);
//устанавливает окно вывода
void LCD_SetActiveWindow(LCD_Handler* lcd, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
//задает ориентацию изображения на дисплее
void LCD_SetOrientation(LCD_Handler* lcd, LCD_PageOrientation orientation);
//отправляет данные на дисплей - без DMA
void LCD_WriteData(LCD_Handler *lcd, uint16_t *data, uint32_t len);
//отправляет данные на дисплей с использованием DMA
void LCD_WriteDataDMA(LCD_Handler *lcd, uint16_t *data, uint32_t len);
//заливает окно с заданными координатами заданным цветом
void LCD_FillWindow(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);
//заливает весь экран заданным цветом
void LCD_Fill(LCD_Handler* lcd, uint32_t color);
//рисует точку в заданных координатах заданным цветом
void LCD_DrawPixel(LCD_Handler* lcd, int16_t x, int16_t y, uint32_t color);
//рисует линию по заданным координатам заданным цветом
void LCD_DrawLine(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color);
//рисует прямоугольник по заданным координатам заданным цветом
void LCD_DrawRectangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t color);
//рисует закрашенный прямоугольник по заданным координатам заданным цветом
void LCD_DrawFilledRectangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t color);
//рисует треугольник
void LCD_DrawTriangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint32_t color);
//рисует закрашенный треугольник
void LCD_DrawFilledTriangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint32_t color);
//рисует окружность с заданным центром и радиусом с заданным цветом
void LCD_DrawCircle(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t r, uint32_t color);
//рисует закрашенную окружность
void LCD_DrawFilledCircle(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t r, uint32_t color);
//пересылает на дисплей блок памяти (например, кусок изображения)
void LCD_DrawImage(LCD_Handler* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *data, uint8_t dma_use_flag);
//выводит символ в указанной позиции
void LCD_WriteChar(LCD_Handler* lcd, uint16_t x, uint16_t y, char ch, FontDef *font, uint32_t txcolor, uint32_t bgcolor, LCD_PrintSymbolMode modesym);
//выводит строку символов с указанной позиции
void LCD_WriteString(LCD_Handler* lcd, uint16_t x, uint16_t y, const char *str, FontDef *font, uint32_t color, uint32_t bgcolor, LCD_PrintSymbolMode modesym);
//преобразует цвет в формате R8G8B8 (24 бита) в 16 битовый R5G6B5
uint16_t LCD_Color(LCD_Handler *lcd, uint8_t r, uint8_t g, uint8_t b);
uint16_t LCD_Color_24b_to_16b(LCD_Handler *lcd, uint32_t color);

#endif /* INC_DISPLAY_H_ */
