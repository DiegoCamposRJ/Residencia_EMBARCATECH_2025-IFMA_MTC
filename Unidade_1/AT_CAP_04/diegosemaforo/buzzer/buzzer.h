#ifndef BUZZER_H
#define BUZZER_H

void buzzer_init(void);
void buzzer_beep(uint duration_ms);
void buzzer_pwm_on(uint freq_hz);
void buzzer_pwm_off(void);

#endif
