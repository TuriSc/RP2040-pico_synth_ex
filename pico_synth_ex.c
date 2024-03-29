/* pico_synth_ex
 * Original author: ISGK Instruments (Ryo Ishigaki)
 * Original version: v0.1.0 (2021-09-02)
 * https://github.com/risgk/pico_synth_ex   https://risgk.github.io
 * Licensed under a CC0 license
 *
 * C module version by Turi Scandurra
 * version 1.0.0
 * Licensed under a MIT-0 license
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/float.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "pico_synth_ex.h"
#include "pico_synth_ex_presets.h"
#include "pico_synth_ex_tables.h"
#include "sound_i2s.h"

#ifdef __cplusplus
extern "C" {
#endif

//////// Oscillator group //////////////////////////////
static volatile uint8_t Osc_waveform = 0; // waveform setting value
static volatile int8_t Osc_2_coarse_pitch = +0; // oscillator 2 coarse pitch setting value
static volatile int8_t Osc_2_fine_pitch = +4; // oscillator 2 fine pitch setting value
static volatile uint8_t Osc_1_2_mix = 16; // oscillator mix setting

static inline Q28 Osc_phase_to_audio(uint32_t phase, uint8_t pitch) {
  Q14* wave_table = Osc_wave_tables[Osc_waveform][(pitch + 3) >> 2];
  uint16_t curr_index = phase >> 23;
  uint16_t next_index = (curr_index + 1) & 0x000001FF;
  Q14 curr_sample = wave_table[curr_index];
  Q14 next_sample = wave_table[next_index];
  Q14 next_weight = (phase >> 9) & 0x3FFF;
  return (curr_sample << 14) + ((next_sample - curr_sample) * next_weight);
}

static inline Q28 Osc_process(uint8_t id,
                              uint16_t full_pitch, Q14 pitch_mod_in) {
  static uint32_t phase_1[4]; // Oscillator 1 phase
  int32_t full_pitch_1 = full_pitch + ((256 * pitch_mod_in) >> 14);
  full_pitch_1 += (full_pitch_1 < 0)          * (0 - full_pitch_1);
  full_pitch_1 -= (full_pitch_1 > (120 << 8)) * (full_pitch_1 - (120 << 8));
  uint8_t pitch_1 = (full_pitch_1 + 128) >> 8;
  uint8_t tune_1  = (full_pitch_1 + 128) & 0xFF;
  uint32_t freq_1 = Osc_freq_table[pitch_1];
  phase_1[id] += freq_1 + ((id - 1) << 8); // Shift by voice
  phase_1[id] += ((int32_t) (freq_1 >> 8) * Osc_tune_table[tune_1]) >> 6;

  static uint32_t phase_2[4]; // Oscillator 2 phase
  int32_t full_pitch_2 =
      full_pitch_1 + (Osc_2_coarse_pitch << 8) + (Osc_2_fine_pitch << 2);
  full_pitch_2 += (full_pitch_2 < 0)          * (0 - full_pitch_2);
  full_pitch_2 -= (full_pitch_2 > (120 << 8)) * (full_pitch_2 - (120 << 8));
  uint8_t pitch_2 = (full_pitch_2 + 128) >> 8;
  uint8_t tune_2  = (full_pitch_2 + 128) & 0xFF;
  uint32_t freq_2 = Osc_freq_table[pitch_2];
  phase_2[id] += freq_2 + ((id - 1) << 8); // Shift by voice
  phase_2[id] += ((int32_t) (freq_2 >> 8) * Osc_tune_table[tune_2]) >> 6;

  // TODO: I want to make wave_table switching smoother (Is it better to switch at the beginning of the cycle?)
  return ((Osc_phase_to_audio(phase_1[id], pitch_1) >> 14) *
                              Osc_mix_table[Osc_1_2_mix - 0]) +
         ((Osc_phase_to_audio(phase_2[id], pitch_2) >> 14) *
                              Osc_mix_table[64 - Osc_1_2_mix]);
}

//////// filter ///////////////////////////////////
static volatile uint8_t Filter_cutoff = 60; // Cutoff setting value
static volatile uint8_t Filter_resonance = 3; // Resonance setting value
static volatile int8_t Filter_mod_amount = +60; // Cutoff modulation amount setting value

static inline int32_t mul_s32_s32_h32(int32_t x, int32_t y) {
  // Higher 32 bits of signed 32-bit multiplication result
  int32_t x1 = x >> 16; uint32_t x0 = x & 0xFFFF;
  int32_t y1 = y >> 16; uint32_t y0 = y & 0xFFFF;
  int32_t x0_y1 = x0 * y1;
  int32_t z = ((x0 * y0) >> 16) + (x1 * y0) + (x0_y1 & 0xFFFF);
  return (z >> 16) + (x0_y1 >> 16) + (x1 * y1);
}

static inline Q28 Filter_process(uint8_t id, Q28 audio_in, Q14 cutoff_mod_in) {
  static uint16_t curr_cutoff[4]; // Cutoff current value
  int32_t targ_cutoff = Filter_cutoff << 2; // Cutoff target value
  targ_cutoff += (Filter_mod_amount * cutoff_mod_in) >> (14 - 2);
  targ_cutoff += (targ_cutoff < 0)   * (0 - targ_cutoff);
  targ_cutoff -= (targ_cutoff > 480) * (targ_cutoff - 480);
  curr_cutoff[id] += (curr_cutoff[id] < targ_cutoff);
  curr_cutoff[id] -= (curr_cutoff[id] > targ_cutoff);
  struct FILTER_COEFS* coefs_ptr =
      &Filter_coefs_table[Filter_resonance][curr_cutoff[id]];

  static Q28 x1[4], x2[4], y1[4], y2[4];
  Q28 x0 = audio_in;
  Q28 x3 = x0 + (x1[id] << 1) + x2[id];
  Q28 y0 = mul_s32_s32_h32(coefs_ptr->b0_a0, x3)     << 4;
  y0    -= mul_s32_s32_h32(coefs_ptr->a1_a0, y1[id]) << 4;
  y0    -= mul_s32_s32_h32(coefs_ptr->a2_a0, y2[id]) << 4;
  x2[id] = x1[id]; y2[id] = y1[id]; x1[id] = x0; y1[id] = y0;
  return y0;
}

//////// Amplifier //////////////////////////////////
static inline Q28 Amp_process(uint8_t id, Q28 audio_in, Q14 gain_in) {
  return (audio_in >> 14) * gain_in; // Simplify calculation
}

//////// EG (Envelope Generator) /////////////////
static uint32_t EG_exp_table[65]; // Exponential table

static volatile uint8_t EG_decay_time = 40; // Decay time setting value
static volatile uint8_t EG_sustain_level = 0; // Sustain level setting value

static inline Q14 EG_process(uint8_t id, uint8_t gate_in) {
  static int32_t curr_level[4]; // EG output level current value
  static uint8_t curr_gate[4]; // gate input level current value
  static uint8_t curr_attack_phase[4]; // current attack phase

  curr_attack_phase[id] |= (curr_gate[id] == 0) & gate_in;
  curr_attack_phase[id] &= (curr_level[id] < (1 << 24)) & gate_in;
  curr_gate[id]          =  gate_in;

  if (curr_attack_phase[id]) {
    int32_t attack_targ_level = (1 << 24) + (1 << 23);
    curr_level[id] += ((attack_targ_level - curr_level[id]) >> 5);
  } else {
    static uint32_t decay_counter[4]; // Decay counter
    ++decay_counter[id];
    decay_counter[id] =
        (decay_counter[id] < EG_exp_table[EG_decay_time]) * decay_counter[id];
    int32_t decay_targ_level = (EG_sustain_level << 18) * curr_gate[id];
    int32_t to_decay = (curr_level[id] > decay_targ_level) &
                       (decay_counter[id] == 0);
    curr_level[id] += to_decay *
                      ((decay_targ_level - curr_level[id]) >> 5);
  }

  return curr_level[id] >> 10;
}

//////// Low Frequency Oscillator (LFO) /////////
static volatile uint8_t LFO_depth = 16; // Depth setting value
static volatile uint8_t LFO_rate = 48; // Speed ​​setting value

static inline Q14 LFO_process(uint8_t id) {
  static uint32_t phase[4]; // Phase
  phase[id] += LFO_freq_table[LFO_rate] + ((id - 1) << 8); // Shift by voice

  // Generate triangle wave
  uint16_t phase_h16 = phase[id] >> 16;
  uint16_t out = phase_h16;
  out += (phase_h16 >= 32768) * (65536 - (phase_h16 << 1));
  return ((out - 16384) * LFO_depth) >> 7;
}

//////// I2S Audio output ////////////
bool i2s_timer_callback(repeating_timer_t *timer) {
  static int16_t *last_buffer;
  int16_t *buffer = sound_i2s_get_next_buffer();
  if (buffer == NULL) return true;
  if (buffer != last_buffer) {
    last_buffer = buffer;
    Q28 voice_out[4];
    for (int i = 0; i < SOUND_I2S_BUFFER_NUM_SAMPLES; i++) {
      voice_out[0] = process_voice(0);
      voice_out[1] = process_voice(1);
      voice_out[2] = process_voice(2);
      voice_out[3] = process_voice(3);
      Q28 mix = ((voice_out[0] + voice_out[1] +
                        voice_out[2] + voice_out[3]) >> 2);
      uint16_t level = (mix >> 14);

      // Copy to I2S buffer
      *buffer++ = level;
      *buffer++ = level;
    }
  }
  return true;
}


//////// PWM audio output block ///////////////////////
static bool use_pwm;
int8_t PWMA_R_GPIO = -1;
int8_t PWMA_L_GPIO = -1;
static int8_t PWMA_R_SLICE;
static int8_t PWMA_L_SLICE;
static int8_t PWMA_R_CHAN;
static int8_t PWMA_L_CHAN;

#define PWMA_CYCLE (FCLKSYS / FS) // PWM cycle

void PWMA_init(int8_t pwm_gpio_r, int8_t pwm_gpio_l) {
  use_pwm = true;
  PWMA_R_GPIO = pwm_gpio_r;
  PWMA_L_GPIO = pwm_gpio_l;
  if(PWMA_R_GPIO > -1) PWMA_R_SLICE = pwm_gpio_to_slice_num(PWMA_R_GPIO);
  if(PWMA_R_GPIO > -1) PWMA_R_CHAN = pwm_gpio_to_channel(PWMA_R_GPIO);
  if(PWMA_L_GPIO > -1) PWMA_L_SLICE = pwm_gpio_to_slice_num(PWMA_L_GPIO);
  if(PWMA_L_GPIO > -1) PWMA_L_CHAN = pwm_gpio_to_channel(PWMA_L_GPIO);
  if(PWMA_R_GPIO > -1) gpio_set_function(PWMA_R_GPIO, GPIO_FUNC_PWM);
  if(PWMA_L_GPIO > -1) gpio_set_function(PWMA_L_GPIO, GPIO_FUNC_PWM);
  irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_irq_handler);
  irq_set_enabled(PWM_IRQ_WRAP, true);
  pwm_set_irq_enabled(PWMA_L_SLICE, true); // Left channel must be active
  if(PWMA_R_GPIO > -1) pwm_set_wrap(PWMA_R_SLICE, PWMA_CYCLE - 1);
  if(PWMA_L_GPIO > -1) pwm_set_wrap(PWMA_L_SLICE, PWMA_CYCLE - 1);
  if(PWMA_R_GPIO > -1) pwm_set_chan_level(PWMA_R_SLICE, PWMA_R_CHAN, PWMA_CYCLE / 2);
  if(PWMA_L_GPIO > -1) pwm_set_chan_level(PWMA_L_SLICE, PWMA_L_CHAN, PWMA_CYCLE / 2);
  if(PWMA_R_GPIO > -1) pwm_set_enabled(PWMA_R_SLICE, true);
  if(PWMA_L_GPIO > -1) pwm_set_enabled(PWMA_L_SLICE, true);
}

static inline void PWMA_process(Q28 audio_in) {
  int32_t level_int32 = (audio_in >> 18) + (PWMA_CYCLE / 2);
  uint16_t level = (level_int32 > 0) * level_int32;
  if(PWMA_R_GPIO > -1) pwm_set_chan_level(PWMA_R_SLICE, PWMA_R_CHAN, level);
  if(PWMA_L_GPIO > -1) pwm_set_chan_level(PWMA_L_SLICE, PWMA_L_CHAN, level);
}

//////// Interrupt handler and main functions ////////////
static volatile uint16_t start_time = 0; // start time
static volatile uint16_t max_start_time = 0; // max start time
static volatile uint16_t proc_time = 0; // processing time
static volatile uint16_t max_proc_time = 0; // maximum processing time

static volatile uint8_t gate_voice[4]; // gate control value (per voice)
static volatile uint8_t pitch_voice[4]; // pitch control value (per voice)
static volatile int8_t Octave_shift; // key octave shift amount

static inline Q28 process_voice(uint8_t id) {
  Q14 lfo_out    = LFO_process(id);
  Q14 eg_out     = EG_process(id, gate_voice[id]);
  Q28 osc_out    = Osc_process(id, pitch_voice[id] << 8, lfo_out);
  Q28 filter_out = Filter_process(id, osc_out, eg_out);
  Q28 amp_out    = Amp_process(id, filter_out, eg_out);
  return amp_out;
}

static void pwm_irq_handler() {
  pwm_clear_irq(PWMA_L_SLICE);
  start_time = pwm_get_counter(PWMA_L_SLICE);

  Q28 voice_out[4];
  voice_out[0] = process_voice(0);
  voice_out[1] = process_voice(1);
  voice_out[2] = process_voice(2);
  voice_out[3] = process_voice(3);
  PWMA_process((voice_out[0] + voice_out[1] +
                voice_out[2] + voice_out[3]) >> 2);

  uint16_t end_time = pwm_get_counter(PWMA_L_SLICE);
  proc_time = end_time - start_time; // simplify calculation
  max_start_time +=
      (start_time > max_start_time) * (start_time - max_start_time);
  max_proc_time +=
      (proc_time > max_proc_time) * (proc_time - max_proc_time);
}

void note_toggle(uint8_t key) {
  uint8_t pitch = key + (Octave_shift * 12);
  if      (pitch_voice[0] == pitch) { gate_voice[0] = (gate_voice[0] == 0); }
  else if (pitch_voice[1] == pitch) { gate_voice[1] = (gate_voice[1] == 0); }
  else if (pitch_voice[2] == pitch) { gate_voice[2] = (gate_voice[2] == 0); }
  else if (pitch_voice[3] == pitch) { gate_voice[3] = (gate_voice[3] == 0); }
  else if (gate_voice[0] == 0) { pitch_voice[0] = pitch; gate_voice[0] = 1; }
  else if (gate_voice[1] == 0) { pitch_voice[1] = pitch; gate_voice[1] = 1; }
  else if (gate_voice[2] == 0) { pitch_voice[2] = pitch; gate_voice[2] = 1; }
  else                         { pitch_voice[3] = pitch; gate_voice[3] = 1; }
}

void note_on(uint8_t key) {
  uint8_t pitch = key + (Octave_shift * 12);
  static uint8_t current_voice;

  pitch_voice[current_voice] = pitch;
  gate_voice[current_voice] = 1;
  current_voice = (++current_voice % 4);
}

void note_off(uint8_t key) {
  uint8_t pitch = key + (Octave_shift * 12);
  for (uint8_t id = 0; id < 4; ++id) {
    if (pitch_voice[id] == pitch) { gate_voice[id] = 0; }
  }
}

void all_notes_off() {
  for (uint8_t id = 0; id < 4; ++id) { gate_voice[id] = 0; }
}

void startup_chord() {
  for (uint8_t id = 0; id < 4; ++id) { pitch_voice[id] = 60; }
  note_on(60); note_on(64); note_on(67); note_on(71);
}

int8_t get_octave_shift() {
  return Octave_shift;
}

static void load_factory_preset(uint8_t preset) {
  Octave_shift       = presets[preset].Octave_shift;
  Osc_waveform       = presets[preset].Osc_waveform;
  Filter_cutoff      = presets[preset].Filter_cutoff;
  Filter_resonance   = presets[preset].Filter_resonance;
  Filter_mod_amount  = presets[preset].Filter_mod_amount;
  EG_decay_time      = presets[preset].EG_decay_time;
  EG_sustain_level   = presets[preset].EG_sustain_level;
  Osc_2_coarse_pitch = presets[preset].Osc_2_coarse_pitch;
  Osc_2_fine_pitch   = presets[preset].Osc_2_fine_pitch;
  Osc_1_2_mix        = presets[preset].Osc_1_2_mix;
  LFO_depth          = presets[preset].LFO_depth;
  LFO_rate           = presets[preset].LFO_rate;
}

void load_preset(Preset_t preset) {
  Octave_shift       = preset.Octave_shift;
  Osc_waveform       = preset.Osc_waveform;
  Filter_cutoff      = preset.Filter_cutoff;
  Filter_resonance   = preset.Filter_resonance;
  Filter_mod_amount  = preset.Filter_mod_amount;
  EG_decay_time      = preset.EG_decay_time;
  EG_sustain_level   = preset.EG_sustain_level;
  Osc_2_coarse_pitch = preset.Osc_2_coarse_pitch;
  Osc_2_fine_pitch   = preset.Osc_2_fine_pitch;
  Osc_1_2_mix        = preset.Osc_1_2_mix;
  LFO_depth          = preset.LFO_depth;
  LFO_rate           = preset.LFO_rate;
}

void control_message(control_message_t message) {
  switch(message){
    case OCTAVE_SHIFT_DEC:        if (Octave_shift       > -5)  { --Octave_shift;       } break;
    case OCTAVE_SHIFT_INC:        if (Octave_shift       < +4)  { ++Octave_shift;       } break;
    case FILTER_CUTOFF_DEC:       if (Filter_cutoff      > 0)   { --Filter_cutoff;      } break;
    case FILTER_CUTOFF_INC:       if (Filter_cutoff      < 120) { ++Filter_cutoff;      } break;
    case FILTER_RESONANCE_DEC:    if (Filter_resonance   > 0)   { --Filter_resonance;   } break;
    case FILTER_RESONANCE_INC:    if (Filter_resonance   < 5)   { ++Filter_resonance;   } break;
    case FILTER_MOD_AMOUNT_DEC:   if (Filter_mod_amount  > +0)  { --Filter_mod_amount;  } break;
    case FILTER_MOD_AMOUNT_INC:   if (Filter_mod_amount  < +60) { ++Filter_mod_amount;  } break;
    case EG_DECAY_TIME_DEC:       if (EG_decay_time      > 0)   { --EG_decay_time;      } break;
    case EG_DECAY_TIME_INC:       if (EG_decay_time      < 64)  { ++EG_decay_time;      } break;
    case EG_SUSTAIN_LEVEL_DEC:    if (EG_sustain_level   > 0)   { --EG_sustain_level;   } break;
    case EG_SUSTAIN_LEVEL_INC:    if (EG_sustain_level   < 64)  { ++EG_sustain_level;   } break;
    case OSC_WAVEFORM_DEC:        if (Osc_waveform       > 0)   { --Osc_waveform;       } break;
    case OSC_WAVEFORM_INC:        if (Osc_waveform       < 1)   { ++Osc_waveform;       } break;
    case OSC_2_COARSE_PITCH_DEC:  if (Osc_2_coarse_pitch > +0)  { --Osc_2_coarse_pitch; } break;
    case OSC_2_COARSE_PITCH_INC:  if (Osc_2_coarse_pitch < +24) { ++Osc_2_coarse_pitch; } break;
    case OSC_2_FINE_PITCH_DEC:    if (Osc_2_fine_pitch   > +0)  { --Osc_2_fine_pitch;   } break;
    case OSC_2_FINE_PITCH_INC:    if (Osc_2_fine_pitch   < +32) { ++Osc_2_fine_pitch;   } break;
    case OSC_1_2_MIX_DEC:         if (Osc_1_2_mix        > 0)   { --Osc_1_2_mix;        } break;
    case OSC_1_2_MIX_INC:         if (Osc_1_2_mix        < 64)  { ++Osc_1_2_mix;        } break;
    case LFO_DEPTH_DEC:           if (LFO_depth          > 0)   { --LFO_depth;          } break;
    case LFO_DEPTH_INC:           if (LFO_depth          < 64)  { ++LFO_depth;          } break;
    case LFO_RATE_DEC:            if (LFO_rate           > 0)   { --LFO_rate;           } break;
    case LFO_RATE_INC:            if (LFO_rate           < 64)  { ++LFO_rate;           } break;
    case PRESET_0:                                      load_factory_preset(0);           break;
    case PRESET_1:                                      load_factory_preset(1);           break;
    case PRESET_2:                                      load_factory_preset(2);           break;
    case PRESET_3:                                      load_factory_preset(3);           break;
    case PRESET_4:                                      load_factory_preset(4);           break;
    case PRESET_5:                                      load_factory_preset(5);           break;
    case PRESET_6:                                      load_factory_preset(6);           break;
    case PRESET_7:                                      load_factory_preset(7);           break;
    case PRESET_8:                                      load_factory_preset(8);           break;
    case PRESET_9:                                      load_factory_preset(9);           break;
    case ALL_NOTES_OFF:                                 all_notes_off();                  break;
  }
}

void set_parameter(synth_parameter_t parameter, int8_t value){
  switch(parameter){
    case OCTAVE_SHIFT:       if (value >= -5 && value <= +4)  { Octave_shift = value;       } break;    
    case OSC_WAVEFORM:       if (value >=  0 && value <= 1)   { Osc_waveform = value;       } break;
    case OSC_2_COARSE_PITCH: if (value >=  0 && value <= 24)  { Osc_2_coarse_pitch = value; } break;
    case OSC_2_FINE_PITCH:   if (value >=  0 && value <= 32)  { Osc_2_fine_pitch = value;   } break;
    case OSC_1_2_MIX:        if (value >=  0 && value <= 64)  { Osc_1_2_mix = value;        } break;
    case EG_SUSTAIN_LEVEL:   if (value >=  0 && value <= 64)  { EG_sustain_level = value;   } break;
    case EG_DECAY_TIME:      if (value >=  0 && value <= 64)  { EG_decay_time = value;      } break;
    case FILTER_CUTOFF:      if (value >=  0 && value <= 120) { Filter_cutoff = value;      } break;
    case FILTER_RESONANCE:   if (value >=  0 && value <= 5)   { Filter_resonance = value;   } break;
    case FILTER_MOD_AMOUNT:  if (value >=  0 && value <= 60)  { Filter_mod_amount = value;  } break;
    case LFO_DEPTH:          if (value >=  0 && value <= 64)  { LFO_depth = value;          } break;
    case LFO_RATE:           if (value >=  0 && value <= 64)  { LFO_rate = value;           } break;
  }
}

void print_status(){
  printf("Pitch             : [ %3hhu, %3hhu, %3hhu, %3hhu ]\n",
      pitch_voice[0], pitch_voice[1], pitch_voice[2], pitch_voice[3]);
  printf("Gate              : [ %3hhu, %3hhu, %3hhu, %3hhu ]\n",
      gate_voice[0], gate_voice[1], gate_voice[2], gate_voice[3]);
  printf("Octave Shift      : %+3hd\n",       Octave_shift);
  printf("Osc Waveform      : %3hhu\n",       Osc_waveform);
  printf("Osc 2 Coarse Pitch: %+3hd\n",       Osc_2_coarse_pitch);
  printf("Osc 2 Fine Pitch  : %+3hd\n",       Osc_2_fine_pitch);
  printf("Osc 1/2 Mix       : %3hhu\n",       Osc_1_2_mix);
  printf("Filter Cutoff     : %3hhu\n",       Filter_cutoff);
  printf("Filter Resonance  : %3hhu\n",       Filter_resonance);
  printf("Filter EG Amount  : %+3hd\n",       Filter_mod_amount);
  printf("EG Decay Time     : %3hhu\n",       EG_decay_time);
  printf("EG Sustain Level  : %3hhu\n",       EG_sustain_level);
  printf("LFO Depth         : %3hhu\n",       LFO_depth);
  printf("LFO Rate          : %3hhu\n",       LFO_rate);
  printf("Start Time        : %4hu/%4hu\n",   start_time, max_start_time);
  printf("Processing Time   : %4hu/%4hu\n\n", proc_time, max_proc_time);
}

#ifdef __cplusplus
}
#endif