#ifndef AVIS_OPTIONAL_FUNCTIONS_H
#define AVIS_OPTIONAL_FUNCTIONS_H

#ifdef __cplusplus
extern "C" {
#endif 
//===========================================================================================================================
extern uint8_t 	button_block_sound_lora;    // Блокировка звука при нажатие кнопки
//===========================================================================================================================
extern void 			ReinitSensor(SNS_CFG_Type *pSnsCfg, uint8_t chan);
extern void 			ErrorClear(SNS_CFG_Type *pSnsCfg);
extern void             LimitClear(SNS_CFG_Type *pSnsCfg);
extern uint8_t 			LimitReceiptFlag(SNS_CFG_Type *pSnsCfg);
extern void 			LimitReceiptStart(void);
extern void 			BlockSoundLimitReceipt(void);
extern void 			Block_Sound(uint8_t state);
extern void             LimitProcessing(SNS_CFG_Type * pSnsCfg);                // Проверка порогов
//------------------------------------------------------------------------------
extern void             BumpTest(uint8_t chan, uint8_t state_chan);
//------------------------------------------------------------------------------
extern void             Indication(void);                                       // Индикация
extern void 			LedStateIndication(void);
//------------------------------------------------------------------------------
extern uint8_t 			SearchOxygenLimit1(void);
extern uint8_t          TestSleep(void);                                        // Проверка сработки таймеров и порогов
//------------------------------------------------------------------------------
extern uint8_t 			Test_all_chan_status_bit(uint32_t bit);
extern uint8_t 			Test_all_chan_status_err_bit(uint32_t bit);
//------------------------------------------------------------------------------
extern uint16_t 		Search_text(uint8_t *data1, uint8_t *data2);
//------------------------------------------------------------------------------
extern uint32_t 		RTC_ReadTime(void);
extern error_status 	RTC_WriteTime(uint32_t TimeCounter);
//===========================================================================================================================
#ifdef __cplusplus
}
#endif

#endif /* AVIS_OPTIONAL_FUNCTIONS_H */
