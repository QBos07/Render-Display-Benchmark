#include <appdef.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cinttypes>
#include <numbers>
#include <sdk/os/debug.h>
#include <sdk/os/lcd.h>
#include <sdk/calc/calc.h>
#include "power.h"
#include "tmu.h"

#define MODE_FULL 0
#define MODE_LINE 1
#define MODE_PIXEL 2

#define MODE MODE_LINE

#define USE_IL
#define USE_X
#define USE_Y
#define USE_DMA
#define USE_UP

#if MODE == MODE_FULL
#define MS "F"
#elif MODE == MODE_LINE
#define MS "L"
#elif MODE == MODE_PIXEL
#define MS "P"
#endif

#ifdef USE_DMA
#include "dmac.h"
#define DMAS "-DMA"
#else
#define DMAS
#endif

#if MODE != MODE_LINE && defined(USE_Y) && !(MODE == MODE_PIXEL && defined(USE_UP))
#error "USE_Y needs MODE_LINE or MODE_PIXEL with USE_UP"
#endif
#if MODE == MODE_PIXEL && defined(USE_DMA) && !defined(USE_UP)
#error "USE_DMA doesnt work with MODE_PIXEL without USE_UP"
#endif

#define TO_STRING(x) #x
#define TO_STRING2(x) TO_STRING(x)

#ifdef USE_IL
#define IL_ATTR [[gnu::section(".oc_mem.il." TO_STRING2(__COUNTER__))]]
#define ILS "-IL"
#else
#define IL_ATTR
#define ILS
#endif

#ifdef USE_X
extern "C" void on_alt_stack(void *, void (*)());
#define XS "-X"
#else
#define on_alt_stack(s, f) f()
#define XS
#endif

#ifdef USE_Y
#define YS "-Y"
#else
#define YS
#endif

#ifdef USE_UP
#define UP_MUL(x) ((x) * 2)
#define UP_DIV(x) ((x) / 2)
#define UPS "-UP"
#else
#define UP_MUL
#define UP_DIV
#define UPS
#endif

#if defined(USE_UP) && defined(USE_DMA)
#define UP_DMA_MUL(x) ((x) * 2)
#define UP_DMA_DIV(x) ((x) / 2)
#else
#define UP_DMA_MUL
#define UP_DMA_DIV
#endif

APP_NAME("Bencher " MS ILS XS YS DMAS UPS)
APP_AUTHOR("QBos07")
APP_DESCRIPTION("Benchmarks various display and rendering aspects")
APP_VERSION("1.0.0 " __TIMESTAMP__)

constexpr std::array<uint8_t, 256> make_sin_lut()
{
    std::array<uint8_t, 256> lut{};

    for (int i = 0; i < 256; ++i)
    {
        const auto angle = (i / 256.) * 2. * std::numbers::pi;
        const auto s = __builtin_sin(angle); // std::sin has constexpr problems
        lut[i] = static_cast<std::uint8_t>((s * 127.5) + 127.5);
    }

    return lut;
}

/*[[gnu::section(".oc_mem.x.sin")]]*/ [[gnu::aligned(32)]] static const auto sin_lut = make_sin_lut();
#ifdef USE_X
[[gnu::section(".oc_mem.x.stack")]] [[gnu::aligned(4)]] static std::uint8_t xstack[4 * 1024]{};
static const auto xstack_begin = xstack + sizeof(xstack);
#endif
#if MODE == MODE_LINE || (defined(USE_UP) && MODE == MODE_PIXEL)
#ifdef USE_Y
[[gnu::section(".oc_mem.y.buf")]]
#endif
[[gnu::aligned(32)]] static std::uint16_t linebuf[2][UP_DIV(UP_DMA_MUL(width))]{};
#endif

