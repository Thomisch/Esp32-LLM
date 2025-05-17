#include <stdio.h>
#include <inttypes.h>
#include "esp_spiffs.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include <time.h>
#include "llm.h"
#include <u8g2.h>
#include "u8g2_esp32_hal.h"
#include <driver/i2c.h>
#include <string.h>
#include "llama.h"

static const char *TAG = "MAIN";
u8g2_t u8g2;

#define PIN_SDA 8
#define PIN_SCL 9
#define OLED_I2C_ADDRESS 0x3C // Changed from 0x78 to the typical 7-bit address for SSD1306

#define LLM_OUTPUT_BUFFER_SIZE (256 * 8) // Ajustez si nécessaire (steps * taille moyenne d'un token en char)
char llm_full_output_buffer[LLM_OUTPUT_BUFFER_SIZE];
int llm_current_output_length = 0;

/**
 * @brief Configure SSD1306 display
 * Uses I2C connection
 */
void init_display(void)
{
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
    u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
    u8g2_esp32_hal_init(u8g2_esp32_hal);
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2, U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&u8g2.u8x8, OLED_I2C_ADDRESS);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_SendBuffer(&u8g2);
    ESP_LOGI(TAG, "Display initialized");
}

void init_storage(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false};
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL) ESP_LOGE(TAG, "Failed to mount or format filesystem");
        else if (ret == ESP_ERR_NOT_FOUND) ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        else ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    else ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
}

void write_display(char *text)
{
    u8g2_ClearBuffer(&u8g2);
    u8g2_DrawStr(&u8g2, 0, u8g2_GetDisplayHeight(&u8g2) / 2, text);
    u8g2_SendBuffer(&u8g2);
}

void draw_llama(void)
{
    u8g2_ClearBuffer(&u8g2);
    u8g2_DrawXBM(&u8g2, 0, 0, u8g2_GetDisplayWidth(&u8g2), u8g2_GetDisplayHeight(&u8g2), (const uint8_t *)llama_bmp);
    u8g2_SendBuffer(&u8g2);
}

void token_generated_cb(const char *token_str)
{
    if (token_str != NULL)
    {
        int token_len = strlen(token_str);
        if (llm_current_output_length + token_len < LLM_OUTPUT_BUFFER_SIZE - 1) // -1 pour le caractère nul final
        {
            strcat(llm_full_output_buffer, token_str); // Concatène le nouveau token
            llm_current_output_length += token_len;
        }
        else
        {
            // Le buffer est plein, on pourrait logger une erreur ou arrêter d'ajouter.
            // Pour l'instant, on ignore les tokens supplémentaires pour éviter un débordement.
            // Un ESP_LOGW serait approprié ici si cela arrive fréquemment.
        }
    }
}

void generate_complete_cb(float tk_s)
{
    // char buffer[50]; // Pour l'affichage OLED si utilisé
    // sprintf(buffer, "%.2f tok/s", tk_s);
    // write_display(buffer);

    // --- MODIFIÉ : Imprimer le buffer complet ---
    printf("\nRéponse Complète du LLM:\n-----------------------------------------------------\n");
    printf("%s", llm_full_output_buffer); // Imprime le contenu du buffer
    printf("\n-----------------------------------------------------\n");
    printf("Génération terminée. Vitesse : %.2f tokens/seconde\n", tk_s);
    fflush(stdout);
    // --- FIN MODIFIÉ ---
}

void app_main(void)
{
    //init_display();
    //write_display("Loading Model");
    init_storage();

    char *checkpoint_path = "/data/stories260K.bin";
    char *tokenizer_path = "/data/tok512.bin";
    float temperature = 1.0f;
    float topp = 0.9f;
    int steps = 256;
    unsigned long long rng_seed = 0;

    char *prompt_to_run = "Tell me a short story about a brave knight and a dragon.";
    ESP_LOGI(TAG, "Utilisation de l'invite codée en dur : \"%s\"", prompt_to_run);

    if (rng_seed <= 0)
        rng_seed = (unsigned int)time(NULL);

    Transformer transformer;
    ESP_LOGI(TAG, "LLM Path is %s", checkpoint_path);
    build_transformer(&transformer, checkpoint_path);
    if (steps == 0 || steps > transformer.config.seq_len)
        steps = transformer.config.seq_len;

    Tokenizer tokenizer;
    build_tokenizer(&tokenizer, tokenizer_path, transformer.config.vocab_size);

    Sampler sampler;
    build_sampler(&sampler, transformer.config.vocab_size, temperature, topp, rng_seed);

    // --- MODIFIÉ : Initialisation du buffer et suppression de l'ancien printf ---
    ESP_LOGI(TAG, "Démarrage de la génération LLM avec l'invite fournie...");
    
    // Initialiser le buffer de sortie avant la génération
    llm_current_output_length = 0;
    llm_full_output_buffer[0] = '\0'; // S'assurer que le buffer est une chaîne vide au début

    // Le printf suivant n'est plus nécessaire ici, car la sortie sera groupée à la fin.
    // printf("\nRéponse du LLM:\n-----------------------------------------------------\n");
    // fflush(stdout);
    // --- FIN MODIFIÉ ---

    // MODIFICATION : La fonction generate dans llm.c appellera token_generated_cb pour chaque token.
    // La callback generate_complete_cb s'occupera d'imprimer le buffer final.
    generate(&transformer, &tokenizer, &sampler, prompt_to_run, steps, token_generated_cb, generate_complete_cb);


    // Les printf suivants ne sont plus nécessaires ici car generate_complete_cb s'en charge.
    // printf("-----------------------------------------------------\n");
    // printf("Cycle d'interaction avec le LLM terminé.\n");
    // fflush(stdout);
}