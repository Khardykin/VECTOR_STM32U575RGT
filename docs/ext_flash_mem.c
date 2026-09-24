#include "Avis_main.h"
#if (CONFIG_SAVE_PARAM_LOG_AT45)
static uint8_t power_at_256 = 0;
const uint16_t mas_reg_at45[14] = {TYPE_MEM_AT45, MAX_LOG_ON_SECTOR_AT45,
		PAGE_ADDR_FIRMWARE_AT45, SIZE_FIRMWARE_AT45, PAGE_ADDR_FIRMWARE_PARAM_AT45, PAGE_ADDR_PARAM_AT45,
		PAGE_ADDR_PARAM_DEFAULT_AT45, PAGE_ADDR_ADDR_LOG_AT45, PAGE_ADDR_PARAM_TIME_WARM_AT45,
		BUSY_FLAG_AT45, PAGEPROG_AT45, READSTAT_AT45, POWERDOWN_AT45, TIME_WAIT_STATUS_AT45};
//==============================================================================
const uint16_t mas_reg_w25[14] = {TYPE_MEM_W25, MAX_LOG_ON_SECTOR_W25,
		PAGE_ADDR_FIRMWARE_W25, SIZE_FIRMWARE_W25, PAGE_ADDR_FIRMWARE_PARAM_W25, PAGE_ADDR_PARAM_W25,
		PAGE_ADDR_PARAM_DEFAULT_W25, PAGE_ADDR_ADDR_LOG_W25, PAGE_ADDR_PARAM_TIME_WARM_W25,
		BUSY_FLAG_W25, PAGEPROG_W25, READSTAT_W25, POWERDOWN_W25, TIME_WAIT_STATUS_W25};
REG_MEM *reg_mem = (REG_MEM*)(&mas_reg_w25);
#define SPI_MEM 	(&hspi2)
//===========================================================================================================================
//address_reg - номер регистра(лога)
void save_log_memory(uint16_t address_reg, uint8_t *Data)
{
	uint16_t len = 0;
	uint16_t len_reg = SIZE_LOG;
	uint32_t byte_addr = address_reg*SIZE_LOG;
	if(reg_mem->Type_mem == TYPE_MEM_AT45){
		byte_addr = address_reg*SIZE_LOG;
	}
	else if(reg_mem->Type_mem == TYPE_MEM_W25){
		byte_addr = byte_addr + (address_reg/MAX_LOG_ON_SECTOR_W25)*(SIZE_PAGE - SIZE_LAST_PAGE_SECTOR_W25);
//		byte_addr = ((address_reg/MAX_LOG_ON_SECTOR_W25)*SIZE_SECTOR_W25) + ((address_reg%MAX_LOG_ON_SECTOR_W25)*SIZE_LOG);
	}
	//----------------------------------------------------------------------------
	//write: SIZE_LOG
	do{
		//----------------------------------------------------------------------------
		write_sector_erase_memory((byte_addr>>8), (byte_addr&0x00FF));
		//----------------------------------------------------------------------------
		if(((byte_addr&0x00FF) + len_reg) > SIZE_PAGE){
			len = SIZE_PAGE - (byte_addr&0x00FF);
		}
		else{
			len = len_reg;
		}
//		if(reg_mem->Type_mem == TYPE_MEM_W25){
//			if((byte_addr&0x0F00) == 0x0F00){
//				if(((byte_addr&0x00FF) + len_reg) > SIZE_LAST_PAGE_SECTOR_W25){
//					len = 0;
//					byte_addr &= ~0x00000FFF;
//					byte_addr += PAGE_TO_SECTOR_W25<<8;
//				}
//			}
//		}
		if(write_data_memory((byte_addr>>8), (byte_addr&0x00FF), Data, len)){
			SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
		}
		if(!wait_status_busy(reg_mem->time_wait_status)){
			break;
		}
		Data += len;
		len_reg -= len;
		byte_addr += len;
	}while(len_reg);
	//----------------------------------------------------------------------------
}