#if MODE == MODE_FULL
IL_ATTR static void update_full(std::uint16_t *buffer) {
    #ifdef USE_DMA
        dma_wait(DMAC_CHCR_0);
    #endif
    LCD_SetDrawingBounds(0, width - 1, 0, height - 1);
    LCD_SendCommand(COMMAND_PREPARE_FOR_DRAW_DATA);
    #ifndef USE_DMA
    #ifndef USE_UP
    for (auto pixel = buffer; pixel < buffer + width * height; pixel++)
        *lcd_data_port = *pixel;
    #else
    for (auto line = buffer; line < buffer + (height / 2) * (width / 2); line += width / 2)
    {
        for (auto pixel = line; pixel < line + width / 2; pixel++) {
            *lcd_data_port = *pixel;
            *lcd_data_port = *pixel;
        }
        for (auto pixel = line; pixel < line + width / 2; pixel++) {
            *lcd_data_port = *pixel;
            *lcd_data_port = *pixel;
        }
    }
    #endif
    #else
        dmac_chcr tmp_chcr = { .raw = 0 };
        tmp_chcr.s.TS_0 = SIZE_2_0;
        tmp_chcr.s.TS_1 = SIZE_2_1;
        tmp_chcr.s.DM   = DAR_FIXED_HARD;
        tmp_chcr.s.SM   = SAR_INCREMENT;
        tmp_chcr.s.RS   = AUTO;
        tmp_chcr.s.TB   = CYCLE_STEAL;
        tmp_chcr.s.RPT  = REPEAT_NORMAL;
        tmp_chcr.s.DE   = 1;

        DMAC_CHCR_0->raw = 0;
        *DMAC_SAR_0 = reinterpret_cast<std::uintptr_t>(buffer) & 0x1FFFFFFF;
        *DMAC_DAR_0 = reinterpret_cast<std::uintptr_t>(lcd_data_port) & 0x1FFFFFFF;
        *DMAC_TCR_0 = width * height;

        for (auto ptr = reinterpret_cast<std::uintptr_t>(buffer); ptr < reinterpret_cast<std::uintptr_t>(buffer + width * height); ptr += 32)
            __asm__ volatile ("ocbwb @%0" : : "r"(ptr));

        DMAC_CHCR_0->raw = tmp_chcr.raw;
    #endif
}

#elif MODE == MODE_LINE

IL_ATTR static void update_line(std::uint16_t *buffer, unsigned line) {
    #ifdef USE_DMA
        dma_wait(DMAC_CHCR_0);
    #endif
    LCD_SetDrawingBounds(0, width - 1, line, line
    #ifdef USE_UP
    + 1
    #endif
    );
    LCD_SendCommand(COMMAND_PREPARE_FOR_DRAW_DATA);
    #ifndef USE_DMA
    #ifndef USE_UP
    for (auto pixel = buffer; pixel < buffer + width; pixel++)
        *lcd_data_port = *pixel;
    #else
    for (auto pixel = buffer; pixel < buffer + width / 2; pixel++) {
        *lcd_data_port = *pixel;
        *lcd_data_port = *pixel;
    }
    for (auto pixel = buffer; pixel < buffer + width / 2; pixel++) {
        *lcd_data_port = *pixel;
        *lcd_data_port = *pixel;
    }
    #endif
    #else
        dmac_chcr tmp_chcr = { .raw = 0 };
        tmp_chcr.s.TS_0 = SIZE_2_0;
        tmp_chcr.s.TS_1 = SIZE_2_1;
        tmp_chcr.s.DM   = DAR_FIXED_HARD;
        tmp_chcr.s.SM   = SAR_INCREMENT;
        tmp_chcr.s.RS   = AUTO;
        tmp_chcr.s.TB   = CYCLE_STEAL;
        #ifndef USE_UP
        tmp_chcr.s.RPT  = REPEAT_NORMAL;
        #else
        tmp_chcr.s.RPT  = RELOAD_SAR_TCR;
        #endif
        tmp_chcr.s.DE   = 1;

        DMAC_CHCR_0->raw = 0;
        *DMAC_SAR_0 = reinterpret_cast<std::uintptr_t>(buffer)
        #ifndef USE_Y
        & 0x1FFFFFFF
        #endif
        ;
        *DMAC_DAR_0 = reinterpret_cast<std::uintptr_t>(lcd_data_port) & 0x1FFFFFFF;
        *DMAC_TCR_0 = UP_MUL(width);
        #ifdef USE_UP
        *DMAC_TCRB_0 = width << 16 | width;
        #endif

        for (auto ptr = reinterpret_cast<std::uintptr_t>(buffer); ptr < reinterpret_cast<std::uintptr_t>(buffer + width); ptr += 32)
            __asm__ volatile ("ocbwb @%0" : : "r"(ptr));

        DMAC_CHCR_0->raw = tmp_chcr.raw;
    #endif
}

#endif

#if MODE != MODE_PIXEL
IL_ATTR static void update_bench() {
    LCD_SetDrawingBounds(0, debug_char_width * 19 - 1, 0, debug_line_height * 3 - 1);
    LCD_SendCommand(COMMAND_PREPARE_FOR_DRAW_DATA);
    for (size_t line = 0; line < debug_line_height * 3; line++)
        for(size_t off = 0; off < debug_char_width * 19; off++)
            *lcd_data_port = vram[line * width + off];
}
#else
IL_ATTR static void update_bench() {
    LCD_SetDrawingBounds(0, debug_char_width * 19 - 1, debug_line_height * 2, debug_line_height * 3 - 1);
    LCD_SendCommand(COMMAND_PREPARE_FOR_DRAW_DATA);
    for (size_t line = debug_line_height * 2; line < debug_line_height * 3; line++)
        for(size_t off = 0; off < debug_char_width * 19; off++)
            *lcd_data_port = vram[line * width + off];
}
#endif

