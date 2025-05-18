/*RESIDÊNCIA PROFISSIONAL EM SISTEMAS EMBARCADOS-IFMA
 * Protótipo Termômetro OLED com DMA
 * Autor: [Diego da Silva Campos do Nascimento]
 * Data: [17/05/2025]
 * Matrícula:20251RSE.MTC0017
 *
 * Descrição: Sistema de Aquisição de Temperatura com DMA
 *
 * Repositório no GitHub - https://github.com/DiegoCamposRJ/Residencia_EMBARCATECH_2025-IFMA_MTC/tree/main/Unidade_1/AT_CAP_05
 * 
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "oled/ssd1306.h"

/*
 * DEFINIÇÕES DE HARDWARE
 */

// Configurações do display OLED
#define I2C_PORT i2c1           // Porta I2C utilizada
#define I2C_SDA_PIN 14          // Pino GPIO para SDA
#define I2C_SCL_PIN 15          // Pino GPIO para SCL
#define OLED_ADDRESS 0x3C       // Endereço I2C do display
#define OLED_WIDTH 128          // Largura do display em pixels
#define OLED_HEIGHT 64          // Altura do display em pixels

// Buffer para armazenar as leituras do ADC
#define ADC_BUFFER_SIZE 16      // Tamanho do buffer para média móvel
uint16_t adc_buffer[ADC_BUFFER_SIZE]; // Buffer para armazenar leituras ADC

/*
 * VARIÁVEIS GLOBAIS
 */
volatile float temperature_c = 0.0f;    // Armazena a temperatura atual em Celsius
volatile uint32_t sample_count = 0;     // Contador de amostras coletadas
uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8]; // Buffer para o display OLED
int dma_chan;                          // Canal DMA utilizado
dma_channel_config dma_config;         // Configuração do canal DMA

/*
 * FUNÇÃO: init_adc_temp_sensor()
 * DESCRIÇÃO: Configura o ADC e o sensor de temperatura interno do RP2040
 * - Inicializa o hardware ADC
 * - Habilita o sensor de temperatura interno (conectado ao ADC4)
 * - Configura o FIFO do ADC para trabalhar com DMA
 */
void init_adc_temp_sensor() {
    adc_init(); // Inicializa o módulo ADC
    adc_set_temp_sensor_enabled(true); // Habilita o sensor de temperatura interno
    adc_select_input(4); // Seleciona o canal ADC4 (sensor de temperatura)
    
    // Configura o FIFO do ADC para:
    adc_fifo_setup(
        true,    // Escrever cada resultado no FIFO
        true,    // Habilitar DMA para transferências automáticas
        1,       // Gerar solicitação DMA após 1 amostra no FIFO
        false,   // Não descartar leituras com erro
        false    // Não reduzir amostras (usar todas as 12 bits)
    );
    
    adc_set_clkdiv(0); // Configura o ADC para máxima velocidade (clock divisor = 0)
}

/*
 * FUNÇÃO: init_dma_adc_transfer()
 * DESCRIÇÃO: Configura o DMA para transferir automaticamente as leituras do ADC
 * - Reserva um canal DMA disponível
 * - Configura o canal para transferir dados de 16 bits
 * - Define o FIFO do ADC como origem e nosso buffer como destino
 * - Configura o DMA para ser acionado pelo ADC (DREQ_ADC)
 */
void init_dma_adc_transfer() {
    // Reserva um canal DMA não utilizado
    dma_chan = dma_claim_unused_channel(true);
    
    // Obtém configuração padrão para o canal DMA
    dma_config = dma_channel_get_default_config(dma_chan);
    
    // Configurações específicas:
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_16); // Dados de 16 bits
    channel_config_set_read_increment(&dma_config, false); // Origem não incrementa (FIFO)
    channel_config_set_write_increment(&dma_config, true); // Destino incrementa (buffer)
    channel_config_set_dreq(&dma_config, DREQ_ADC); // Usar sinal do ADC para controle
    
    // Configura a transferência DMA:
    dma_channel_configure(
        dma_chan,               // Canal DMA
        &dma_config,            // Configuração
        adc_buffer,             // Buffer de destino (RAM)
        &adc_hw->fifo,          // Fonte (FIFO do ADC)
        ADC_BUFFER_SIZE,        // Número de transferências
        false                   // Não iniciar ainda
    );
    
    // Inicia as conversões contínuas do ADC
    adc_run(true);
}

/*
 * FUNÇÃO: calculate_temperature()
 * PARÂMETROS:
 *   - adc_value: valor digital lido do ADC (12 bits)
 * RETORNO: temperatura em graus Celsius
 * DESCRIÇÃO: Converte o valor do ADC para temperatura usando a fórmula do datasheet
 */
float calculate_temperature(uint16_t adc_value) {
    // Converte valor ADC para tensão (referência de 3.3V, 12 bits)
    float voltage = adc_value * 3.3f / (1 << 12);
    
    // Fórmula do datasheet do RP2040 para sensor de temperatura:
    // Temperatura = 27 - (V_sensor - 0.706)/0.001721
    return 27.0f - (voltage - 0.706f) / 0.001721f;
}

/*
 * FUNÇÃO: center_text()
 * PARÂMETROS:
 *   - buffer: buffer do display
 *   - page: página (linha) do display (0-7 para display de 64px)
 *   - text: texto a ser centralizado
 * DESCRIÇÃO: Centraliza um texto horizontalmente em uma linha do display
 */
