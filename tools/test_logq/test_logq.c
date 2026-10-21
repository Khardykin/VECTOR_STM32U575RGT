/*
 * tools/test_logq/test_logq.c - хост-тест журнала v2 (LOG_FORMAT.md).
 *
 * Тестирует ЧИСТУЮ логику из Config_save_read.h (геометрия кольца 40 Б ×
 * 102/сектор × 512, RAM-очередь 16×40 Б, валидность записи, решение о
 * стирании, Archiveinterval->секунды) - те же static inline, что исполняет
 * прошивка, + зеркало алгоритма писателя (log_write_rec/log_flush из
 * Config_save_read.c) поверх RAM-модели flash со случайными EXT_BUSY.
 *
 * Сборка и запуск: make run   (gcc, без зависимостей).
 * Регрессия для: PLAN.md п.58 (очередь записей + формат слота 40 Б).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Настоящие заголовки прошивки (чистая логика, HAL/ThreadX не нужны) */
#include "Config_save_read.h"   /* shared_types.h + extstore.h transitively */

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* ================================================================ геометрия */
static void test_geometry(void)
{
    const uint32_t base = 0x00402000u;          /* SF_LOG_BASE */
    const uint32_t end  = base + 0x00200000u;   /* 2 МБ, LOG_PTR на 0x602000 */

    /* размеры структур (дублирует compile-time assert'ы заголовка) */
    CHECK(sizeof(log_hdr_t) == 12u);
    CHECK(sizeof(EVENT_LOG) == 40u);
    CHECK(sizeof(log_pl_snapshot_t) == 28u);
    CHECK(sizeof(log_pl_readings_t) == 28u);
    CHECK(sizeof(log_pl_gas_t) == 28u);
    CHECK(sizeof(log_pl_power_t) == 28u);
    CHECK(sizeof(log_pl_ppe_t) == 28u);
    CHECK(sizeof(log_pl_beacons_t) == 28u);
    CHECK(sizeof(log_pl_param_t) == 28u);
    CHECK(LOG_TIME_OFFSET == 4u);

    /* известные точки (§2): addr(ptr) = base + (phys/102)*4096 + (phys%102)*40 */
    CHECK(log_slot_addr_v2(base, 0u)      == base);
    CHECK(log_slot_addr_v2(base, 1u)      == base + 40u);
    CHECK(log_slot_addr_v2(base, 101u)    == base + 101u * 40u);
    CHECK(log_slot_addr_v2(base, 102u)    == base + 4096u);           /* сектор 1 */
    CHECK(log_slot_addr_v2(base, 103u)    == base + 4096u + 40u);
    /* последний слот последнего сектора: phys=52223 -> сектор 511, слот 101 */
    CHECK(log_slot_addr_v2(base, 52223u)  == base + 511u*4096u + 101u*40u);
    CHECK(log_slot_addr_v2(base, 52223u) + 40u <= end - 16u + 16u);   /* в области */
    /* заворот кольца */
    CHECK(log_slot_addr_v2(base, 52224u)  == base);
    CHECK(log_slot_addr_v2(base, 52225u)  == base + 40u);
    /* хвост сектора (16 Б) не используется: конец слота 101 != конец сектора */
    CHECK(log_slot_addr_v2(base, 101u) + 40u == base + 4080u);

    /* полный перебор кольца: адрес в области, выровнен на слот, физпозиция
       уникальна в пределах круга, sector/slot арифметика сходится */
    for (uint32_t ptr = 0; ptr < LOG_RING_SLOTS; ptr++)
    {
        uint32_t a = log_slot_addr_v2(base, ptr);
        uint32_t off = a - base;
        CHECK(a >= base && a + LOG_SLOT_SIZE <= end);
        CHECK((off % 4096u) % 40u == 0u);                 /* слот в секторе */
        CHECK((off / 4096u) == (ptr / 102u));             /* сектор */
        CHECK(((off % 4096u) / 40u) == (ptr % 102u));     /* слот */
        if (failures) break;
    }

    /* ёмкость/геометрия кольца */
    CHECK(LOG_RING_SLOTS == 52224u);
    CHECK(LOG_SLOTS_PER_SECTOR == 102u);
    CHECK(LOG_USABLE_SLOTS == 52224u - 102u);
    CHECK(LOG_SLOTS_PER_SECTOR * LOG_SLOT_SIZE <= 4096u);
}