IL_ATTR static void render_plasma_line(int y, uint16_t frame_phase
#if MODE != MODE_PIXEL || defined(USE_UP)
    , uint16_t *target
#endif
)
{
    // 8.8 fixed point phases
    std::uint16_t x_phase = frame_phase;
    std::uint16_t y_phase = (y << 4) + frame_phase;

    std::uint16_t x_step = UP_MUL(5 << 8);      // horizontal frequency
    std::uint16_t y_mod  = y_phase >> 8;

    for (std::size_t x = 0; x < UP_DIV(UP_DMA_MUL(width)); x += UP_DMA_MUL(1), x_phase += x_step)
    {
        std::uint8_t s1 = sin_lut[x_phase >> 8];
        std::uint8_t s2 = sin_lut[y_mod];
        std::uint8_t s3 = sin_lut[((x_phase + y_phase) >> 9) & 0xFF];

        std::uint16_t sum = s1 + s2 + s3;   // 0–765

        std::uint8_t v = sum >> 2;          // 0–191 approx

        // RGB565 mapping (fast, no multiplies)
        std::uint16_t r = (v & 0x1F) << 11;
        std::uint16_t g = (v & 0x3F) << 5;
        std::uint16_t b = (v & 0x1F);

        std::uint16_t pixel = r | g | b;
        #if MODE != MODE_PIXEL
        target[x] = pixel;
        #else
        *lcd_data_port = pixel;
        #endif
        #ifdef USE_UP
        #if MODE == MODE_PIXEL
        *lcd_data_port = pixel;
        target[x] = pixel;
        #endif
        #ifdef USE_DMA
        target[x + 1] = pixel;
        #endif
        #endif
    }
}

#if MODE == MODE_FULL && defined(USE_DMA) && defined(USE_UP)
IL_ATTR void dmac_copy(std::size_t y) {
    const auto channel = (y / 2) % 2;
    const auto chcr = channel ? DMAC_CHCR_1 : DMAC_CHCR_0;
    const auto tcr = channel ? DMAC_TCR_1 : DMAC_TCR_0;
    const auto sar = channel ? DMAC_SAR_1 : DMAC_SAR_0;
    const auto dar = channel ? DMAC_DAR_1 : DMAC_DAR_0;

    dma_wait(chcr);
    
    dmac_chcr tmp_chcr = { .raw = 0 };
    tmp_chcr.s.TS_0 = SIZE_2_0;
    tmp_chcr.s.TS_1 = SIZE_2_1;
    tmp_chcr.s.DM   = DAR_INCREMENT;
    tmp_chcr.s.SM   = SAR_INCREMENT;
    tmp_chcr.s.RS   = AUTO;
    tmp_chcr.s.TB   = CYCLE_STEAL;
    tmp_chcr.s.RPT  = REPEAT_NORMAL;
    tmp_chcr.s.DE   = 1;

    chcr->raw = 0;
    *sar = reinterpret_cast<std::uintptr_t>(vram + y * width) & 0x1FFFFFFF;
    *dar = reinterpret_cast<std::uintptr_t>(vram + (y + 1) * width) & 0x1FFFFFFF;
    *tcr = width;

    for (auto ptr = reinterpret_cast<std::uintptr_t>(vram + y * width); ptr < reinterpret_cast<std::uintptr_t>(vram + (y + 1) * width); ptr += 32)
        __asm__ volatile ("ocbwb @%0" : : "r"(ptr));

    chcr->raw = tmp_chcr.raw;
}
#endif

#if MODE == MODE_PIXEL && defined(USE_UP)
IL_ATTR void send_line(std::uint16_t *buffer) {
    #ifndef USE_DMA
        for (auto pixel = buffer; pixel < buffer + width / 2; pixel++) {
            *lcd_data_port = *pixel;
            *lcd_data_port = *pixel;
        }
    #else
        dmac_chcr tmp_chcr = { .raw = 0 };
        tmp_chcr.s.TS_0 = SIZE_2_0;
        tmp_chcr.s.TS_1 = SIZE_2_1;
        tmp_chcr.s.DM   = DAR_FIXED_HARD;
        tmp_chcr.s.SM   = SAR_INCREMENT;
        tmp_chcr.s.RS   = AUTO;
        tmp_chcr.s.TB   = CYCLE_STEAL;
        tmp_chcr.s.RPT  = REPEAT_NORMAL;
        tmp_chcr.s.DE   = 1;

        DMAC_CHCR_0->raw = 0;
        *DMAC_SAR_0 = reinterpret_cast<std::uintptr_t>(buffer)
        #ifndef USE_Y
        & 0x1FFFFFFF
        #endif
        ;
        *DMAC_DAR_0 = reinterpret_cast<std::uintptr_t>(lcd_data_port) & 0x1FFFFFFF;
        *DMAC_TCR_0 = width;

        for (auto ptr = reinterpret_cast<std::uintptr_t>(buffer); ptr < reinterpret_cast<std::uintptr_t>(buffer + width); ptr += 32)
            __asm__ volatile ("ocbwb @%0" : : "r"(ptr));

        DMAC_CHCR_0->raw = tmp_chcr.raw;
        dma_wait(DMAC_CHCR_0);
    #endif
}
#endif

