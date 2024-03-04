# RP2040-pico_synth_ex
## Polyphonic synth engine for Raspberry Pi Pico, with envelope, filter and LFO

This repository contains my adaptation of the great little software synthesizer [pico_synth_ex](https://github.com/risgk/pico_synth_ex) by Ryo Ishigaki (ISGK Instruments).

The synth is tetraphonic and boasts two oscillators with descending sawtooth and square waveforms, a customizable filter with resonance control and cutoff modulation, a Decay-Sustain amp envelope, and an LFO for added modulation possibilities.

This version of the synth can output audio via I²S interface, unlike the original program, which only used PWM. A previous version served as the audio engine for my toy keyboard [Picophonica](https://github.com/TuriSc/Picophonica).


### Usage
An example program is included. Please see [example/example.c](/example/example.c).

To specify which audio output to use, enable only one of the two definitions in your CMakeLists.txt
```cmake
target_compile_definitions(${PROJECT_NAME} PRIVATE
        # USE_AUDIO_PWM=1
        USE_AUDIO_I2S=1
    )
```

### A note about PWM audio
The audio quality of PWM output is greatly inferior to I²S audio. It's also very noisy if unfiltered, and for this reason you might want to pair it with a DAC circuit to smooth the signal. There are several designs that will work, but my research led me to the one I used for [Dodepan](https://github.com/TuriSc/Dodepan), which also provides some noise filtering and DC offset removal. 


### Credits and license
This library is released under a MIT-0 license (see [LICENSE](/LICENSE)), with the exception of the I²S driver, based on the work of [Ricardo Massaro](https://github.com/moefh/), released under a MIT License (see [sound_i2s/LICENSE](/sound_i2s/LICENSE) for details).

Original code: [pico_synth_ex](https://github.com/risgk/pico_synth_ex) by Ryo Ishigaki (ISGK Instruments). Released in the public domain.


### Version history
- 2024-03-04 - First release
