#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_MEM_CUSTOM  0
#define LV_MEM_SIZE    (128 * 1024U)

#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF     130

#define LV_DRAW_COMPLEX        1
#define LV_SHADOW_CACHE_SIZE   0
#define LV_CIRCLE_CACHE_SIZE   4
#define LV_IMG_CACHE_DEF_SIZE  0
#define LV_GRADIENT_MAX_STOPS  2
#define LV_GRAD_CACHE_DEF_SIZE 0
#define LV_DITHER_GRADIENT     0
#define LV_DISP_ROT_MAX_BUF    (10 * 1024)

#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_SWM341_DMA  0
#define LV_USE_GPU_NXP_PXP     0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL         0

#define LV_USE_LOG    1
#define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_ASSERT_HANDLER_INCLUDE   <stdint.h>
#define LV_ASSERT_HANDLER           while(1);

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

#define LV_TXT_ENC                          LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS                  " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN          0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN  3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD                    "#"
#define LV_USE_BIDI                         0
#define LV_USE_ARABIC_PERSIAN_CHARS         0

#define LV_USE_ARC       1
#define LV_USE_BAR       1
#define LV_USE_BTN       1
#define LV_USE_BTNMATRIX 0
#define LV_USE_CANVAS    0
#define LV_USE_CHECKBOX  0
#define LV_USE_DROPDOWN  0
#define LV_USE_IMG       0
#define LV_USE_LABEL     1
#  define LV_LABEL_TEXT_SELECTION 1
#  define LV_LABEL_LONG_TXT_HINT  1
#define LV_USE_LINE      0
#define LV_USE_ROLLER    0
#define LV_USE_SLIDER    0
#define LV_USE_SWITCH    0
#define LV_USE_TEXTAREA  0
#define LV_USE_TABLE     0
#define LV_USE_LED       0
#define LV_USE_MSGBOX    0
#define LV_USE_SPINBOX   0
#define LV_USE_SPINNER   0
#define LV_USE_TABVIEW   0
#define LV_USE_TILEVIEW  0
#define LV_USE_WIN       0
#define LV_USE_SPAN      0
#define LV_USE_MENU      0
#define LV_USE_METER     0

#define LV_USE_FLEX      1
#define LV_USE_GRID      0
#define LV_USE_ANIMATION 1

#define LV_USE_THEME_DEFAULT 1
#  define LV_THEME_DEFAULT_DARK            1
#  define LV_THEME_DEFAULT_GROW            1
#  define LV_THEME_DEFAULT_TRANSITION_TIME 80

#define LV_USE_SNAPSHOT   0
#define LV_USE_MONKEY     0
#define LV_USE_GRIDNAV    0
#define LV_USE_FRAGMENT   0
#define LV_USE_IMGFONT    0
#define LV_USE_MSG        0
#define LV_USE_IME_PINYIN 0
#define LV_BUILD_EXAMPLES 0

#endif
#endif
