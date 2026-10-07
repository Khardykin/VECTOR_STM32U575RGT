/*
 * Vector_main.h
 *
 *  Created on: 1 окт. 2026 г.
 *      Author: Dmitriy
 */

#ifndef VECTORLIB_INC_VECTOR_MAIN_H_
#define VECTORLIB_INC_VECTOR_MAIN_H_

//===========================================================================================================================
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "rtc.h"
//===========================================================================================================================
#include "config_device.h"
#include "vector_config.h"

#include <stdint.h>

//#include "shared_macros.h"
//#include "shared_types.h"

#if CONFIG_LORA
	#include "Lora_S7678S.h"
#endif

#include "buffer.h"

#include "audio_demo.h"
#include "audio_player.h"
#include "vector_tasks.h"

#endif /* VECTORLIB_INC_VECTOR_MAIN_H_ */
