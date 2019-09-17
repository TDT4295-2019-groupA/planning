// some planning psuedo code just so that we're on the same page:
// 'freq' is short for frequency

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>


// =======================================================
// ================== DEFINITIONS ========================
// =======================================================


// shared by FPGA and the microcontroller

#define PI              3.1415926535
#define SAMPLE_RATE     44100
#define N_MIDI_KEYS     128
#define N_MIDI_CHANNELS 16    /* remember, we ignore the channel dedicated to drums */
#define MIDI_A3_INDEX   45    /* see https://www.noterepeat.com/articles/how-to/213-midi-basics-common-terms-explained */
#define MIDI_A3_INDEX   58    /* see https://www.noterepeat.com/articles/how-to/213-midi-basics-common-terms-explained */
#define MIDI_A3_FREQ    440.0 /* no, i won't listen to your A=432Hz bullshit */
#define VELOCITY_MAX    0x7f  /* 7 bits */
#define SAMPLE_MAX      0x7FFF /* max output from a single generator */
#define N_GENERATORS    16/* number of supported notes playing simultainiously  (polytones), \
                             subject to change, chisel and microcontroller code \
                             should scale from this single variable alone */

typedef unsigned int    uint;
typedef unsigned char   byte;
typedef signed char     sbyte; // signed byte
typedef unsigned short  ushort;
typedef byte            NoteIndex;
typedef byte            ChannelIndex;
typedef byte            Velocity; // goes from 0 to 127
typedef signed short    Sample;   // To represent a single audio "frame", output from each sound generator
typedef signed int      WSample;  // Used to avoid overflow, the final output type from FPGA
typedef unsigned short  Time;     // measured in n/4 samples, meaning x second is represented as x * SAMPLE_RATE / 4
typedef unsigned int    WTime;    // measured in n samples, meaning x second is represented as x * SAMPLE_RATE


typedef enum Instrument { // we can expand this as much as we want
    SQUARE   = 0,
    TRIANGLE = 1,
    SAWTOOTH = 2,
    SINE     = 3,
} Instrument;

typedef struct Envelope { // either preset or controlled by knobs/buttons on the PCB
    Time   attack;
    Time   decay;
    byte   sustain; // 'percentage' of volume to sustain at, from 0 to 0xFF
    Time   release;
} __attribute__((packed)) Envelope;


// The following two structs represent the two packet types to be transmitted
// from the microcontroller to the FPGA:

typedef struct MicrocontrollerGlobalState {
    byte       master_volume;       // from 0 to 0xFF
    Envelope   envelope;
    sbyte      pitchwheels [N_MIDI_CHANNELS];
} __attribute__((packed)) MicrocontrollerGlobalState;


typedef struct MicrocontrollerGeneratorState {
    // this is the state of a single fpga generator as seen on the microprocessor,
    // it is also the packet sent over SPI from the microcontroller to the FPGA
    // It represents the state of a single sound generator
    bool         enabled;           // whether the sound generator should be generating audio or not
    Instrument   instrument;        // the index determining which waveform to use
    NoteIndex    note_index;        // to determine pitch/frequency
    ChannelIndex channel_index;     // to know which pitchwheel to use
    Velocity     velocity;          // to know which pitchwheel to use
} __attribute__((packed)) MicrocontrollerGeneratorState;

// the following two types are the internal state of the FPGA

typedef MicrocontrollerGlobalState FPGAGlobalState;

typedef struct FPGAGeneratorState {
    MicrocontrollerGeneratorState data; // packet from microcontroller stored here

    // The number of samples the note has been playing for, used to determine where in the envelope we are. It gets incremented for each sample generated by the FPGA
    // it is automatically reset to 0 every time the FPGA recieves an generator_state update packet which instructs it to do so
    WTime note_life;
} __attribute__((packed)) FPGAGeneratorState;





// =======================================================
// ========== MICROCONTROLLER BEHAVIOUR ==================
// =======================================================







// microcontroller state

static MicrocontrollerGlobalState    microcontroller_global_generator_state;
static MicrocontrollerGeneratorState microcontroller_generator_states[N_GENERATORS];

// This endpoint is responsible for pushing data over SPI to the FPGA
void microcontroller_send_spi_packet(const byte* data, size_t length); // intentionally left undefined. To be implemented by the simulator

// The following two functions sends update packets to the FPGA
void microcontroller_send_global_state_update() {
    byte data[1 + sizeof(MicrocontrollerGlobalState)];

    data[0] = 1; // global_state update

    memcpy(data+1, &microcontroller_global_generator_state, sizeof(MicrocontrollerGlobalState));

    microcontroller_send_spi_packet((byte*)data, sizeof(data));
}