/* ================================================== решение о стирании (§2) */
static void test_erase_decision(void)
{
    /* середина сектора - никогда не стираем */
    CHECK(log_erase_decision(103u, 0u, 1) == 0);
    CHECK(log_erase_decision(103u, 0u, 0) == 0);
    /* граница, сектор пуст - пишем без стирания */
    CHECK(log_erase_decision(102u, 0u, 1) == 0);
    /* граница, сектор не пуст (мусор/второй круг) - стирание без сдвига */
    CHECK(log_erase_decision(102u, 0u, 0) == 1);
    /* граница, кольцо полное - стирание + сдвиг начала */
    CHECK(log_erase_decision(LOG_USABLE_SLOTS, 0u, 1) == 2);
    CHECK(log_erase_decision(LOG_USABLE_SLOTS, 0u, 0) == 2);
    /* почти полное (на границе сектора, но cur-beg < USABLE) - ещё не full */
    CHECK(log_erase_decision(LOG_USABLE_SLOTS - 102u, 0u, 1) == 0);
    CHECK(log_erase_decision(LOG_USABLE_SLOTS - 102u, 0u, 0) == 1);
    /* не на границе - не стираем даже при полном кольце */
    CHECK(log_erase_decision(LOG_USABLE_SLOTS + 1u, 0u, 0) == 0);
    /* u32-заворот указателей: разница считается модульно */
    uint32_t cur = 0xFFFFFFFFu - 10u, beg = cur - LOG_USABLE_SLOTS;
    CHECK(log_erase_decision(cur + 11u, beg, 1) == 2);   /* граница + full */
}

/* ========================================================= очередь (§6) */
static EVENT_LOG mk_rec(uint32_t wh, uint32_t tag)
{
    EVENT_LOG r;
    memset(&r, 0, sizeof r);
    r.H.Type_log = TYPE_PARAM;
    r.H.Fmt_ver = LOG_FMT_VER;
    r.H.Payload_len = LOG_PAYLOAD_MAX;
    r.H.working_hours = wh;
    r.H.CurrentAddrFile = tag;    /* в тесте используем как метку порядка */
    return r;
}

static void test_queue(void)
{
    logq_ctx_t q;
    logq_init(&q);
    CHECK(logq_count(&q) == 0u);
    CHECK(logq_peek(&q) == NULL);
    logq_pop(&q);                       /* pop пустой - no-op */
    CHECK(logq_count(&q) == 0u);

    /* FIFO: 3 push, порядок сохраняется */
    for (uint32_t i = 1; i <= 3; i++) { EVENT_LOG r = mk_rec(i, i); CHECK(logq_push(&q, &r) == 0); }
    CHECK(logq_count(&q) == 3u);
    for (uint32_t i = 1; i <= 3; i++)
    {
        const EVENT_LOG *p = logq_peek(&q);
        CHECK(p && p->H.CurrentAddrFile == i && p->H.working_hours == i);
        logq_pop(&q);
    }
    CHECK(logq_count(&q) == 0u);

    /* переполнение: 16 + 1 -> старейшая перезаписана, over=1, count=16 */
    logq_init(&q);
    for (uint32_t i = 0; i < LOGQ_SLOTS; i++) { EVENT_LOG r = mk_rec(i, i); CHECK(logq_push(&q, &r) == 0); }
    CHECK(logq_full(&q) == 1);
    { EVENT_LOG r = mk_rec(100, 100); CHECK(logq_push(&q, &r) == 1); }
    CHECK(logq_count(&q) == LOGQ_SLOTS);
    CHECK(logq_peek(&q)->H.CurrentAddrFile == 1u);   /* 0 потеряна, 1 - старейшая */
    /* последние две: 15 и новая 100 */
    {
        uint32_t last = 0, prev = 0;
        while (logq_count(&q))
        {
            prev = last;
            last = logq_peek(&q)->H.CurrentAddrFile;
            logq_pop(&q);
        }
        CHECK(last == 100u);
        CHECK(prev == 15u);
    }

    /* обход индекса head через границу массива много раз */
    logq_init(&q);
    for (int cycle = 0; cycle < 100; cycle++)
    {
        for (uint32_t i = 0; i < 10; i++) { EVENT_LOG r = mk_rec(i, i); CHECK(logq_push(&q, &r) == 0); }
        for (uint32_t i = 0; i < 10; i++)
        {
            CHECK(logq_peek(&q)->H.CurrentAddrFile == i);
            logq_pop(&q);
        }
    }

    /* стресс: случайные push/pop против модельной очереди (порядок FIFO) */
    logq_init(&q);
    uint32_t model[4096]; uint32_t mhead = 0, mcount = 0, next_tag = 0;
    unsigned seed = 12345u;
    for (int step = 0; step < 200000 && failures == 0; step++)
    {
        seed = seed * 1103515245u + 12345u;
        int do_push = (int)((seed >> 16) % 100u) < 60;
        if (do_push)
        {
            EVENT_LOG r = mk_rec(next_tag, next_tag);
            int over = logq_push(&q, &r);
            if (mcount == LOGQ_SLOTS)
            {
                CHECK(over == 1);
                mhead = (mhead + 1u) % 4096u;      /* модель: старейшая потеряна */
            }
            else
            {
                CHECK(over == 0);
                mcount++;
            }
            model[(mhead + mcount - 1u) % 4096u] = next_tag;
            next_tag++;
        }
        else if (mcount > 0u)
        {
            const EVENT_LOG *p = logq_peek(&q);
            CHECK(p != NULL);
            CHECK(p->H.CurrentAddrFile == model[mhead]);
            logq_pop(&q);
            mhead = (mhead + 1u) % 4096u;
            mcount--;
        }
        CHECK(logq_count(&q) == mcount);
    }
}

