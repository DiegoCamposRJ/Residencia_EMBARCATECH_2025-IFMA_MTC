#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <stdlib.h>
#include "hardware/pwm.h"
#include "hardware/clocks.h" // Adicionado para clock_get_hz e clk_sys


/* ――― Pinos do Joystick ――― */
#define JOYSTICK_VRX 27
#define JOYSTICK_VRY 26
/* ――― Pino do Buzzer PWM ――― */
#define BUZZER_PWM_PIN 21
/* ――― Pino do Buzzer Digital ――― */
#define BUZZER_DIGITAL_PIN 10
/* ――― Pinos do LED RGB ――― */
#define LED_R_PIN 13
#define LED_G_PIN 11
#define LED_B_PIN 12

/* ――― Estado Global (Volátil) ――― */
volatile uint8_t global_state = 0; // 0 = baixo, 1 = moderado, 2 = alto, 3 = crítico

/* ――― Limiares de Atividade (Ajuste Conforme Necessário) ――― */
#define LOW_THRESHOLD 1000
#define MODERATE_THRESHOLD 2500
#define HIGH_THRESHOLD 3500
#define CRITICAL_THRESHOLD 3800

/* ――― Período do Alarme (Núcleo 0) ――― */
#define ALARM_PERIOD_MS 50

void inicializar_pino(uint pino, uint direcao)
{
    gpio_init(pino);
    gpio_set_dir(pino, direcao);
}

/* ――― Função de Alarme (Núcleo 0) ――― */
int64_t joystick_alarm_callback(alarm_id_t id, void *user_data)
{
    adc_select_input(0);
    uint16_t vry_value = adc_read();
    adc_select_input(1);
    uint16_t vrx_value = adc_read();
    uint32_t joystick_data = (vrx_value << 16) | vry_value;
    multicore_fifo_push_blocking(joystick_data);
    printf("[CORE 0] VRx: %d, VRy: %d (enviado para CORE 1)\n", vrx_value, vry_value);
    return ALARM_PERIOD_MS * 1000;
}

/* ───────────────────────── Núcleo 0 ───────────────────────── */
static void core1_entry(void);

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);
    adc_init();
    adc_gpio_init(JOYSTICK_VRY);
    adc_gpio_init(JOYSTICK_VRX);
    multicore_launch_core1(core1_entry);
    add_alarm_in_ms(ALARM_PERIOD_MS, joystick_alarm_callback, NULL, true);
    while (true)
    {
        tight_loop_contents();
    }
}

/* ───────────────────────── Núcleo 1 ───────────────────────── */
static void core1_entry(void)
{
    /* --- Inicialização dos Buzzers --- */
    inicializar_pino(BUZZER_PWM_PIN, GPIO_OUT); // Inicializado como saída para PWM
    inicializar_pino(BUZZER_DIGITAL_PIN, GPIO_OUT); // Inicializado para controle digital
    gpio_put(BUZZER_PWM_PIN, 0);
    gpio_put(BUZZER_DIGITAL_PIN, 0);

    /* --- Inicialização do LED RGB --- */
    inicializar_pino(LED_R_PIN, GPIO_OUT);
    inicializar_pino(LED_G_PIN, GPIO_OUT);
    inicializar_pino(LED_B_PIN, GPIO_OUT);
    gpio_put(LED_R_PIN, 0);
    gpio_put(LED_G_PIN, 0);
    gpio_put(LED_B_PIN, 0);

    /* --- Configuração do PWM para o Primeiro Buzzer --- */
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PWM_PIN);
    uint channel = pwm_gpio_to_channel(BUZZER_PWM_PIN);
    uint32_t sys_clk_khz = clock_get_hz(clk_sys) / 1000;
    uint32_t divider = sys_clk_khz / 1000;
    pwm_set_clkdiv(slice_num, divider);
    pwm_set_wrap(slice_num, 100);
    pwm_set_chan_level(slice_num, channel, 50);
    pwm_set_enabled(slice_num, false); // Desabilitado inicialmente

    /* Loop principal: aguarda dados do joystick e controla os elementos */
    while (true)
    {
        if (multicore_fifo_rvalid())
        {
            uint32_t joystick_data = multicore_fifo_pop_blocking();
            uint16_t received_vrx = (joystick_data >> 16) & 0xFFFF;
            uint16_t received_vry = joystick_data & 0xFFFF;

            printf("[CORE 1] Recebeu VRx: %d, VRy: %d\n", received_vrx, received_vry);

            uint16_t activity_level = (abs(2048 - received_vrx) + abs(2048 - received_vry));

            // Determina o estado global
            if (activity_level > CRITICAL_THRESHOLD)
            {
                global_state = 3; // Crítico
            }
            else if (activity_level > HIGH_THRESHOLD)
            {
                global_state = 2; // Alto
            }
            else if (activity_level > MODERATE_THRESHOLD)
            {
                global_state = 1; // Moderado
            }
            else
            {
                global_state = 0; // Baixo
            }
            // Controle do Buzzer PWM (GPIO 21) - BIPE
            if (global_state == 3)
            {
                uint32_t alarm_freq = 1500;
                uint32_t alarm_divider = sys_clk_khz / alarm_freq;
                pwm_set_clkdiv(slice_num, alarm_divider);
                pwm_set_chan_level(slice_num, channel, 90); // Aumenta o ciclo de trabalho para 90%
                pwm_set_enabled(slice_num, true);
                sleep_ms(200); // Duração do bipe (ajuste conforme necessário)
                pwm_set_enabled(slice_num, false);
            }
            else
            {
                pwm_set_enabled(slice_num, false);
            }

            // Controle do Buzzer Digital (GPIO 10) - Exemplo: Ativa no estado ALTO
            gpio_put(BUZZER_DIGITAL_PIN, (global_state == 2));

            // Controle do LED RGB
            gpio_put(LED_R_PIN, (global_state == 2));
            gpio_put(LED_G_PIN, (global_state == 1));
            gpio_put(LED_B_PIN, (global_state == 0));

            sleep_ms(10);
        }
        tight_loop_contents();
    }
}