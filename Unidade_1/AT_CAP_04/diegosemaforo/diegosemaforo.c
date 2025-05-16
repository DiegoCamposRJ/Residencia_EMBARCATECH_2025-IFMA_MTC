/*RESIDÊNCIA PROFISSIONAL EM SISTEMAS EMBARCADOS-IFMA
 * Protótipo de Semáforo Inteligente para Pedestres
 * Autor: [Diego da Silva Campos do Nascimento]
 * Data: [16/05/2025]
 * Matrícula:20251RSE.MTC0017
 *
 * Descrição: Controla um semáforo com temporização programável,     
 *      botões para pedestres e feedback visual/sonoro.
 *
 * Repositório no GitHub - https://github.com/DiegoCamposRJ/Residencia_EMBARCATECH_2025-IFMA_MTC/tree/main/Unidade_1/AT_CAP_04/diegosemaforo
 * Projeto no Wokwi - https://wokwi.com/projects/431083943044407297
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "oled/ssd1306.h"
#include <string.h>
#include "buzzer/buzzer.h"

/******************************
 * DEFINIÇÕES DE HARDWARE
 ******************************/

// Configuração dos pinos GPIO
#define PIN_LED_RED     13  // LED vermelho do semáforo
#define PIN_LED_GREEN   11  // LED verde do semáforo
#define PIN_LED_BLUE    12  // LED azul (não utilizado no padrão tradicional)
#define PIN_BT_A        5   // Botão de pedestre A
#define PIN_BT_B        6   // Botão de pedestre B (opcional)
#define PIN_SW          22  // Botão geral (não utilizado no código atual)
#define BUZZER_PIN      10  // Pino do buzzer ativo

/******************************
 * CONFIGURAÇÕES DE TEMPO
 ******************************/

// Tempos de cada estado do semáforo (em milissegundos)
#define TEMPO_VERMELHO  10000  // 10s no estado vermelho
#define TEMPO_VERDE     10000  // 10s no estado verde
#define TEMPO_AMARELO   3000   // 3s no estado amarelo
#define TEMPO_TRAVESSIA 10000  // 10s para travessia de pedestres

// Tempo de debounce para evitar ruído nos botões
#define DEBOUNCE_DELAY_MS 50  // 50ms para filtro de bouncing

/******************************
 * DEFINIÇÕES DE ESTADOS
 ******************************/

// Estados possíveis do semáforo
typedef enum {
    VERMELHO,  // Estado vermelho - parada obrigatória
    VERDE,     // Estado verde - passagem liberada
    AMARELO    // Estado amarelo - atenção/preparação para parar
} EstadoSemaforo;

// Estrutura para controle de debounce dos botões
typedef struct {
    absolute_time_t ultimo_acionamento;  // Timestamp do último acionamento
    bool estado_anterior;               // Estado anterior do botão
} DebounceData;

/******************************
 * VARIÁVEIS GLOBAIS
 ******************************/

// Controle de pedido de travessia
static volatile bool pedido_travessia = false;

// Controle do buzzer
static volatile bool buzzer_active = false;
static volatile alarm_id_t beep_alarm_id = -1;

// Temporização
static volatile int tempo_restante = 0;

// Display OLED
static uint8_t oled_buf[ssd1306_buffer_length];  // Buffer para o display
static struct render_area area_total = {          // Área de renderização
    .start_column = 0,
    .end_column = ssd1306_width - 1,
    .start_page = 0,
    .end_page = ssd1306_n_pages - 1
};

// Controle de debounce
static DebounceData debounce;

/******************************
 * PROTÓTIPOS DE FUNÇÕES
 ******************************/

// Funções de controle do buzzer
static int64_t parar_buzzer(alarm_id_t id, void *user_data);
static int64_t bip_periodico(alarm_id_t id, void *user_data);

// Funções de interface
static void atualizar_display_tempo();
static void gpio_callback(uint gpio, uint32_t events);
static void atualizar_semaforo(EstadoSemaforo estado);

// Função de temporização
static bool temporizador_callback(repeating_timer_t *t);

/******************************
 * IMPLEMENTAÇÃO DAS FUNÇÕES
 ******************************/

/**
 * @brief Callback para desligar o buzzer
 * @param id ID do alarme que disparou o callback
 * @param user_data Dados do usuário (não utilizado)
 * @return Retorno 0 indica que o alarme não deve ser repetido
 */
int64_t parar_buzzer(alarm_id_t id, void *user_data) {
    (void)user_data;  // Parâmetro não utilizado
    if (id == beep_alarm_id) {
        buzzer_pwm_off();     // Desliga o buzzer
        beep_alarm_id = -1;   // Reseta o ID do alarme
    }
    return 0;  // Não repetir o alarme
}

/**
 * @brief Callback para bip periódico do buzzer
 * @param id ID do alarme que disparou o callback
 * @param user_data Dados do usuário (não utilizado)
 * @return Intervalo para próximo bip (500ms) ou 0 se inativo
 */