/* =================================================== валидность записи (§7.1) */
static void test_rec_valid(void)
{
    EVENT_LOG r = mk_rec(1, 1);
    CHECK(log_rec_valid(&r) == 1);
    const uint8_t types[] = { TYPE_TURN_ON, TYPE_TURN_OFF, TYPE_FW_UPDATE,
                              TYPE_READINGS, TYPE_PARAM, TYPE_GAS,
                              TYPE_BEACONS, TYPE_PPE, TYPE_SNAPSHOT };
    for (unsigned i = 0; i < sizeof types; i++)
    {
        r.H.Type_log = types[i];
        CHECK(log_rec_valid(&r) == 1);
    }
    r.H.Type_log = 0x11; CHECK(log_rec_valid(&r) == 0);   /* неизвестный тип */
    r.H.Type_log = 0xFF; CHECK(log_rec_valid(&r) == 0);   /* стёртая flash   */
    r.H.Type_log = TYPE_PARAM;
    r.H.Fmt_ver = 0; CHECK(log_rec_valid(&r) == 0);       /* старый формат   */
    r.H.Fmt_ver = 2; CHECK(log_rec_valid(&r) == 0);       /* будущий         */
    r.H.Fmt_ver = LOG_FMT_VER;
    r.H.Payload_len = 29; CHECK(log_rec_valid(&r) == 0);  /* переполнение    */
    r.H.Payload_len = 0;  CHECK(log_rec_valid(&r) == 1);  /* 0 значимых - ок */

    /* Archiveinterval -> секунды (§5): (мин<<8)|сек */
    CHECK(log_archive_period_s(0x0A00) == 600u);
    CHECK(log_archive_period_s(0x0F00) == 900u);
    CHECK(log_archive_period_s(0x0505) == 305u);
    CHECK(log_archive_period_s(0x0005) == 5u);
    CHECK(log_archive_period_s(0xFFFF) == 15555u);
}

/* ================================================ писатель: RAM-flash модель */
/* Зеркало log_write_rec/log_flush (Config_save_read.c) на общей с прошивкой
   арифметике (log_slot_addr_v2/log_erase_decision/logq_*). Модель flash -
   область LOG 2 МБ; erase/write могут случайно вернуть EXT_BUSY (шина/звук) -
   записи должны остаться в очереди, порядок и нумерация - сохраниться.     */
#define FLASH_SIZE  0x00200000u
static uint8_t *g_flash;
static uint32_t g_base = 0x00402000u;
static unsigned g_seed = 777u;
static unsigned long g_erases = 0, g_busy_erases = 0, g_busy_writes = 0;

static int rnd100(void)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return (int)((g_seed >> 16) % 100u);
}
static int m_ext_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
    memcpy(dst, g_flash + (addr - g_base), len);
    return 0;
}
static int m_ext_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
    uint8_t *p = g_flash + (addr - g_base);
    if (rnd100() < 10) { g_busy_writes++; return EXT_BUSY; }  /* try-lock не удался: на шине НИЧЕГО не записано */
    /* NOR: program может только сбрасывать биты 1->0 (запись в нечистый слот
       исказила бы данные - тест это поймает на валидности/полях) */
    for (uint32_t i = 0; i < len; i++) { p[i] &= src[i]; }
    return 0;
}
static int m_ext_erase(uint32_t addr)
{
    if (rnd100() < 30) { g_busy_erases++; return EXT_BUSY; }  /* «звук» */
    memset(g_flash + (addr - g_base), 0xFF, 4096u);
    g_erases++;
    return 0;
}

