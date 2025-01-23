/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/*----------------------------------------------*/
/*
 *  Portions сopyright (C) 2022-2023, VadRov, all right reserved.
 *  Оптимизация (в т.ч., для stm32), улучшения, дополнения:
 *  - В структуре JDEC буфер workbuf увеличен в два раза, введен указатель смещения
 *  в этом буфере для использования DMA. Декодер не простаивает, ожидая вывода на
 *  дисплей ранее декодированного блока;
 *  - Введен дополнительный формат цвета пикселя Grayscale_16. 32 оттенка серого в 16-битном
 *    формате цвета R5G6B5. Производительность в сравнении с цветным RGB565 выше
 *    до 1.5 - 2 раз.
 * 	- Оптимизирована процедура mcu_output: существенно улучшена производительность
 * 	  для цвета в формате R5G6B5.
 * 	  Исключен промежуточный этап преобразования цвета из формата R8G8B8 в R5G6B5. R5G6B5 теперь
 * 	  формируется непосредственно без дополнительных преобразований;
 * 	- Оптимизирована процедура mcu_load;
 * 	- Реализованы процедуры быстрого преобразования YCbCr в R5G6B5 и R8G8B8, Y в 16
 * 	  битный монохромный (32 оттенка серого);
 * 	- Реализовано быстрое заполнение областей памяти заданным значением шириной:
 * 			- в полуслово (memset_16 - asm);
 * 			- в слово (memset_32 - asm).
 * 	- Реализовано быстрое копирование областей памяти memcpy_16 по полусловам;
 * 	Для подключения оптимизации используется определение JD_FAST_OPTIMIZE в заголовочном файле
 *  tjpgdcnf.h
 *
 * https://www.youtube.com/@VadRov
 * https://dzen.ru/vadrov
 * https://vk.com/vadrov
 *----------------------------------------------------------------------------------------------*/

/* Оптимизация
/ 0: Выключена
/ 1: Включена
 */
#define JD_FAST_OPTIMIZE	1

#define JD_BYTES_SWAP		1

/* Specifies size of stream input buffer */
#define	JD_SZBUF		1024


/* Specifies output pixel format.
/  0: RGB888 (24-bit/pix)
/  1: RGB565 (16-bit/pix)
/  2: Grayscale (8-bit/pix)
/  3: Grayscale_16 (16-bit/pix) //(32 оттенка серого в формате 16-битного RGB565). Добавлено VadRov
/								//не поддерживается при отключенной оптимизации JD_FAST_OPTIMIZE
*/
#define JD_FORMAT		1


/* Switches output descaling feature.
/  0: Disable
/  1: Enable
*/
#define	JD_USE_SCALE	1


/* Use table conversion for saturation arithmetic. A bit faster, but increases 1 KB of code size.
/  0: Disable
/  1: Enable
*/
#define JD_TBLCLIP		1 //Опция не влияет на производительность в режиме JD_FAST_OPTIMIZE
						  //Функционал данной опции полностью отключен в режиме JD_FAST_OPTIMIZE.


/* Optimization level
/  0: Basic optimization. Suitable for 8/16-bit MCUs.
/  1: + 32-bit barrel shifter. Suitable for 32-bit MCUs.
/  2: + Table conversion for huffman decoding (wants 6 << HUFF_BIT bytes of RAM)
*/
#define JD_FASTDECODE	2