void microcontroller_send_generator_update(ushort generator_index, bool reset_note_lifetime) {
    // set reset_note_lifetime to true when sending note-on events
    byte data[2 + sizeof(ushort) + sizeof(MicrocontrollerGeneratorState)];

    data[0] = 2; // generator update

    data[1] = (byte) reset_note_lifetime;
    *(ushort*)(&data[2]) = generator_index;
    memcpy(data+2+sizeof(ushort), &microcontroller_generator_states[generator_index], sizeof(MicrocontrollerGeneratorState));

    microcontroller_send_spi_packet(data, sizeof(data));
}

// The following two functions are our input handlers
void microcontroller_handle_button_event(uint button_index) { // called when button is pushed down
    // The GPIO interrupt handler should call this function

    // TODO
}

void microcontroller_handle_midi_event(const byte *data, size_t length) {
    // The UART interrupt handler should call this function when it has recieved a full midi event

    byte status_byte    = data[0];
    byte packet_type    = (status_byte & 0xF0) >> 4;
    byte type_specifier = (status_byte & 0x0F);

    // validate packet:
    for (size_t i = 0; i < length; i++)
        if (!( ((data[i] & 0x80) == 0) || (i == 0) ))
            return; // ignore invalid packets

    // interpret and handle packet:
    switch (packet_type) {
        // see https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
        break; case 0b1000: { // note-off event
            assert(length == 3);
            ChannelIndex channel  = type_specifier;
            NoteIndex    note     = data[1];
            Velocity     velocity = data[2];

            if (channel == 9) return; // ignore drums

            // find the sound generator currenty playing this note (we should perhaps keep an index/lookup)
            uint idx = 0; // sound_generator_index
            while (idx < N_GENERATORS && !(
                microcontroller_generator_states[idx].enabled
                && microcontroller_generator_states[idx].note_index    == note
                && microcontroller_generator_states[idx].channel_index == channel
            )) idx++;
            if (idx >= N_GENERATORS) return; // none found, probably due to the note-on being ignored due to lack of generators

            microcontroller_generator_states[idx].enabled       = false;
            microcontroller_generator_states[idx].note_index    = note;
            microcontroller_generator_states[idx].channel_index = channel;
            microcontroller_generator_states[idx].velocity      = velocity;
            microcontroller_send_generator_update(idx, false);
        }
        break; case 0b1001: { // note-on event
            assert(length == 3);
            ChannelIndex channel  = type_specifier;
            NoteIndex    note     = data[1];
            Velocity     velocity = data[2];

            if (channel == 9) return; // ignore drums

            if (velocity > 0) {
                // find vacant sound generator
                uint idx = 0; // sound_generator_index
                while (idx < N_GENERATORS && microcontroller_generator_states[idx].enabled) idx++;
                if (idx >= N_GENERATORS) return; // out of sound generators, ignore

                microcontroller_generator_states[idx].enabled       = true;
                microcontroller_generator_states[idx].note_index    = note;
                microcontroller_generator_states[idx].channel_index = channel;
                microcontroller_generator_states[idx].velocity      = velocity;
                microcontroller_send_generator_update(idx, true);
            } else { // people suck at following the MIDI standard

                // find the sound generator currenty playing this note (we should perhaps keep an index/lookup)
                uint idx = 0; // sound_generator_index
                while (idx < N_GENERATORS && !(
                    microcontroller_generator_states[idx].enabled
                    && microcontroller_generator_states[idx].note_index    == note
                    && microcontroller_generator_states[idx].channel_index == channel
                )) idx++;
                if (idx >= N_GENERATORS) return; // none found, probably due to the note-on being ignored due to lack of generators

                microcontroller_generator_states[idx].enabled       = false;
                microcontroller_generator_states[idx].note_index    = note;
                microcontroller_generator_states[idx].channel_index = channel;
                microcontroller_generator_states[idx].velocity      = velocity;
                microcontroller_send_generator_update(idx, false);
            }
        }
        break; case 0b1010: /*IGNORE*/ // Polyphonic Key Pressure (Aftertouch) event
        break; case 0b1011: /*IGNORE*/ // Control Change event
        break; case 0b1100: /*IGNORE*/ // Program Chang event
        break; case 0b1101: /*IGNORE*/ // Channel Pressure (After-touch) event
        break; case 0b1110: /*IGNORE*/ // Pitch Bend Change event
        break; case 0b1111: /*IGNORE*/ // System Exclusive event
        break; default: break;         // unknown - ignored
    }
}








// =======================================================
// =================== FPGA BEHAVIOUR ====================
// =======================================================





// The sound generation can be difficult to wrap your head around.
// Here is some equivalent Python code for the generation of a single sample from all the
// generators on the FPGA, using 'sin' as its waveform.   ( the sin(2 * pi * _ ) part )
// >>> sample = sum(
// ...     apply_envelope(
// ...         generator.velocity * sin(
// ...             2 * pi * (
// ...                 fpga_note_index_to_freq(generator.note_index)
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


// The FPGA shared state
static FPGAGlobalState    fpga_global_state;
static FPGAGeneratorState fpga_generators[N_GENERATORS];



