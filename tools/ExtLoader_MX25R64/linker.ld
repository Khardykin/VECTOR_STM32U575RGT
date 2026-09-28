/*
*****************************************************************************
**  File        : linker.ld
**
**  Линкер external loader'а для STM32CubeProgrammer (STM32U575RGTx).
**  Основан на шаблоне ST (stm32-memory-loaders, "Loader_Files/other devices")
**  и статье ST "How to add your SPI flash into the STM32CubeProgrammer's
**  external loader" (часть 1).
**
**  Смысл: ВЕСЬ код лоадера исполняется из RAM (CubeProgrammer загружает его
**  по SWD и вызывает функции), поэтому:
**    - точка входа Init (не Reset_Handler);
**    - два PT_LOAD-сегмента: Loader (код/данные) и SgInfo (структура
**      StorageInfo из Dev_Inf.c - по ней CubeProgrammer узнаёт геометрию);
**    - память одна: RAM с ORIGIN 0x20000004 (первые 4 байта не занимаем).
**
**  В CubeIDE: Project -> Properties -> C/C++ Build -> Settings ->
**  MCU GCC Linker -> General: Linker Script = этот файл; там же снять
**  галку "Discard unused sections" (иначе линкер выбросит функции API -
**  внутри проекта они "не используются", их зовёт CubeProgrammer).
*****************************************************************************
*/

/* Entry Point */
ENTRY(Init)

/* Generate 2 segment for Loader code and device info */
PHDRS {Loader PT_LOAD ; SgInfo PT_LOAD ; }

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);    /* end of RAM */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200 ;      /* required amount of heap  */
_Min_Stack_Size = 0x400 ;     /* required amount of stack */

/* Specify the memory areas: SRAM1 STM32U575 = 192K @ 0x20000000.
   Лоадеру с запасом хватает 128K. */
MEMORY
{
  RAM (xrw) : ORIGIN = 0x20000004, LENGTH = 128K
}

/* Define output sections */
SECTIONS
{
  /* Векторная таблица (из startup-файла проекта) - со смещением 0x1FC,
     то есть по адресу 0x20000200; туда же Loader_Src.c ставит SCB->VTOR. */
  .isr_vector :
  {
    . = . + 0x1FC;
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >RAM :Loader

  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >RAM :Loader
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >RAM :Loader

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >RAM :Loader

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >RAM :Loader

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >RAM :Loader

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM :Loader

  /* Uninitialized data section */
  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM :Loader

  /* Код. KEEP'ы обязательны: функции API лоадера внутри проекта никто не
     вызывает, их находит по символам STM32CubeProgrammer. */
  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))
    KEEP (*Loader_Src.o (.text*))
    KEEP (*mx25r_drv.o  (.text*))

    . = ALIGN(4);
    _etext = .;
  } >RAM :Loader

  /* Паспорт памяти для CubeProgrammer - отдельный сегмент SgInfo */
  .Dev_info :
  {
    __Dev_info_START = .;
    KEEP(*Dev_Inf.o (.rodata*))
    *(.Dev_info*)
    KEEP(*(.Dev_info))
    __Dev_info_END = .;
  } >RAM :SgInfo

  /* Constant data */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >RAM :Loader

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(4);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(4);
  } >RAM :Loader

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
