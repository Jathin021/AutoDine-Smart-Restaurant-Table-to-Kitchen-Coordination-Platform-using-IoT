/* hardware_compat.h — stub so ESP-IDF .c files compile in Arduino
 * Provides lvgl_acquire / lvgl_release declarations.
 * The actual implementations are in AutoDine_Table_Ino.ino (extern "C"). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void lvgl_acquire(void);
void lvgl_release(void);
#ifdef __cplusplus
}
#endif