// this should be lookup table, only 128 possible input values
float fpga_note_index_to_freq(NoteIndex note_index) {
    return MIDI_A3_FREQ * pow(2.0, (note_index - MIDI_A3_INDEX) / 12.f);
}

uint freq_to_wavelength_in_samples(float freq) {
    // todo: convert freq to an integer
    return round(SAMPLE_RATE / freq); // the rounding might need some dithering
}

Sample fpga_apply_envelope(Sample sample, WTime note_life) {
    fpga_global_state.envelope.attack;
    fpga_global_state.envelope.decay;
    fpga_global_state.envelope.sustain;
    fpga_global_state.envelope.release;
    // TODO
    return sample; // currently does nothing
}


// This represets the FPGA's SPI input handler
void fpga_handle_spi_packet(const byte* data, size_t length) {
    byte packet_type = data[0];

    if (packet_type == 1) { // global_state update
        if (length >= 1 + sizeof(MicrocontrollerGlobalState)) {

            // write each byte into where they belong, this could perhaps be a bit more hardcoded on the FPGA
            for (size_t i = 0; i < sizeof(MicrocontrollerGlobalState); i++) {
                *(((byte*)&fpga_global_state) + i) = *(data + 1 + i);
            }

        }
    }
    else if (packet_type == 2) { // generator_state update
        if (length >= 2 + sizeof(ushort) + sizeof(MicrocontrollerGeneratorState)) {

            bool reset_note_lifetime = (bool)data[1];
            ushort generator_index = *(ushort*)(data+2);

            // write each byte into where they belong, this could perhaps be a bit more hardcoded on the FPGA
            byte* generator_data_ptr = &fpga_generators[generator_index].data;
            for (size_t i = 0; i < sizeof(MicrocontrollerGeneratorState); i++) {
                *(generator_data_ptr + i) = *(data + 2 + sizeof(ushort) + i);
            }

            if (reset_note_lifetime) {
                fpga_generators[generator_index].note_life = 0; // make sure this doesn't conflict with the incrmentation after generating a sample
            }
        }
    }
    // ignore unknown packets
}


// this represents a single generator module, which there are N_GENERATORS of on the FPGA
Sample fpga_generate_sample_from_generator(uint generator_index) {
    const FPGAGeneratorState* generator = &fpga_generators[generator_index];

    if (generator->data.enabled) {
        float freq = fpga_note_index_to_freq(generator->data.note_index);
        //freq *= fpga_global_state.pitchwheels[generator->data.channel_index]; // TODO, account for pitchwheels type change from float to sbyte
        uint wavelength = freq_to_wavelength_in_samples(freq);

        Sample sample;
        // NOTE: perhaps move these generators to separate chisel modules and just wire them in
        if (generator->data.instrument == SQUARE) {
            if (((generator->note_life * 2) / wavelength) % 2 == 1) {
                sample = -SAMPLE_MAX;
            } else {
                sample = SAMPLE_MAX;
            }
        }
        else if (generator->data.instrument == TRIANGLE) {
            sample = 0; // TODO
        }
        else if (generator->data.instrument == SAWTOOTH) {
            sample = ((generator->note_life % wavelength) * 2 - wavelength) * SAMPLE_MAX  / wavelength;
        }
        else if (generator->data.instrument == SINE) {
            // todo: convert float to integer
            // sin(2*pi*x) should be a lookup-table, that ought to suffice
            sample = round(SAMPLE_MAX * sin(2 * PI * generator->note_life / wavelength));
        }

        // the divide at the end should be represented as a bit-shift
        return fpga_apply_envelope(sample, generator->note_life) * generator->data.velocity / VELOCITY_MAX;
    } else {
        return 0;
    }
}


// This represents the 'adder' module, which combines the sound from all the generators
WSample fpga_generate_sound_sample() { // is run once per sound sample
    WSample out = 0;

    // this is trivial to do in parallel
    for (size_t generator_idx = 0; generator_idx < N_GENERATORS; generator_idx++) {
        out += fpga_generate_sample_from_generator(generator_idx);
    }

    // tick time. IMPORTANT: do this only once per sample!
    for (size_t generator_idx = 0; generator_idx < N_GENERATORS; generator_idx++) {
        fpga_generators[generator_idx].note_life++;
    }

    return out * fpga_global_state.master_volume << 6; // 6 bits headroom
}












// =======================================================
// ================== DEFINITIONS ========================
// =======================================================



void do_sanity_check() {
    static_assert(sizeof(byte)    == 1); // There you go, Rikke
    static_assert(sizeof(bool)    == 1);
    static_assert(sizeof(ushort)  == 2);
    static_assert(sizeof(Sample)  == 2); // bitdepth/samplewidth == 16 (2 bytes)
    static_assert(sizeof(uint)    == 4); // 32 bit
    static_assert(sizeof(Instrument) == 4); // 32 bit - todo: make this smaller?
}
