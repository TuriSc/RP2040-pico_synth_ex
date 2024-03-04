/* pico_synth_ex
 * Original author: ISGK Instruments (Ryo Ishigaki)
 * Original version: v0.1.0 (2021-09-02)
 * https://github.com/risgk/pico_synth_ex   https://risgk.github.io
 * Licensed under a CC0 license
 *
 * C module by Turi Scandurra
 * version 1.0.0
 * Licensed under a MIT-0 license
 *
 */
 
#ifndef PICO_SYNTH_EX_H_
#define PICO_SYNTH_EX_H_

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int8_t  Octave_shift;
  uint8_t Osc_waveform;
  int8_t  Osc_2_coarse_pitch;
  int8_t  Osc_2_fine_pitch;
  uint8_t Osc_1_2_mix;
  uint8_t Filter_cutoff;
  uint8_t Filter_resonance;
  int8_t  Filter_mod_amount;
  uint8_t EG_decay_time;
  uint8_t EG_sustain_level;
  uint8_t LFO_depth;
  uint8_t LFO_rate;
} Preset_t;

// Synth parameters for direct access
typedef enum {
  OCTAVE_SHIFT,
  OSC_WAVEFORM,
  OSC_2_COARSE_PITCH,
  OSC_2_FINE_PITCH,
  OSC_1_2_MIX,
  EG_SUSTAIN_LEVEL,
  EG_DECAY_TIME,
  FILTER_CUTOFF,
  FILTER_RESONANCE,
  FILTER_MOD_AMOUNT,
  LFO_DEPTH,
  LFO_RATE
} synth_parameter_t;

// Synth control messages
typedef enum {
  OCTAVE_SHIFT_INC,
  OCTAVE_SHIFT_DEC,
  EG_SUSTAIN_LEVEL_INC,
  EG_SUSTAIN_LEVEL_DEC,
  OSC_WAVEFORM_DEC,
  OSC_WAVEFORM_INC,
  OSC_2_COARSE_PITCH_INC,
  OSC_2_COARSE_PITCH_DEC,
  OSC_2_FINE_PITCH_INC,
  OSC_2_FINE_PITCH_DEC,
  OSC_1_2_MIX_INC,
  OSC_1_2_MIX_DEC,
  EG_DECAY_TIME_INC,
  EG_DECAY_TIME_DEC,
  FILTER_CUTOFF_INC,
  FILTER_CUTOFF_DEC,
  FILTER_RESONANCE_INC,
  FILTER_RESONANCE_DEC,
  FILTER_MOD_AMOUNT_INC,
  FILTER_MOD_AMOUNT_DEC,
  LFO_DEPTH_INC,
  LFO_DEPTH_DEC,
  LFO_RATE_INC,
  LFO_RATE_DEC,
  ALL_NOTES_OFF,
  PRESET_0,
  PRESET_1,
  PRESET_2,
  PRESET_3,
  PRESET_4,
  PRESET_5,
  PRESET_6,
  PRESET_7,
  PRESET_8,
  PRESET_9,
} control_message_t;

typedef int32_t Q28; // Signed fixed-point number with 28-bit fractional part
typedef int16_t Q14; // Signed fixed-point number with 14-bit fractional part

#define ONE_Q28 ((Q28) (1 << 28)) // 1.0 for Q28 type
#define ONE_Q14 ((Q14) (1 << 14)) // 1.0 for type Q14
#define PI ((float) M_PI) // Pi in float type
#define FCLKSYS (120000000) // system clock frequency (Hz)
#define FS (44100) // sampling frequency (Hz)
#define FA (440.0F) // reference frequency (Hz)

static inline Q28 Osc_phase_to_audio(uint32_t phase, uint8_t pitch);
static inline Q28 Osc_process(uint8_t id,
                              uint16_t full_pitch, Q14 pitch_mod_in);
static inline int32_t mul_s32_s32_h32(int32_t x, int32_t y);

static inline Q28 Filter_process(uint8_t id, Q28 audio_in, Q14 cutoff_mod_in);
static inline Q28 Amp_process(uint8_t id, Q28 audio_in, Q14 gain_in);
static inline Q14 EG_process(uint8_t id, uint8_t gate_in);
static inline Q14 LFO_process(uint8_t id);

bool i2s_timer_callback(repeating_timer_t *timer);

static void pwm_irq_handler();
void PWMA_init(int8_t pwm_gpio_r, int8_t pwm_gpio_l);
static inline void PWMA_process(Q28 audio_in);
static inline Q28 process_voice(uint8_t id);
static void pwm_irq_handler();
void note_toggle(uint8_t key);
void all_notes_off();
void note_on(uint8_t key);
void note_off(uint8_t key);
void startup_chord();
int8_t get_octave_shift();
static void load_factory_preset(uint8_t preset);
void load_preset(Preset_t preset);
void control_message(control_message_t message);
void set_parameter(synth_parameter_t parameter, int8_t value);
void print_status();

#ifdef __cplusplus
}
#endif

#endif