int64_t bip_periodico(alarm_id_t id, void *user_data) {
    (void)id;
    (void)user_data;
    if (buzzer_active) {
        buzzer_pwm_on(4000);  // Liga o buzzer em 4kHz
        // Agenda desligamento após 100ms
        add_alarm_in_ms(100, parar_buzzer, NULL, false);
        return 500;  // Repete após 500ms
    }
    return 0;  // Não repetir se buzzer inativo
}

/**
 * @brief Atualiza o display com o tempo restante
 * 
 * Exibe o tempo restante no estado atual em fonte ampliada (scale2)
 * e centralizada na tela.
 */
static void atualizar_display_tempo() {
    char tempo_str[16];
    // Formata o tempo com 2 dígitos (ex: "05")
    snprintf(tempo_str, sizeof(tempo_str), "%02d", tempo_restante);

    // Calcula posição X para centralizar (considerando 12px por dígito)
    int num_digitos = strlen(tempo_str);
    int pos_x = (128 - (num_digitos * 12)) / 2;

    // Limpa o buffer e atualiza o display
    memset(oled_buf, 0, sizeof(oled_buf));
    ssd1306_draw_string(oled_buf, 0, 0, "Tempo restante:");
    ssd1306_draw_string_scale2(oled_buf, pos_x, 16, tempo_str);
    render_on_display(oled_buf, &area_total);
}

/**
 * @brief Callback de interrupção para os botões
 * @param gpio Pino GPIO que gerou a interrupção
 * @param events Tipo de evento (edge fall/rise)
 */
static void gpio_callback(uint gpio, uint32_t events) {
    // Verifica se foi um dos botões de pedestre
    if (gpio == PIN_BT_A || gpio == PIN_BT_B) {
        // Lê o estado atual (invertido por causa do pull-up)
        bool estado_atual = !gpio_get(gpio);

        // Obtém timestamp atual para debounce
        absolute_time_t agora = get_absolute_time();
        
        // Verifica condições de debounce:
        // - Estado mudou OU
        // - Tempo desde último acionamento > tempo de debounce
        if (estado_atual != debounce.estado_anterior || 
            absolute_time_diff_us(debounce.ultimo_acionamento, agora) > DEBOUNCE_DELAY_MS * 1000) {
            
            // Atualiza estado do debounce
            debounce.estado_anterior = estado_atual;
            debounce.ultimo_acionamento = agora;
            
            // Processa apenas quando o botão é pressionado (estado_atual = true)
            if (estado_atual) {
                pedido_travessia = true;  // Sinaliza pedido de travessia
                buzzer_active = true;     // Ativa o buzzer
                
                // Buzzer imediato (300ms)
                buzzer_pwm_on(4000);
                add_alarm_in_ms(300, parar_buzzer, NULL, false);
                
                // Inicia bips periódicos (500ms entre bips)
                beep_alarm_id = add_alarm_in_ms(500, bip_periodico, NULL, true);

                printf("Botão de Pedestres acionado\n");
                
                // Atualiza display
                memset(oled_buf, 0, sizeof(oled_buf));
                ssd1306_draw_string(oled_buf, 0, 0, "Pedido recebido");
                render_on_display(oled_buf, &area_total);
            }
        }
    }
}

/**
 * @brief Atualiza o estado do semáforo
 * @param estado Novo estado do semáforo (VERMELHO, VERDE, AMARELO)
 */
static void atualizar_semaforo(EstadoSemaforo estado) {
    const char *cor = "";  // String para exibição no display
    
    // Desliga todos os LEDs primeiro
    gpio_put(PIN_LED_RED, 0);
    gpio_put(PIN_LED_GREEN, 0);
    gpio_put(PIN_LED_BLUE, 0);

    // Atualiza LEDs conforme o estado
    switch (estado) {
        case VERMELHO:
            gpio_put(PIN_LED_RED, 1);  // Acende apenas vermelho
            cor = "Vermelho";
            break;
            
        case VERDE:
            gpio_put(PIN_LED_GREEN, 1);  // Acende apenas verde
            cor = "Verde";
            break;
            
        case AMARELO:
            // Amarelo = vermelho + verde (no semáforo tradicional)
            gpio_put(PIN_LED_RED, 1);
            gpio_put(PIN_LED_GREEN, 1);
            cor = "Amarelo";
            break;
    }
    
    printf("Sinal: %s\n", cor);
    
    // Atualiza display OLED
    memset(oled_buf, 0, sizeof(oled_buf));
    ssd1306_draw_string(oled_buf, 0, 0, "Sinal:");
    ssd1306_draw_string(oled_buf, 48, 0, (char *)cor);
    render_on_display(oled_buf, &area_total);
}

/**
 * @brief Callback do temporizador para contagem regressiva
 * @param t Estrutura do timer repetitivo
 * @return true para continuar o timer, false para parar
 */
static bool temporizador_callback(repeating_timer_t *t) {
    (void)t;  // Parâmetro não utilizado
    if (tempo_restante > 0) {
        tempo_restante--;  // Decrementa o tempo
        printf("Tempo restante: %d segundos\n", tempo_restante);
        atualizar_display_tempo();  // Atualiza display
    }
    return true;  // Continua o timer
}

