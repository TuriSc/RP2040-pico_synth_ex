
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico_synth_ex.h"    // Original code by ISGK Instruments (Ryo Ishigaki), licensed under CC0
#include "notes.h"            // A simple table matching note names to Midi numbers

// You can define your audio output in CMakeLists.txt.
// Under target_compile_definitions, choose either
// USE_AUDIO_PWM=1 or USE_AUDIO_I2S=1 (but not both).

#if USE_AUDIO_PWM
  // Mono output, disabling right PWM output
  // for this example.
  #define AUDIO_PIN_RIGHT    -1
  #define AUDIO_PIN_LEFT     0

#elif USE_AUDIO_I2S
  #include "sound_i2s.h"
  #define I2S_DATA_PIN             28 // -> I2S DIN
  #define I2S_CLOCK_PIN_BASE       26 // -> I2S BCK
  // The third required connection is GPIO 27 -> I2S LRCK (BCK+1)

  static const struct sound_i2s_config sound_config = {
    .pin_sda         = I2S_DATA_PIN,
    .pin_scl         = I2S_CLOCK_PIN_BASE,
    .pin_ws          = I2S_CLOCK_PIN_BASE + 1,
    .sample_rate     = 44100,
    .pio_num         = 0, // 0 for pio0, 1 for pio1
  };

  repeating_timer_t i2s_timer;
#endif

int main() {
  // Set the system clock.
  // If you run it at the default speed, expect an offset
  // with the output frequencies.
  set_sys_clock_khz(FCLKSYS / 1000, true);
  stdio_init_all();

  // Start the synth.
  #if USE_AUDIO_PWM
    // Pass the two output GPIOs as arguments.
    // Left channel must be active.
    // For a mono setup, the right channel can be disabled by passing -1.
    PWMA_init(AUDIO_PIN_RIGHT, AUDIO_PIN_LEFT);
  #elif USE_AUDIO_I2S
    sound_i2s_init(&sound_config);
    sound_i2s_playback_start();
    add_repeating_timer_ms(10, i2s_timer_callback, NULL, &i2s_timer);
  #endif

  sleep_ms(2000);

  // control_message() sends instructions to the synth, triggering
  // pre-defined actions. Here we load a preset:
  control_message(PRESET_5);

  // Play a note
  note_on(68);
  sleep_ms(500);
  // You can stop a note with note_off(uint8_t key), or
  // stop all notes with all_notes_off()
  note_off(68);

  sleep_ms(1000);

  // note_toggle() will stop a note if it is already playing
  note_toggle(70);
  sleep_ms(20);
  note_toggle(70);
  sleep_ms(2000);

  // Create and load a custom preset.
  // See pico_synth_ex.h for a structure definition.
  Preset_t custom_preset = { 0, 0, 12, 2, 9, 44, 3, 59, 42, 32, 10, 9};
  load_preset(custom_preset);
  // Print the current synth configuration
  print_status();

  note_on(72);
  sleep_ms(2000);
  note_off(72);
  sleep_ms(1000);

  // Shift the base octave up
  control_message(OCTAVE_SHIFT_INC);
  note_on(72);
  printf("Current octave: %d\n", get_octave_shift());
  sleep_ms(2000);
  note_off(72);
  sleep_ms(1000);

  control_message(PRESET_9);

  note_on(74);
  // Parameters can be set directly
  set_parameter(FILTER_MOD_AMOUNT, 60);
  sleep_ms(500);
  set_parameter(FILTER_MOD_AMOUNT, 50);
  sleep_ms(500);
  set_parameter(FILTER_MOD_AMOUNT, 40);
  sleep_ms(500);
  set_parameter(FILTER_MOD_AMOUNT, 30);
  sleep_ms(500);
  set_parameter(FILTER_MOD_AMOUNT, 20);
  sleep_ms(500);
  set_parameter(FILTER_MOD_AMOUNT, 10);
  sleep_ms(500);
  note_off(74);

  while (true) {
    // Nothing to do here
    tight_loop_contents();
  }
}