/* зеркало log_write_rec */
static int m_write_rec(uint32_t *cur, uint32_t *beg, EVENT_LOG *rec)
{
    int rc;
    rec->H.CurrentAddrFile = *cur;
    if (((*cur % LOG_RING_SLOTS) % LOG_SLOTS_PER_SECTOR) == 0u)
    {
        int blank = 0, dec;
        if ((uint32_t)(*cur - *beg) < LOG_USABLE_SLOTS)
        {
            uint8_t probe[4];
            m_ext_read(log_slot_addr_v2(g_base, *cur), probe, sizeof probe);
            blank = (probe[0] == 0xFFu && probe[1] == 0xFFu &&
                     probe[2] == 0xFFu && probe[3] == 0xFFu);
        }
        dec = log_erase_decision(*cur, *beg, blank);
        if (dec != 0)
        {
            rc = m_ext_erase(log_slot_addr_v2(g_base, *cur));
            if (rc == EXT_BUSY) { return EXT_BUSY; }
            if (rc != 0) { return -1; }
            if (dec == 2) { *beg += LOG_SLOTS_PER_SECTOR; }
        }
    }
    rc = m_ext_write(log_slot_addr_v2(g_base, *cur), (const uint8_t *)rec, LOG_SLOT_SIZE);
    if (rc == EXT_BUSY) { return EXT_BUSY; }
    if (rc != 0) { return -1; }
    (*cur)++;
    return 0;
}

static void test_writer(void)
{
    logq_ctx_t q;
    uint32_t cur = 0, beg = 0;
    /* модель событий: ptr -> wh (проверяем прочитанное), окно = живые записи */
    static uint32_t ev_wh[70000];
    const uint32_t N = 60000u;    /* > ёмкости кольца 52224 - exercising wrap */

    g_flash = malloc(FLASH_SIZE);
    if (!g_flash) { printf("no mem\n"); exit(1); }
    /* «грязная» flash: второй круг + мусор старого формата - стирается лениво */
    for (uint32_t i = 0; i < FLASH_SIZE; i++) { g_flash[i] = (uint8_t)(i * 7u + 3u); }
    logq_init(&q);

    uint32_t pushed = 0, flushed = 0;
    while (pushed < N || logq_count(&q) > 0u)
    {
        /* производитель: пачка 1..3 событий (всплеск) */
        if (pushed < N)
        {
            int burst = 1 + rnd100() % 3;
            for (int i = 0; i < burst && pushed < N; i++, pushed++)
            {
                EVENT_LOG r = mk_rec(1000u + pushed, 0);
                ev_wh[pushed] = 1000u + pushed;
                (void)logq_push(&q, &r);   /* очередь не переполняется: drain чаще */
            }
        }
        /* писатель: drain до EXT_BUSY/конца (зеркало log_flush) */
        for (;;)
        {
            const EVENT_LOG *pk = logq_peek(&q);
            EVENT_LOG rec;
            int rc;
            if (!pk) { break; }
            rec = *pk;
            rc = m_write_rec(&cur, &beg, &rec);
            if (rc == EXT_BUSY) { break; }
            if (rc != 0) { CHECK(0); break; }
            logq_pop(&q);
            flushed++;
        }
        /* инварианты кольца после каждого прохода */
        CHECK((uint32_t)(cur - beg) <= LOG_USABLE_SLOTS);
    }
    CHECK(flushed == N);

    /* содержимое диска: все ЖИВЫЕ записи [beg,cur) валидны, номер = ptr,
       время = момент события (снимок); стёртые секторы за beg не читаем */
    uint32_t live = cur - beg;
    CHECK(live > 0u);
    for (uint32_t ptr = beg; ptr != cur; ptr++)
    {
        EVENT_LOG r;
        m_ext_read(log_slot_addr_v2(g_base, ptr), (uint8_t *)&r, sizeof r);
        CHECK(log_rec_valid(&r) == 1);
        CHECK(r.H.CurrentAddrFile == ptr);
        CHECK(r.H.working_hours == ev_wh[ptr]);
        if (failures) { break; }
    }
    printf("writer: %u records, live %u (beg=%u cur=%u), erases=%lu "
           "(busy-defers: erase=%lu write=%lu)\n",
           N, live, beg, cur, g_erases, g_busy_erases, g_busy_writes);
    /* первый круг по грязной flash + wrap: стирание на КАЖДОМ секторе */
    CHECK(g_erases >= (cur / LOG_SLOTS_PER_SECTOR));
    free(g_flash);
}

int main(void)
{
    test_geometry();
    test_erase_decision();
    test_queue();
    test_rec_valid();
    test_writer();
    if (failures == 0) { printf("test_logq: ALL OK\n"); return 0; }
    printf("test_logq: %d FAILURES\n", failures);
    return 1;
}
