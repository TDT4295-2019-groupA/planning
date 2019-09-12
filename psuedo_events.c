// some planning psuedo code just so that we're on the same page:
// 'freq' is short for frequency

// ========== DEFINITIONS ==================
// shared by FPGA and the microcontroller

#define PI              3.2415926535
#define SAMPLE_RATE     44100
#define N_MIDI_KEYS     128
#define N_MIDI_CHANNELS 16
#define MIDI_A3_INDEX   45 /* see https://www.noterepeat.com/articles/how-to/213-midi-basics-common-terms-explained */
#define MIDI_A3_FREQ    440.0
#define SAMPLE_MAX      0x7FFF
#define N_GENERATORS    8 /* number of supported notes playing simultainiously  (polytones), \
                             subject to change, chisel and microcontroller code \
                             should scale from this single variable */

typedef unsigned int    uint;
typedef unsigned char   NoteIndex;
typedef unsigned char   Velocity; // goes from 0 to 127
typedef signed short    Sample;   // To represent a single audio "frame"
typedef unsigned int    Time;     // measured in n samples

static_assert(sizeof(unsigned char) == 1); // There you go, Rikke
static_assert(sizeof(signed short)  == 2);

typedef enum Instrument { // we can expand this as much as we want
    SQUARE   = 0,
    TRIANGLE = 1,
    SAWTOOTH = 2,
    SINE     = 3,
} Instrument;

typedef struct Envelope { // either preset or controlled by knobs/buttons on the PCB
    Time attack;
    Time decay;
    Sample sustain; // percentage of volume
    Time release;
} Envelope;


// The following two structs represent the two packet types to be transmitted
// from the microcontroller to the FPGA:

typedef struct FPGAGlobalState {
    Velocity   master_volume;
    Envelope   envelope;
    Note       notes        [N_MIDI_CHANNELS];
} FPGAGlobalState;

typedef struct FPGAGeneratorState {
    // this is the state stored on the FPGA, it is also the information sent over SPI from the microcontroller
    // It represents the state of a single sound generator, like if it's on, how
    // long it's been playing for how long, and which midi channel (pitchwheel) it is assigned to
    bool       enabled;           // whether the sound generator should be generating audio or not
    Instrument instrument;        // the index determining which waveform to use
    NoteIndex  note_index    = 0; // to determine pitch/frequency
    uint       channel_index = 0; // to know which pitchwheel to use
    Velocity   velocity; // to know which pitchwheel to use

    // this value is only stored on the FPGA only and is automatically reset to
    // 0 every time the FPGA recieves an event/update for this generator
    Time      note_life     = 0; // The number of samples the note has been playing for, used to determine where in the envelope we are. It gets incremented for each sample generated
} FPGAGeneratorState;



// ========== MICROPROCESSOR BEHAVIOUR ==================




// todo




// ========== FPGA BEHAVIOUR ==================





// The sound generation can be difficult to wrap your head around.
// Here is some equivalent Python code for the generation of a single sample from all the
// generators on the FPGA, using 'sin' as its waveform.   ( the sin(2 * pi * _ ) part )
// >>> sample = sum(
// ...     apply_envelope(
// ...         generator.velocity * sin(
// ...             2 * pi * (
// ...                 note_index_to_freq(generator.note_index)
// ...                 + global_state.pitchwheels[generator.channel_index]
// ...             ) * (generator.note_life / SAMPLERATE)
// ...         ),
// ...         generator.note_life,
// ...         global_state.envelope,
// ...     ) for generator in generators if generator.enabled
// ... )

// TODO: get rid of the remaining floats
//       one solution is to convert everything to ints, and just scale it up by
//       like 1000 or something to get 3 decimals of accuracy.
//       If we use 1024 as the coefficient, then we can bitshift the result of a
//       multiplication to maintain the scale, which is basically free on the fpga.


// The FPGA state
static FPGAGlobalState    fpga_global_state;
static FPGAGeneratorState generators[N_GENERATORS];


// This represents the 'mux', which combines the sound from all the generators
Sample generate_sound_sample() { // is run once per sound sample
    Sample out = 0;

    // this is trivial to do in parallel
    for (size_t generator_index = 0; generator_index < N_GENERATORS; generator_index++) {
        out += generate_sample_from_generator(generator_index);
        generators[generator_index].note_life++; // tick time. IMPORTANT: do this only once per sample!
    }

    return sample * data->master_volume;
}


// this represents a single generator, which there are N_GENERATORS of on the FPGA
Sample generate_sample_from_generator(uint generator_index) {
    FPGAGeneratorState generator = &generators[generator_index]

    if (generator->enabled) {
        float freq = note_index_to_freq(generator->note_index);
        freq *= fpga_global_state.pitchwheels[generator->channel_index]
        uint wavelength = freq_to_wavelength_in_samples(freq);

        Sample sample;
        if (generator->instrument == SQUARE) {
            if (((generator->note_life * 2) / wavelength) % 2 == 1) {
                sample = SAMPLE_MAX;
            } else {
                sample = -SAMPLE_MAX;
            }
        }
        if (generator->instrument == TRIANGLE) {
            sample = 0; // TODO
        }
        if (generator->instrument == SAWTOOTH) {
            sample = 0; // TODO
        }
        if (generator->instrument == SINE) {
            sample = SAMPLE_MAX * sin(2 * PI * generator->note_life / (wavelength));
        }
        return generator->velocity * sample;
    } else{
        return 0;
    }
}


float note_index_to_freq(NoteIndex note_index) {
    // this should be lookup table, only 128 possible values
    return MIDI_A3_FREQ * pow(2, (note_index - MIDI_A3_INDEX) / 12);
}

uint freq_to_wavelength_in_samples(float freq) {
    // todo: convert freq to an integer
    return round(SAMPLE_RATE / freq); // the rounding might need some dithering
}

Sample apply_envelope(Sample sample, Time note_life, Envelope envelope) {
    // TODO
    return sample; // does nothing
}