void center_text(uint8_t *buffer, int page, const char *text) {
    int text_length = strlen(text);
    int x_pos = (OLED_WIDTH - (text_length * 8)) / 2; // Calcula posição central
    if (x_pos < 0) x_pos = 0; // Garante que não seja negativo
    ssd1306_draw_string(buffer, x_pos, page * 8, text); // Desenha o texto
}

/*
 * FUNÇÃO: init_oled()
 * DESCRIÇÃO: Inicializa o display OLED via I2C
 * - Configura os pinos I2C
 * - Inicializa o display
 * - Limpa o buffer e o display
 */
void init_oled() {
    // Configura o hardware I2C
    i2c_init(I2C_PORT, 400 * 1000); // Inicializa I2C a 400kHz
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C); // Configura pino SDA
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C); // Configura pino SCL
    gpio_pull_up(I2C_SDA_PIN); // Habilita pull-up no SDA
    gpio_pull_up(I2C_SCL_PIN); // Habilita pull-up no SCL
    
    // Inicializa o display OLED
    ssd1306_init();
    
    // Limpa o buffer do display
    memset(oled_buffer, 0, sizeof(oled_buffer));
    
    // Define área de renderização (toda a tela)
    struct render_area area = {
        .start_column = 0,
        .end_column = OLED_WIDTH - 1,
        .start_page = 0,
        .end_page = OLED_HEIGHT/8 - 1
    };
    
    // Calcula tamanho do buffer e renderiza
    calculate_render_area_buffer_length(&area);
    render_on_display(oled_buffer, &area);
}

/*
 * FUNÇÃO: update_display()
 * PARÂMETROS:
 *   - temp: temperatura a ser exibida
 *   - count: contador de amostras
 * DESCRIÇÃO: Atualiza o display OLED com a temperatura e contador
 * - Formata a temperatura como "XX,X°C"
 * - Centraliza os textos
 * - Exibe o contador de amostras
 */
void update_display(float temp, uint32_t count) {
    char temp_str[16], samples_str[24];
    
    // Limpa o buffer do display
    memset(oled_buffer, 0, sizeof(oled_buffer));

    // Linha 1: Título "Temperatura" centralizado
    center_text(oled_buffer, 0, "Temperatura");

    // Linha 2: Formata a temperatura como "XX,X°C"
    int temp_int = (int)temp; // Parte inteira
    // Parte decimal (arredondada para 1 dígito)
    int temp_decimal = (int)((fabs(temp) - fabs((float)temp_int)) * 10 + 0.5f);
    
    // Formata a string com vírgula (0x2C) e símbolo de grau (0xF8)
    snprintf(temp_str, sizeof(temp_str), "%d%c%d%cC", 
            temp_int, 0x2C, temp_decimal, 0xF8);

    // Centraliza o texto da temperatura (em tamanho 2x)
    int text_width = strlen("00,0°C") * 16; // Largura fixa para centralização
    int x_pos = (OLED_WIDTH - text_width) / 2;
    
    // Desenha cada caractere com posicionamento personalizado
    for (int i = 0; i < strlen(temp_str); i++) {
        int y_pos = 20; // Posição base
        
        // Ajusta posição da vírgula (6px mais baixo)
        if (temp_str[i] == 0x2C) {
            y_pos = 26;
        }
        // Ajusta posição do símbolo de grau e 'C'
        else if (temp_str[i] == 0xF8 || temp_str[i] == 'C') {
            y_pos = 16;
        }
        
        // Desenha caractere em tamanho 2x
        ssd1306_draw_char_scale2(oled_buffer, x_pos, y_pos, temp_str[i]);
        x_pos += 16; // Avança para próxima posição
    }

    // Linha inferior: Contador de amostras
    snprintf(samples_str, sizeof(samples_str), "Amostra: %lu", count);
    center_text(oled_buffer, (OLED_HEIGHT/8)-1, samples_str);

    // Renderiza tudo no display
    struct render_area area = {
        .start_column = 0,
        .end_column = OLED_WIDTH - 1,
        .start_page = 0,
        .end_page = OLED_HEIGHT/8 - 1
    };
    calculate_render_area_buffer_length(&area);
    render_on_display(oled_buffer, &area);
}

/*
 * FUNÇÃO PRINCIPAL
 */
int main() {
    // Inicializações básicas
    stdio_init_all(); // Inicializa stdio (para printf)
    init_adc_temp_sensor(); // Configura ADC e sensor de temperatura
    init_dma_adc_transfer(); // Configura DMA para transferir leituras
    init_oled(); // Inicializa o display OLED
    
    // Mostra tela inicial (0°C e amostra 0)
    update_display(0.0f, 0);
    
    // Loop principal
    while (1) {
        sleep_ms(500); // Atualiza a cada 500ms
        
        // Reinicia a transferência DMA para novas leituras
        dma_channel_configure(
            dma_chan,
            &dma_config,
            adc_buffer,
            &adc_hw->fifo,
            ADC_BUFFER_SIZE,
            true // Inicia imediatamente
        );
        
        // Espera a transferência DMA completar
        dma_channel_wait_for_finish_blocking(dma_chan);
        
        // Calcula média das amostras no buffer
        uint32_t sum = 0;
        for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
            sum += adc_buffer[i];
        }
        
        // Calcula nova temperatura (média das amostras)
        temperature_c = calculate_temperature(sum / ADC_BUFFER_SIZE);
        sample_count++; // Incrementa contador
        
        // Log no terminal (para debug)
        printf("Leitura %lu: %d (%.1f°C)\n", sample_count, sum / ADC_BUFFER_SIZE, temperature_c);
        
        // Atualiza o display com os novos valores
        update_display(temperature_c, sample_count);
    }
    
    return 0;
}