//===========================================================================================================================
//address_reg - номер регистра(лога) с которого читают
//len_reg - количество данных которые считываются
void read_log_memory(uint16_t address_reg, uint8_t *Data, uint16_t len_reg)
{
	uint16_t len = 0;
	uint32_t byte_addr = address_reg*SIZE_LOG;
	if(reg_mem->Type_mem == TYPE_MEM_AT45){
		byte_addr = address_reg*SIZE_LOG;
	}
	else if(reg_mem->Type_mem == TYPE_MEM_W25){
		byte_addr = byte_addr + (address_reg/MAX_LOG_ON_SECTOR_W25)*(SIZE_PAGE - SIZE_LAST_PAGE_SECTOR_W25);
	}

	while(!wait_status_busy(reg_mem->time_wait_status));
	//----------------------------------------------------------------------------
	do{
		if(((byte_addr&0x00FF) + len_reg) > SIZE_PAGE){
			len = SIZE_PAGE - (byte_addr&0x00FF);
		}
		else{
			len = len_reg;
		}
		if(reg_mem->Type_mem == TYPE_MEM_W25){
			if((byte_addr&0x0F00) == 0x0F00){
				if(((byte_addr&0x00FF) + len_reg) > SIZE_LAST_PAGE_SECTOR_W25){
					len = SIZE_LAST_PAGE_SECTOR_W25 - (byte_addr&0x00FF);
					if(len == 0){
						byte_addr &= ~0x00000FFF;
						byte_addr += PAGE_TO_SECTOR_W25<<8;
					}
				}
			}
		}

		if(read_data_memory((byte_addr>>8), (byte_addr&0x00FF), Data, len)){
			SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
		}
		Data += len;
		len_reg -= len;
		byte_addr += len;
	}while(len_reg);
  //----------------------------------------------------------------------------
}

//===========================================================================================================================
//Сохранение текущей конфигурации параметров
void Save_param_memory(uint16_t page_addr, uint8_t *Data, uint16_t len_reg)
{
  uint16_t len = 0;
  uint16_t byte_addr = 0;
  uint16_t err_busy = 3;
  //----------------------------------------------------------------------------
  //write:
  do{
	  //----------------------------------------------------------------------------
	  write_sector_erase_memory(page_addr, byte_addr);
	  //----------------------------------------------------------------------------
	  if(len_reg > SIZE_PAGE){
		  len = SIZE_PAGE;
	  }
	  else{
		  len = len_reg;
	  }
	  ERR_MARK:
	  if(write_data_memory(page_addr, byte_addr, Data, len)){
		  SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
	  }
	  if(!wait_status_busy(reg_mem->time_wait_status)){
		  err_busy--;
		  if(err_busy){
			  goto ERR_MARK;
		  }
		  break;
	  }
	  err_busy = 3;
	  Data += len;
	  len_reg -= len;
	  page_addr += 1;
  }while(len_reg);
  //----------------------------------------------------------------------------
}
//Чтение текущей конфигурации параметров
void Read_param_memory(uint16_t page_addr, uint8_t *Data, uint16_t len_reg)
{
  uint16_t len = 0;
  uint16_t byte_addr = 0;
  //----------------------------------------------------------------------------
  do{
    //--------------------------------------------------------------------------
    if(len_reg > SIZE_PAGE){
      len = SIZE_PAGE;//SIZE_PAGE;
    }
    else{
      len = len_reg;
    }
    //--------------------------------------------------------------------------
    if(read_data_memory(page_addr, byte_addr, Data, len)){
      SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
    }
    //--------------------------------------------------------------------------
    Data += len;
    len_reg -= len;
    byte_addr += len;
    if(byte_addr >= SIZE_PAGE){
    	byte_addr = 0;
    	page_addr += 1;
    }
  }while(len_reg);
  //----------------------------------------------------------------------------
}

