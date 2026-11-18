/**
  ******************************************************************************
  * @file    Command_vector.c
  * @brief   Загрузка данных во внешнюю SPI flash по COM. См. Command_vector.h.
  *          Приём: USART3_IRQHandler -> InputBuffer[TYPE_USART] (кольцо 254 Б),
  *          разбор: сильное command_message() из receiver_task (10 мс).
  ******************************************************************************
  */
#include <string.h>
#include "Command_vector.h"
#include "spiflash.h"
#include "audio_image.h"

#define CMD_SOUNDS_LIMIT    0x00400000u   /* sfmap.h: выше - CONFIG/CFG_DEF/LOG */
#define CMD_ERASE_KEY       0xA5u

static cmd_flash_pause_t s_pause  = 0;
static cmd_commit_t      s_commit = 0;
static cmd_tx_t          s_tx     = 0;
static uint8_t           s_rx[CMD_MAX_FRAME];
static uint16_t          s_rx_len = 0u;
static uint16_t          s_expect = 0u;
static uint8_t           s_ready  = 0u;
static uint32_t          s_rx_ms  = 0u;

void Command_vector_SetPauseHook(cmd_flash_pause_t h) { s_pause = h; }
void Command_vector_SetCommitHook(cmd_commit_t h)     { s_commit = h; }
void Command_vector_SetTx(cmd_tx_t h)                 { s_tx = h; }

