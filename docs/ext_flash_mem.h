#ifndef EXT_FLASH_MEM_H_
#define EXT_FLASH_MEM_H_
//==============================================================================
// 168 байт максимальный размер лога на 32767 записей = 21504 страниц,
// что бы не залесть на другие страницы,
// если больше размер лога, то нужно подвинуть остальные записи.
// Одна страница 256 байт.
#define SIZE_LOG 	                	sizeof(EVENT_LOG)
#define SIZE_PAGE 	                	(256)
//#define PAGE_ADDR_LOG              		(0)
#define PAGE_ADDR_END                   32767//(7FFF)
//==============================================================================
// Для AT45
#define TYPE_MEM_AT45	0
#define SIZE_LOG_ALL_PAGE_AT45	            ((32767*SIZE_LOG)/SIZE_PAGE)
#define MAX_LOG_ON_SECTOR_AT45				(1)
//==============================================================================
#define PAGE_ADDR_FIRMWARE_AT45             30000//to 30500
#define SIZE_FIRMWARE_AT45                  2000
#define PAGE_ADDR_FIRMWARE_PARAM_AT45       (PAGE_ADDR_FIRMWARE_AT45 + SIZE_FIRMWARE_AT45 + 1)

#define SIZE_ADDR_PARAM_AT45 				(sizeof(SNS_CFG)/256 + 1)
#define PAGE_ADDR_PARAM_AT45                (PAGE_ADDR_FIRMWARE_PARAM_AT45 + 1)
#define PAGE_ADDR_PARAM_DEFAULT_AT45        (PAGE_ADDR_PARAM_AT45 + SIZE_ADDR_PARAM_AT45)
#define PAGE_ADDR_ADDR_LOG_AT45             (PAGE_ADDR_PARAM_DEFAULT_AT45 + SIZE_ADDR_PARAM_AT45)
#define PAGE_ADDR_PARAM_TIME_WARM_AT45		(PAGE_ADDR_ADDR_LOG_AT45 + 3)
//==============================================================================
#define TIME_WAIT_STATUS_AT45    			(100)
#define BUSY_FLAG_AT45    					(0x8080)
//==============================================================================
// Для W25
#define TYPE_MEM_W25	1
#define SIZE_SECTOR_W25                     (4096)
#define MAX_LOG_ON_SECTOR_W25               ((SIZE_SECTOR_W25/SIZE_LOG))
#define SIZE_LAST_PAGE_SECTOR_W25 			(SIZE_PAGE - (SIZE_SECTOR_W25 - (MAX_LOG_ON_SECTOR_W25*SIZE_LOG)))
//==============================================================================
#define PAGE_ADDR_FIRMWARE_W25              (0x7400)// 116*65536
#define SIZE_FIRMWARE_W25                   (2000)                                          //7D0
#define PAGE_ADDR_FIRMWARE_PARAM_W25        (PAGE_ADDR_FIRMWARE_W25 + SIZE_FIRMWARE_W25)            //7BD0

#define PAGE_TO_SECTOR_W25                  (16)                                            //0x10
#define PAGE_ADDR_PARAM_W25                 (PAGE_ADDR_FIRMWARE_PARAM_W25 + PAGE_TO_SECTOR_W25)     //7BE0
#define PAGE_ADDR_PARAM_DEFAULT_W25         (PAGE_ADDR_PARAM_W25 + PAGE_TO_SECTOR_W25)              //7BF0
#define PAGE_ADDR_ADDR_LOG_W25              (PAGE_ADDR_PARAM_DEFAULT_W25 + PAGE_TO_SECTOR_W25)      //7C00
#define PAGE_ADDR_PARAM_TIME_WARM_W25		(PAGE_ADDR_ADDR_LOG_W25 + PAGE_TO_SECTOR_W25)		//7C10
//==============================================================================
#define TIME_WAIT_STATUS_W25     			(100)
#define BUSY_FLAG_W25    					(0x01)
//==============================================================================
#define PAGEPROG_AT45    	0x58
#define READSTAT_AT45     	0xD7
#define POWERDOWN_AT45     	0x79
//-----------------------------------
#define PAGEPROG_W25     	0x02
#define READSTAT_W25     	0x05
#define POWERDOWN_W25     	0xB9
//-----------------------------------