//===========================================================================================================================
//Сохранение текущей конфигурации параметра
void Save_param_addr_log_memory(uint16_t page_addr, uint16_t byte_addr, uint8_t *Data, uint16_t len_reg)
{
	uint16_t len = 0;
	uint16_t err_busy = 3;
	//----------------------------------------------------------------------------
	//write:
	do{
		//----------------------------------------------------------------------------
		write_sector_erase_memory(page_addr, byte_addr);
		//----------------------------------------------------------------------------
		if(len_reg > SIZE_PAGE){
			len = SIZE_PAGE;
		}
		else{
			len = len_reg;
		}
		ERR_MARK:
		if(write_data_memory(page_addr, byte_addr, Data, len)){
			SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
		}
		if(!wait_status_busy(reg_mem->time_wait_status)){
			err_busy--;
			if(err_busy){
				goto ERR_MARK;
			}
			break;
		}
		err_busy = 3;
		Data += len;
		len_reg -= len;
		page_addr += 1;
	}while(len_reg);
	//----------------------------------------------------------------------------
}
//Чтение текущей конфигурации параметра
void Read_param_addr_log_memory(uint16_t page_addr, uint16_t byte_addr, uint8_t *Data, uint16_t len_reg)
{
	uint16_t len = 0;
	//----------------------------------------------------------------------------
	do{
		//--------------------------------------------------------------------------
		if(len_reg > SIZE_PAGE){
			len = SIZE_PAGE;
		}
		else{
			len = len_reg;
		}
		//--------------------------------------------------------------------------
		if(read_data_memory(page_addr, byte_addr, Data, len)){
			SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
		}
		//--------------------------------------------------------------------------
		Data += len;
		len_reg -= len;
		page_addr += 1;
	}while(len_reg);
	//----------------------------------------------------------------------------
}

//===========================================================================================================================
//Сохранение прошивки
void SaveFirmwareMemory(uint32_t address, uint8_t *Data, uint16_t len_reg)
{
  uint16_t len = 0;
  uint16_t page_addr = address/SIZE_PAGE + reg_mem->Page_addr_fw;
  uint16_t byte_addr = address%SIZE_PAGE;
  uint16_t err_busy = 3;
  //----------------------------------------------------------------------------
  //write:
  do{
	  //----------------------------------------------------------------------------
	  write_sector_erase_memory(page_addr, byte_addr);
	  //----------------------------------------------------------------------------
	  if((byte_addr + len_reg) > SIZE_PAGE){
		  len = SIZE_PAGE - byte_addr;
	  }
	  else{
		  len = len_reg;
	  }
	  ERR_MARK:
	  if(write_data_memory(page_addr, byte_addr, Data, len)){
		  SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
	  }
	  if(!wait_status_busy(reg_mem->time_wait_status)){
		  err_busy--;
		  if(err_busy){
			  goto ERR_MARK;
		  }
		  break;
	  }
	  err_busy = 3;
	  Data += len;
	  len_reg -= len;
	  page_addr += 1;
	  byte_addr = 0;
  }while(len_reg);
  //----------------------------------------------------------------------------
}
//Чтение прошивки
void ReadFirmwareMemory(uint32_t address, uint8_t *Data, uint16_t len_reg)
{
  uint16_t len = 0;
  uint16_t page_addr = address/SIZE_PAGE + reg_mem->Page_addr_fw;
  uint16_t byte_addr = address%SIZE_PAGE;
  //----------------------------------------------------------------------------
  do{
    if((byte_addr + len_reg) > SIZE_PAGE){
      len = SIZE_PAGE - byte_addr;
    }
    else{
      len = len_reg;
    }
    if(read_data_memory(page_addr, byte_addr, Data, len)){
      SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
    }

    Data += len;
    len_reg -= len;
    page_addr += 1;
    byte_addr = 0;
  }while(len_reg);
  //----------------------------------------------------------------------------
}