uint16_t Command_vector_CRC16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFu; uint16_t i; uint8_t b;
  for (i = 0u; i < len; i++) {
    crc ^= (uint16_t)data[i];
    for (b = 0u; b < 8u; b++) {
      if ((crc & 1u) != 0u) { crc = (uint16_t)((crc >> 1) ^ 0xA001u); }
      else                  { crc = (uint16_t)(crc >> 1); }
    }
  }
  return crc;
}
static uint32_t rd32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3]; }
static void wr32(uint8_t *p,uint32_t v){ p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }
static void wr16(uint8_t *p,uint16_t v){ p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
static uint16_t finish(uint8_t *rsp,uint16_t len)
{
  uint16_t crc = Command_vector_CRC16(rsp,len);
  rsp[len]=(uint8_t)(crc&0xFFu); rsp[len+1u]=(uint8_t)(crc>>8);
  return (uint16_t)(len+2u);
}
static uint16_t err(uint8_t *rsp,uint8_t slave,uint8_t cmd,uint8_t code)
{ rsp[0]=slave; rsp[1]=(uint8_t)(cmd|0x80u); rsp[2]=code; return finish(rsp,3u); }

/* Программируем постранично: sf_program может не делить буфер сам. */
static uint8_t program_pages(uint32_t addr,const uint8_t *src,uint32_t len)
{
  while (len != 0u) {
    uint32_t room = SF_PAGE_SIZE - (addr % SF_PAGE_SIZE);
    uint32_t n = (len < room) ? len : room;
    if (sf_program(addr,src,n) != HAL_OK) { return CMD_ERR_IO; }
    addr += n; src += n; len -= n;
  }
  return 0u;
}

uint16_t Command_vector_Frame(const uint8_t *req,uint16_t req_len,uint8_t *rsp,uint16_t rsp_cap)
{
  uint8_t slave,cmd; uint32_t addr; uint16_t size,cc,cr;
  if ((req==0)||(rsp==0)||(req_len < (CMD_FRAME_HDR+2u))) { return 0u; }
  if (rsp_cap < (CMD_FRAME_HDR+2u)) { return 0u; }
  cc = Command_vector_CRC16(req,(uint16_t)(req_len-2u));
  cr = (uint16_t)req[req_len-2u] | ((uint16_t)req[req_len-1u]<<8);
  if (cc != cr) { return 0u; }
  slave=req[0]; cmd=req[1]; addr=rd32(&req[2]);
  size=(uint16_t)(((uint16_t)req[6]<<8)|req[7]);
  if ((uint32_t)req_len != (CMD_FRAME_HDR+(uint32_t)size+2u)) { return 0u; }
  memcpy(rsp,req,CMD_FRAME_HDR);          /* эхо шапки - как в Avis CmdFunc15 */

  switch (cmd) {
  case CMD_OP_INFO: {
    audio_img_header_t h; audio_img_entry_t e; uint8_t ok=0u; uint16_t rate=0u;
    if (rsp_cap < (CMD_FRAME_HDR+23u+2u)) { return 0u; }
    memset(&h,0,sizeof(h)); memset(&e,0,sizeof(e));
    if (sf_read(AUDIO_IMG_BASE_ADDR,(uint8_t*)&h,sizeof(h)) == HAL_OK) {
      ok = (h.magic == AUDIO_IMG_MAGIC) ? 1u : 0u;
      if ((ok!=0u)&&(h.count!=0u)) {
        (void)sf_read(AUDIO_IMG_BASE_ADDR+h.table_off,(uint8_t*)&e,sizeof(e));
        rate = e.rate;
      }
    }
    wr32(&rsp[8],(uint32_t)SF_TOTAL_SIZE); wr16(&rsp[12],(uint16_t)SF_SECTOR_SIZE);
    wr16(&rsp[14],(uint16_t)SF_PAGE_SIZE); wr32(&rsp[16],h.magic);
    wr16(&rsp[20],h.version); wr16(&rsp[22],h.count); wr32(&rsp[24],h.total_size);
    wr16(&rsp[28],rate); rsp[30]=ok; wr16(&rsp[6],23u);
    return finish(rsp,(uint16_t)(CMD_FRAME_HDR+23u));
  }
  case CMD_OP_COMMIT:
    if (s_commit != 0) { s_commit(); }
    wr16(&rsp[6],0u); return finish(rsp,CMD_FRAME_HDR);

  case CMD_OP_ERASE: {
    uint32_t len,first,last,a;
    if (size != 5u) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    len = rd32(&req[8]);
    if ((len==0u)||(addr>=(uint32_t)SF_TOTAL_SIZE)||(len>((uint32_t)SF_TOTAL_SIZE-addr)))
    { return err(rsp,slave,cmd,CMD_ERR_RANGE); }
    if (((addr+len)>CMD_SOUNDS_LIMIT)&&(req[12]!=CMD_ERASE_KEY))
    { return err(rsp,slave,cmd,CMD_ERR_PROTECTED); }
    first = addr-(addr%SF_SECTOR_SIZE);
    last  = (addr+len-1u); last = last-(last%SF_SECTOR_SIZE);
    if (s_pause!=0) { s_pause(1u); }
    for (a=first;a<=last;a+=SF_SECTOR_SIZE) {
      if (sf_sector_erase(a)!=HAL_OK) {
        if (s_pause!=0) { s_pause(0u); }
        return err(rsp,slave,cmd,CMD_ERR_IO);
      }
    }
    if (s_pause!=0) { s_pause(0u); }
    wr16(&rsp[6],0u); return finish(rsp,CMD_FRAME_HDR);
  }
  case CMD_OP_WRITE: {
    uint8_t rc;
    if ((size==0u)||(size>CMD_MAX_PAYLOAD)) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    if ((addr>=(uint32_t)SF_TOTAL_SIZE)||((uint32_t)size>((uint32_t)SF_TOTAL_SIZE-addr)))
    { return err(rsp,slave,cmd,CMD_ERR_RANGE); }
    /* Запись вне SOUNDS разрешена - той же командой льём и другие данные. */
    if (s_pause!=0) { s_pause(1u); }
    rc = program_pages(addr,&req[8],size);
    if (rc==0u) { if (sf_verify(addr,&req[8],size)!=HAL_OK) { rc=CMD_ERR_VERIFY; } }
    if (s_pause!=0) { s_pause(0u); }
    if (rc!=0u) { return err(rsp,slave,cmd,rc); }
    wr16(&rsp[6],0u); return finish(rsp,CMD_FRAME_HDR);
  }
  case CMD_OP_READ: {
    uint16_t want;
    if (size!=2u) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    want=(uint16_t)(((uint16_t)req[8]<<8)|req[9]);
    if ((want==0u)||(want>CMD_MAX_PAYLOAD)) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    if (rsp_cap < (uint16_t)(CMD_FRAME_HDR+want+2u)) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    if ((addr>=(uint32_t)SF_TOTAL_SIZE)||((uint32_t)want>((uint32_t)SF_TOTAL_SIZE-addr)))
    { return err(rsp,slave,cmd,CMD_ERR_RANGE); }
    if (sf_read(addr,&rsp[CMD_FRAME_HDR],want)!=HAL_OK) { return err(rsp,slave,cmd,CMD_ERR_IO); }
    wr16(&rsp[6],want); return finish(rsp,(uint16_t)(CMD_FRAME_HDR+want));
  }
  default: return err(rsp,slave,cmd,CMD_ERR_UNKNOWN);
  }
}

/* Накопитель: конец кадра - по длине из шапки (size16), а НЕ по паузе в линии:
   поток зовёт нас каждые 10 мс, и тайминг байтов недоступен. CMD_RESYNC_MS
   нужен только чтобы выбросить недобитый кадр.                             */
void Command_vector_RxFeed(const uint8_t *data,uint16_t len,uint32_t now_ms)
{
  uint16_t i;
  if ((data==0)||(len==0u)) { return; }
  for (i=0u;i<len;i++) {
    if (s_ready!=0u) { break; }                 /* прошлый кадр ещё не разобран */
    if (s_rx_len>=CMD_MAX_FRAME) { s_rx_len=0u; s_expect=0u; }
    s_rx[s_rx_len++]=data[i];
    if (s_rx_len==CMD_FRAME_HDR) {
      uint16_t sz=(uint16_t)(((uint16_t)s_rx[6]<<8)|s_rx[7]);
      if (sz>CMD_MAX_PAYLOAD) { s_rx_len=0u; s_expect=0u; }
      else { s_expect=(uint16_t)(CMD_FRAME_HDR+sz+2u); }
    }
    if ((s_expect!=0u)&&(s_rx_len>=s_expect)) { s_ready=1u; s_expect=0u; }
  }
  s_rx_ms=now_ms;
}

void Command_vector_Poll(uint32_t now_ms)
{
  uint8_t rsp[CMD_MAX_FRAME]; uint16_t n;
  if (s_ready!=0u) {
    n = Command_vector_Frame(s_rx,s_rx_len,rsp,(uint16_t)sizeof(rsp));
    s_rx_len=0u; s_expect=0u; s_ready=0u;
    if ((n!=0u)&&(s_tx!=0)) { s_tx(rsp,n); }
    return;
  }
  if ((s_rx_len!=0u)&&((uint32_t)(now_ms-s_rx_ms)>=CMD_RESYNC_MS))
  { s_rx_len=0u; s_expect=0u; }
}

#if CMD_VECTOR_COM
#include "buffer.h"
#include "audio_player.h"
#include "vector_tick.h"

static uint8_t s_wired = 0u;

static void cmd_tx_com(const uint8_t *d,uint16_t n)
{ transmit_buffer((uint8_t*)d,n,TYPE_USART); }

static void cmd_pause_com(uint8_t pause)
{ if (pause!=0u) { audio_stop(); } }   /* образ читать нельзя, пока пишем/стираем */

static void cmd_commit_com(void)
{ audio_reload_image(); }              /* плеер перечитает заголовок после заливки */

/* Сильное определение вместо weak-заглушки main_vector.c: вызывается из
   receiver_task каждые VECTOR_TASKS_RECEIVER_DELAY_MS (10 мс). Кольцо
   InputBuffer[TYPE_USART] наполняет USART3_IRQHandler - здесь только читаем.
   Кольцо 254 Б при 115200 заполняется за ~22 мс, вычитываем его целиком.   */
void command_message(void)
{
  uint8_t  batch[64];
  uint16_t n = 0u;
  uint8_t  b;
  uint32_t now = VTICK_MS();

  if (s_wired==0u) {
    Command_vector_SetTx(cmd_tx_com);
    Command_vector_SetPauseHook(cmd_pause_com);
    Command_vector_SetCommitHook(cmd_commit_com);
    s_wired = 1u;
  }
  while (check_buffer(&InputBuffer[TYPE_USART])!=0u) {
    if (receive_buffer(&InputBuffer[TYPE_USART],&b)==0u) { break; }
    batch[n++]=b;
    if (n==(uint16_t)sizeof(batch)) { Command_vector_RxFeed(batch,n,now); n=0u; }
  }
  if (n!=0u) { Command_vector_RxFeed(batch,n,now); }
  Command_vector_Poll(now);
}
#endif
