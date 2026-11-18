/**
  ******************************************************************************
  * @file    Command_vector.h
  * @brief   Загрузка данных во внешнюю SPI flash по COM (USART3).
  *
  *          Кадр:  [slave][cmd][addr32 BE][size16 BE][payload][CRC16 lo,hi]
  *          Ответ: тот же формат; ошибка: [slave][cmd|0x80][code][CRC] (5 Б).
  *
  *          0x0A COMMIT - плеер перечитывает образ (audio_reload_image)
  *          0x0B INFO   - 23 Б: flash32 sector16 page16 magic32 version16
  *                            count16 image_size32 rate16 ok8
  *          0x0D ERASE  - payload 5 Б: len32 BE + key8 (0xA5 = вне SOUNDS)
  *          0x0E READ   - payload 2 Б: len16 BE (ответ <= 256 Б из-за таймаута TX)
  *          0x0F WRITE  - payload <= 1024 Б, после записи сверка чтением
  *
  *          Конец кадра определяется ДЛИНОЙ из шапки, не паузой в линии:
  *          сервис вызывается раз в 10 мс, тайминг байтов недоступен.
  *          КОНТЕКСТ: receiver_task (не ISR).
  ******************************************************************************
  */
#ifndef COMMAND_VECTOR_H
#define COMMAND_VECTOR_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CMD_MAX_PAYLOAD     1024u
#define CMD_FRAME_HDR       8u
#define CMD_MAX_FRAME       (CMD_FRAME_HDR + CMD_MAX_PAYLOAD + 2u)
#define CMD_OP_COMMIT       0x0Au
#define CMD_OP_INFO         0x0Bu
#define CMD_OP_ERASE        0x0Du
#define CMD_OP_READ         0x0Eu
#define CMD_OP_WRITE        0x0Fu
#define CMD_ERR_LENGTH      1u
#define CMD_ERR_RANGE       2u
#define CMD_ERR_IO          3u
#define CMD_ERR_VERIFY      4u
#define CMD_ERR_PROTECTED   5u
#define CMD_ERR_UNKNOWN     6u
#define CMD_RESYNC_MS       300u
#ifndef CMD_VECTOR_COM
#define CMD_VECTOR_COM      1     /* 0 - собрать без привязки к USART3/плееру */
#endif
typedef void (*cmd_flash_pause_t)(uint8_t pause);
typedef void (*cmd_commit_t)(void);
typedef void (*cmd_tx_t)(const uint8_t *data, uint16_t len);
void Command_vector_SetPauseHook(cmd_flash_pause_t h);
void Command_vector_SetCommitHook(cmd_commit_t h);
void Command_vector_SetTx(cmd_tx_t h);
uint16_t Command_vector_Frame(const uint8_t *req,uint16_t req_len,uint8_t *rsp,uint16_t rsp_cap);
void     Command_vector_RxFeed(const uint8_t *data,uint16_t len,uint32_t now_ms);
void     Command_vector_Poll(uint32_t now_ms);
uint16_t Command_vector_CRC16(const uint8_t *data,uint16_t len);
#ifdef __cplusplus
}
#endif
#endif