#if MODE == MODE_FULL
IL_ATTR [[gnu::noinline]] static void do_bench() {
    *TMU_TCOR_1 = -1;
    
    *TMU_TCNT_1 = -1;
    TMU_TSTR->s.STR1 = 1;
    for (std::size_t y = 0; y < height; y += UP_MUL(1))
    {
        render_plasma_line(y, y * 3, vram + UP_DIV(UP_DMA_MUL(y)) * UP_DIV(UP_DMA_MUL(width)));
        #if defined(USE_UP) && defined(USE_DMA)
        dmac_copy(y);
        #endif
    }
    #if defined(USE_UP) && defined(USE_DMA)
    dma_wait(DMAC_CHCR_1);
    #endif
    const auto after_render = *TMU_TCNT_1;
    update_full(vram);
    #ifdef USE_DMA
    dma_wait(DMAC_CHCR_0);
    #endif
    const auto after_refresh = *TMU_TCNT_1;
    Debug_Printf(0, 0, false, 0, "gen# %8" PRIu32 " ticks", -1u - after_render);
    Debug_Printf(0, 1, false, 0, "ref# %8" PRIu32 " ticks", after_render - after_refresh);
    Debug_Printf(0, 2, false, 0, "all# %8" PRIu32 " ticks", -1u - after_refresh);
    update_bench();
}
#elif MODE == MODE_LINE
IL_ATTR [[gnu::noinline]] static void do_bench() {
    *TMU_TCOR_1 = -1;
    
    std::uint32_t render_ticks = 0;
    std::uint32_t refresh_ticks = 0;
    std::uint32_t last = -1u;
    *TMU_TCNT_1 = last;
    TMU_TSTR->s.STR1 = 1;
    for (std::size_t y = 0; y < height; y += UP_MUL(1))
    {
        render_plasma_line(y, y * 3, linebuf[UP_DIV(y) % 2]);
        {
            const auto current = *TMU_TCNT_1;
            render_ticks += last - current;
            last = current;
        }
        update_line(linebuf[UP_DIV(y) % 2], y);
        {
            const auto current = *TMU_TCNT_1;
            refresh_ticks += last - current;
            last = current;
        }
    }
    #ifdef USE_DMA
    dma_wait(DMAC_CHCR_0);
    refresh_ticks += last - *TMU_TCNT_1;
    #endif
    Debug_Printf(0, 0, false, 0, "gen# %8" PRIu32 " ticks", render_ticks);
    Debug_Printf(0, 1, false, 0, "ref# %8" PRIu32 " ticks", refresh_ticks);
    Debug_Printf(0, 2, false, 0, "all# %8" PRIu32 " ticks", render_ticks + refresh_ticks);
    update_bench();
}
#elif MODE == MODE_PIXEL
IL_ATTR [[gnu::noinline]] static void do_bench() {
    *TMU_TCOR_1 = -1;
    
    *TMU_TCNT_1 = -1;
    TMU_TSTR->s.STR1 = 1;
    LCD_SetDrawingBounds(0, width - 1, 0, height - 1);
    LCD_SendCommand(COMMAND_PREPARE_FOR_DRAW_DATA);
    for (std::size_t y = 0; y < height; y += UP_MUL(1))
    {
        #ifndef USE_UP
        render_plasma_line(y, y * 3);
        #else
        render_plasma_line(y, y*3, linebuf[(y / 2) % 2]);
        send_line(linebuf[(y / 2) % 2]);
        #endif
    }
    const auto after = *TMU_TCNT_1;
    Debug_Printf(0, 2, false, 0, "all# %8" PRIu32 " ticks", -1u - after);
    update_bench();
}
#else
#error "Unknown Mode"
#endif

int main() {
    POWER_MSTPCR0->s.TMU = 0;
    TMU_TCR_1->raw = 0;
    TMU_TCR_1->s.TPSC = PHI_DIV_4;
    #ifdef USE_DMA
    POWER_MSTPCR0->s.DMAC = 0;
    DMAC_DMAOR->raw = 0;
    DMAC_DMAOR->s.DME = 1;
    #endif
    on_alt_stack(xstack_begin, do_bench);
    #ifdef USE_DMA
    DMAC_DMAOR->raw = 0;
    POWER_MSTPCR0->s.DMAC = 1;
    #endif
    POWER_MSTPCR0->s.TMU = 1;
    Debug_WaitKey();
    return 0;
}
