#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

void buzzer_init(void);
void buzzer_beep(uint32_t duration_ms);
void buzzer_beep_order(void);
void buzzer_beep_bill(void);

#endif
