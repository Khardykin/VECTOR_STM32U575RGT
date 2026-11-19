/**
  ******************************************************************************
  * @file    Command_vector.h
  * @brief   Обмен по COM (USART3) в стиле Avis Command.c.
  *
  *          Протокол (CRC16 Modbus lo,hi в конце каждого кадра):
  *            0x03 CmdFunc3  [sl][03][addr16][cnt16]        - holding (Config_common)
  *            0x04 CmdFunc4  [sl][04][addr16][cnt16]        - input   (sensors_status)
  *            0x10 CmdFunc16 [sl][10][addr16][cnt16][nb][data]
  *            0x0F CmdFunc15 [sl][0F][addr32][size16][data] - блочная запись flash
  *          Ответ-ошибка: [sl][cmd|0x80][code][CRC].
  *
  *          Управляющие регистры 0x0F00+ (COMMIT, сведения об образе, стирание
  *          областей, окно данных) - карта в Command_vector.c.
  *
  *          Конец кадра - по длине из шапки, не по паузе в линии.
  *          КОНТЕКСТ: receiver_task (command_message), не ISR.
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
#define CMD_RESYNC_MS       300u
#define CMD_SESSION_IDLE_MS 5000u

/* Резерв под будущий образ прошивки: за LOG_PTR, 1 МБ. */
#define CMD_FW_BASE         0x00603000u
#define CMD_FW_SIZE         0x00100000u

#ifndef CMD_VECTOR_COM
#define CMD_VECTOR_COM      1     /* 0 - собрать без привязки к USART3/плееру */
#endif

/* Коды команд. Имя CmdFuncN = десятичный код, как в Avis. */
typedef enum
{
  CMD_OP_READ_HOLD   = 0x03,   /* чтение holding-регистров        */
  CMD_OP_READ_IN     = 0x04,   /* чтение input-регистров          */
  CMD_OP_WRITE       = 0x0F,   /* блочная запись во внешнюю flash */
  CMD_OP_WRITE_MULTI = 0x10    /* запись группы holding           */
} cmd_op_t;

/* Коды ошибок. У Modbus и у flash смысл одного числа разный,
   поэтому часть значений продублирована под двумя именами. */
typedef enum
{
  CMD_ERR_NONE      = 0,
  CMD_ERR_LENGTH    = 1,   /* неверная длина кадра             */
  CMD_ERR_ADDR      = 2,   /* Modbus: illegal data address     */
  CMD_ERR_RANGE     = 2,   /* flash: адрес вне микросхемы      */
  CMD_ERR_IO        = 3,   /* flash: ошибка обращения          */
  CMD_ERR_VALUE     = 3,   /* Modbus: illegal data value       */
  CMD_ERR_VERIFY    = 4,   /* сверка после записи не совпала   */
  CMD_ERR_PROTECTED = 5,   /* область защищена                 */
  CMD_ERR_UNKNOWN   = 6    /* неизвестная команда/регистр      */
} cmd_err_t;

typedef void (*cmd_flash_pause_t)(uint8_t pause);
typedef void (*cmd_commit_t)(void);
typedef void (*cmd_tx_t)(const uint8_t *data, uint16_t len);

void Command_vector_SetPauseHook(cmd_flash_pause_t hook);
void Command_vector_SetCommitHook(cmd_commit_t hook);
void Command_vector_SetTx(cmd_tx_t hook);

/* Диспетчер: проверяет CRC, зовёт CmdFuncN, дописывает CRC ответа. */
uint16_t parsing_command(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap);

uint16_t CmdFunc3 (const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap);
uint16_t CmdFunc4 (const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap);
uint16_t CmdFunc15(const uint8_t *mas, uint16_t len, uint8_t *out);
uint16_t CmdFunc16(const uint8_t *mas, uint16_t len, uint8_t *out);

uint16_t CreateCRC(uint8_t *data, uint16_t len);
uint16_t CreateCRC_out(const uint8_t *data, uint16_t len);

void    Command_vector_RxFeed(const uint8_t *data, uint16_t len, uint32_t now_ms);
void    Command_vector_Poll(uint32_t now_ms);
void    Command_vector_EraseService(void);   /* один сектор за проход */
uint8_t Command_vector_SessionActive(void);  /* 1 = идёт обмен */
void    Command_vector_LogMute(uint8_t mute);

#ifdef __cplusplus
}
#endif
#endif /* COMMAND_VECTOR_H */