//===========================================================================================================================
uint16_t power_of_2_at64(void)
{
	if(power_at_256 == 1)
	{return 0;}
	if(reg_mem->Type_mem == TYPE_MEM_AT45){
		//----------------------------------------------------------------------------
		SET_OFF_CS_MEM;
		//----------------------------------------------------------------------------
		//cmd
		uint8_t Data[4] = {0x3D, 0x2A, 0x80, 0xA6};
		SPI_Transmit(SPI_MEM, Data, 4, 50);
		//----------------------------------------------------------------------------
		SET_ON_CS_MEM;
		//----------------------------------------------------------------------------
		power_at_256 = 1;
	}
	return 0;
}

//===========================================================================================================================
uint16_t ultra_deep_power_down_memory(void)
{
	//  __disable_irq();
	SET_OFF_CS_MEM;
	//----------------------------------------------------------------------------
	//cmd
	uint8_t Data[1] = {reg_mem->powerdown_reg};
	SPI_Transmit(SPI_MEM, Data, 1, 50);
	SPI_Receive(SPI_MEM, Data, 1, 50);

	Delay(1);
	//----------------------------------------------------------------------------
	SET_ON_CS_MEM;
	//  __enable_irq();
	return 0;
}
//===========================================================================================================================
uint16_t exit_ultra_deep_power_down_memory(void)
{
//	CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
	//----------------------------------------------------------------------------
	//  __disable_irq();
	SET_OFF_CS_MEM;
	//----------------------------------------------------------------------------
	//cmd
	uint8_t Data[1] = {RELEASE};
	if(reg_mem->Type_mem == TYPE_MEM_AT45){
		Delay(1);
	}
	else if(reg_mem->Type_mem == TYPE_MEM_W25){
		SPI_Transmit(SPI_MEM, Data, 1, 500);;
	}
	//----------------------------------------------------------------------------
	Delay(1);
	SPI_Receive(SPI_MEM, Data, 1, 50);
	SET_ON_CS_MEM;
	//  __enable_irq();
	return 0;
}

//===========================================================================================================================
//WRITEENABLE; WRITESTATEN; WRITEDISABLE;
uint8_t write_command_memory(uint8_t data)
{
	//----------------------------------------------------------------------------
	SET_OFF_CS_MEM;
	//----------------------------------------------------------------------------
	uint8_t Data_out[1];
	Data_out[0] = data;
	SPI_Transmit(SPI_MEM, Data_out, 1, 500);
	//----------------------------------------------------------------------------
	SET_ON_CS_MEM;
	__enable_irq();
	return 0;
}

//===========================================================================================================================
uint8_t wait_status_busy(uint16_t time)
{
	uint32_t tickstart = 0U;
	tickstart = GetTick();
	if(reg_mem->Type_mem == TYPE_MEM_AT45){
		while(!(read_status_busy())){
		  if ((GetTick() - tickstart) >  time){
			  SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
			  return 0;
		  }
		}
	}
	else if(reg_mem->Type_mem == TYPE_MEM_W25){
		while((read_status_busy())){
		  if ((GetTick() - tickstart) >  time){
			  SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
			  return 0;
		  }
		}
	}
	CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45);
	return 1;
}

//===========================================================================================================================
uint8_t read_status_busy(void)
{
  return (uint8_t)(read_status_memory() & reg_mem->busy_flag);
}

//===========================================================================================================================
uint16_t read_status_memory(void)
{
	//  uint32_t count_spi;
	//----------------------------------------------------------------------------
//	__disable_irq();
	SET_OFF_CS_MEM;
	//----------------------------------------------------------------------------
	uint8_t Data[4] = {reg_mem->read_stat_reg};
	SPI_Transmit(SPI_MEM, Data, 1, 50);
	SPI_Receive(SPI_MEM, Data, 2, 50);

	//----------------------------------------------------------------------------
	//delay_10us(2);
	SET_ON_CS_MEM;
//	__enable_irq();
	return (Data[1] + (Data[0] << 8));
}