/******************************
 * FUNÇÃO PRINCIPAL
 ******************************/

int main() {
    // Inicializações básicas
    stdio_init_all();
    
    // Inicializa estrutura de debounce
    debounce.ultimo_acionamento = nil_time;
    debounce.estado_anterior = false;
    
    /*** Configuração do Display OLED ***/
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(14, GPIO_FUNC_I2C);  // SDA
    gpio_set_function(15, GPIO_FUNC_I2C);  // SCL
    gpio_pull_up(14);  // Pull-up no SDA
    gpio_pull_up(15);  // Pull-up no SCL
    ssd1306_init();  // Inicializa display
    calculate_render_area_buffer_length(&area_total);  // Calcula buffer
    memset(oled_buf, 0, sizeof(oled_buf));  // Limpa buffer
    render_on_display(oled_buf, &area_total);  // Atualiza display

    /*** Configuração dos LEDs ***/
    gpio_init(PIN_LED_RED);
    gpio_set_dir(PIN_LED_RED, GPIO_OUT);
    gpio_init(PIN_LED_GREEN);
    gpio_set_dir(PIN_LED_GREEN, GPIO_OUT);
    gpio_init(PIN_LED_BLUE);
    gpio_set_dir(PIN_LED_BLUE, GPIO_OUT);
    

    /*** Configuração dos Botões ***/
    // Botão A com callback
    gpio_init(PIN_BT_A);
    gpio_set_dir(PIN_BT_A, GPIO_IN);
    gpio_pull_up(PIN_BT_A);  // Pull-up interno
    gpio_set_irq_enabled_with_callback(PIN_BT_A, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    // Botão B (opcional) sem callback específico
    gpio_init(PIN_BT_B);
    gpio_set_dir(PIN_BT_B, GPIO_IN);
    gpio_pull_up(PIN_BT_B);
    gpio_set_irq_enabled(PIN_BT_B, GPIO_IRQ_EDGE_FALL, true);

    // Inicializa buzzer
    buzzer_init();

    /*** Configuração do Temporizador Principal ***/
    repeating_timer_t timer;
    // Timer de 1s (1000ms) para contagem regressiva
    add_repeating_timer_ms(-1000, temporizador_callback, NULL, &timer);

    // Variável para controle de fluxo após travessia
    bool voltar_para_verde = false;

    /*** Loop Principal ***/
    while (true) {
        // Verifica se deve voltar para verde após travessia
        if (voltar_para_verde) {
            voltar_para_verde = false;
            atualizar_semaforo(VERDE);
            tempo_restante = TEMPO_VERDE / 1000;
            
            // Temporização do estado verde
            for (int i = 0; i < TEMPO_VERDE / 1000; i++) {
                if (pedido_travessia) goto travessia_pedestre;
                sleep_ms(1000);
            }
            continue;  // Volta para o início do loop
        }

        // 1. Estado Vermelho (10s)
        atualizar_semaforo(VERMELHO);
        tempo_restante = TEMPO_VERMELHO / 1000;
        for (int i = 0; i < TEMPO_VERMELHO / 1000; i++) {
            if (pedido_travessia) goto travessia_pedestre;
            sleep_ms(1000);
        }

        // 2. Estado Verde (10s)
        atualizar_semaforo(VERDE);
        tempo_restante = TEMPO_VERDE / 1000;
        for (int i = 0; i < TEMPO_VERDE / 1000; i++) {
            if (pedido_travessia) goto travessia_pedestre;
            sleep_ms(1000);
        }

        // 3. Estado Amarelo (3s)
        atualizar_semaforo(AMARELO);
        tempo_restante = TEMPO_AMARELO / 1000;
        for (int i = 0; i < TEMPO_AMARELO / 1000; i++) {
            if (pedido_travessia) goto travessia_pedestre;
            sleep_ms(1000);
        }

        continue;  // Reinicia o ciclo

        /*** Tratamento do Pedido de Travessia ***/
        travessia_pedestre:
            // Desativa o buzzer
            buzzer_active = false;
            if (beep_alarm_id != -1) {
                cancel_alarm(beep_alarm_id);
                beep_alarm_id = -1;
            }
            buzzer_pwm_off();

            // 1. Transição para Amarelo (3s)
            atualizar_semaforo(AMARELO);
            tempo_restante = TEMPO_AMARELO / 1000;
            for (int i = 0; i < TEMPO_AMARELO / 1000; i++) {
                sleep_ms(1000);
            }
            
            // 2. Estado Vermelho para Travessia (10s)
            atualizar_semaforo(VERMELHO);
            tempo_restante = TEMPO_TRAVESSIA / 1000;
            for (int i = 0; i < TEMPO_TRAVESSIA / 1000; i++) {
                sleep_ms(1000);
            }
            
            // Reseta flags e marca para voltar ao verde
            pedido_travessia = false;
            voltar_para_verde = true;
    }

    return 0;  // Nunca alcançado (loop infinito)
}