//-----------------------------------
#define RELEASE       		0xAB
#define JEDECID       		0x9F
#define FASTREAD      		0x0B

#define SECTORERASE   		0x20
#define WRITEENABLE   		0x06
#define WRITESTATEN   		0x50
#define WRITEDISABLE  		0x04
//==============================================================================
typedef struct//
{
  uint16_t		Type_mem;
  uint16_t      Max_log_on_sector;
  uint16_t      Page_addr_fw;
  uint16_t      Size_fw;
  uint16_t      Page_addr_fw_param;
  uint16_t      Page_addr_param;
  uint16_t      Page_addr_param_def;
  uint16_t      Page_addr_log;
  uint16_t      Page_addr_param_warm;
  uint16_t      busy_flag;
  uint16_t      page_prog_reg;
  uint16_t      read_stat_reg;
  uint16_t      powerdown_reg;
  uint16_t		time_wait_status;
}REG_MEM;
extern REG_MEM *reg_mem;
//==============================================================================
extern const uint16_t mas_reg_at45[14];
//==============================================================================
extern const uint16_t mas_reg_w25[14];
//==============================================================================
#define	SET_ON_CS_MEM 		SET_ON(CS_FLASH)
#define	SET_OFF_CS_MEM	 	SET_OFF(CS_FLASH)
//==============================================================================
extern void             save_log_memory(uint16_t address_reg, uint8_t *Data);                                          //Запись лога в память
extern void             read_log_memory(uint16_t address_reg, uint8_t *Data, uint16_t len_reg);                        //Чтение лога с памяти

extern uint8_t          write_data_memory(uint16_t page_addr, uint16_t byte_addr, uint8_t *Data, uint16_t len);        //Запись данных на страницу
extern uint8_t          read_data_memory(uint16_t page_addr, uint16_t byte_addr, uint8_t *Data, uint16_t len);         //Чтение страницы

extern void             Save_param_addr_log_memory(uint16_t page_addr, uint16_t byte_addr, uint8_t *Data, uint16_t len_reg);
extern void             Read_param_addr_log_memory(uint16_t page_addr, uint16_t byte_addr, uint8_t *Data, uint16_t len_reg);

extern uint16_t         read_status_memory(void);                                      	//Чтение статусного бита
extern uint8_t          read_status_busy(void);                                 		//Чтение статуса и проверка флага на busy
extern uint8_t          wait_status_busy(uint16_t time);                                //Чтение статуса и ожидание флага на busy
extern uint16_t         read_dev_id_memory(void);                            	//Чтение id
extern uint8_t 			write_sector_erase_memory(uint16_t page_addr, uint16_t byte_addr);

extern uint16_t         power_of_2_at64(void);                                  //Поменять режим памяти на 256 байт

extern uint16_t         ultra_deep_power_down_memory(void);                       //Вход в режим
extern uint16_t         exit_ultra_deep_power_down_memory(void);                  //Выход из режима

extern void             Save_param_memory(uint16_t page_addr, uint8_t *Data, uint16_t len_reg);
extern void             Read_param_memory(uint16_t page_addr, uint8_t *Data, uint16_t len_reg);

extern void             SaveCurrentParamFirmware(uint16_t *status, uint16_t *size);
extern void             ReadCurrentParamFirmware(uint16_t *status, uint16_t *size);

extern void             SaveFirmwareMemory(uint32_t page_addr, uint8_t *Data, uint16_t len_reg);
extern void             ReadFirmwareMemory(uint32_t page_addr, uint8_t *Data, uint16_t len_reg);
//==============================================================================
#endif /* EXT_FLASH_MEM_H_ */