//===========================================================================================================================
uint8_t write_sector_erase_memory(uint16_t page_addr, uint16_t byte_addr)
{
	if(reg_mem->Type_mem == TYPE_MEM_W25){
		if(((page_addr&0x000F) == 0) && (byte_addr == 0)){
			if(reg_mem->Type_mem == TYPE_MEM_W25){
				while(!wait_status_busy(reg_mem->time_wait_status));
			}
			//----------------------------------------------------------------------------
			write_command_memory(WRITEENABLE);
			//----------------------------------------------------------------------------
			SET_OFF_CS_MEM;
			//----------------------------------------------------------------------------
			uint8_t Data_out[4];
			Data_out[0] = SECTORERASE;
			Data_out[1] = (page_addr>>8);
			Data_out[2] = (page_addr);
			Data_out[3] = (byte_addr);
			SPI_Transmit(SPI_MEM, Data_out, 4, 500);
			//----------------------------------------------------------------------------
			SET_ON_CS_MEM;
			//----------------------------------------------------------------------------
			if(reg_mem->Type_mem == TYPE_MEM_W25){
				while(!wait_status_busy(reg_mem->time_wait_status));
			}
		}
	}
	return 0;
}

//===========================================================================================================================
uint16_t read_dev_id_memory(void)
{
	uint8_t data[5] = {0};
	uint8_t Data_out[1];
//	reg_mem = (REG_MEM*)(&mas_reg_at45);
//	memcpy(&reg_mem, mas_reg_at45, sizeof(REG_MEM));
	//----------------------------------------------------------------------------
//	__disable_irq();
	SET_OFF_CS_MEM;
	//----------------------------------------------------------------------------
	Data_out[0] = JEDECID;
	SPI_Transmit(SPI_MEM, Data_out, 1, 50);
	SPI_Receive(SPI_MEM, data, 5, 50);
	//----------------------------------------------------------------------------
	//delay_10us(2);
	SET_ON_CS_MEM;
	if(data[0] == 0x1F){
		reg_mem = (REG_MEM*)(&mas_reg_at45);
	}
	else{
		reg_mem = (REG_MEM*)(&mas_reg_w25);
	}
//	__enable_irq();
	return 0;
}

//===========================================================================================================================
uint8_t write_data_memory(uint16_t page_addr, uint16_t byte_addr, uint8_t *Data, uint16_t len)
{
	if(reg_mem->Type_mem == TYPE_MEM_W25){
		write_command_memory(WRITEENABLE);
	}
//	__disable_irq();
	SET_OFF_CS_MEM;
	//----------------------------------------------------------------------------
	uint8_t Data_out[4];
	Data_out[0] = reg_mem->page_prog_reg;
	Data_out[1] = (page_addr>>8);
	Data_out[2] = (page_addr);
	Data_out[3] = (byte_addr);
	SPI_Transmit(SPI_MEM, Data_out, 4, 5);
	SPI_Transmit(SPI_MEM, Data, len, 5);
	//  SPI_Receive(SPI_MEM, Data, 1, 50);
	//----------------------------------------------------------------------------
	SET_ON_CS_MEM;
//	__enable_irq();
	return 0;
}

//===========================================================================================================================
//uint32_t count_spi1 = 0;
uint8_t read_data_memory(uint16_t page_addr, uint16_t byte_addr, uint8_t *Data, uint16_t len)
{
	//----------------------------------------------------------------------------
//	__disable_irq();
	SET_OFF_CS_MEM;
	//----------------------------------------------------------------------------
	uint8_t Data_out[5];
	Data_out[0] = FASTREAD;
	Data_out[1] = (page_addr>>8);
	Data_out[2] = (page_addr);
	Data_out[3] = (byte_addr);
	Data_out[4] = 0x00;
	//  SPI_TransmitReceive(SPI_MEM, Data_out, Data, );
	SPI_Transmit(SPI_MEM, Data_out, 5, 5);
	SPI_Receive(SPI_MEM, Data, len, 5);
	//----------------------------------------------------------------------------
	SET_ON_CS_MEM;
//	__enable_irq();
	return 0;
}

#endif
