#ifndef INSTANTNES_H
#define INSTANTNES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(_WIN32) && defined(IN_BUILD_SHARED)
#define IN_API __declspec(dllexport)
#elif defined(_WIN32)
#define IN_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(IN_BUILD_SHARED)
#define IN_API __attribute__((visibility("default")))
#else
#define IN_API
#endif
#define IN_WIDTH 256
#define IN_HEIGHT 240
#define IN_API_VERSION 1
typedef struct in_system in_system;
/* MMC3 IRQ silicon variants. AUTO retains legacy behavior for ambiguous iNES mapper 4. */
typedef enum { IN_MMC3_REV_AUTO=0, IN_MMC3_REV_B=1, IN_MMC3_REV_A=2 } in_mmc3_revision;
typedef enum { IN_OK=0, IN_ERR_ARGUMENT=-1, IN_ERR_MEMORY=-2, IN_ERR_FORMAT=-3, IN_ERR_UNSUPPORTED=-4, IN_ERR_IO=-5 } in_result;
IN_API in_result in_set_mmc3_revision(in_system *s,in_mmc3_revision revision);
IN_API in_mmc3_revision in_get_mmc3_revision(const in_system *s);
typedef struct { uint8_t a,x,y,sp,p; uint16_t pc; uint64_t cycles; } in_cpu_state;
typedef struct { uint8_t buttons[2]; } in_input;
IN_API in_system *in_create(void);
IN_API void in_destroy(in_system *s);
IN_API in_result in_load_ines(in_system *s,const uint8_t *data,size_t size);
IN_API in_result in_load_file(in_system *s,const char *path);
IN_API void in_unload(in_system *s);
IN_API in_result in_reset(in_system *s);
IN_API void in_request_nmi(in_system *s);
IN_API void in_set_irq_line(in_system *s,int asserted);
IN_API in_result in_step_cpu(in_system *s,uint32_t *cycles);
IN_API in_result in_run_frame(in_system *s);
IN_API const uint32_t *in_framebuffer(const in_system *s);
IN_API void in_set_input(in_system *s,const in_input *input);
IN_API int in_has_rom(const in_system *s);
/* Side-effect-free PRG RAM inspection for external test harnesses. */
IN_API uint8_t in_debug_peek_prg_ram(const in_system *s,uint16_t address);
/* Drains generated mono PCM at approximately 44.1 kHz, signed 16-bit. */
/* Mute only the PCM output; APU clocks, registers, IRQs and DMA continue normally. */
IN_API void in_set_mute(in_system *s,int muted);
IN_API int in_get_mute(const in_system *s);
IN_API size_t in_audio_read(in_system *s,int16_t *samples,size_t capacity);
/* Same-build state format; rejects different ROMs, corrupt files and incompatible builds. */
IN_API in_result in_save_state(in_system *s,const char *path);
IN_API in_result in_load_state(in_system *s,const char *path);
IN_API in_cpu_state in_get_cpu_state(const in_system *s);
IN_API const char *in_result_string(in_result result);
#ifdef __cplusplus
}
#endif
#endif
