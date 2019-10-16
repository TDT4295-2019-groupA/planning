// some planning psuedo code just so that we're on the same page:
// 'freq' is short for frequency

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// you know we're in for a good time right now
#define when      if
#define elsewhen  else if
#define otherwise else

// =======================================================
// ================== DEFINITIONS ========================
// =======================================================


// shared by FPGA and the microcontroller

#define PI              3.1415926535
#define SAMPLE_RATE     44100
#define FREQ_SHIFT      12    /* scaling factor (bit shifts) when dealing with frequencies */
#define NOTE_LIFE_COEFF 10    /* scaling factor when dealing with note_life*/
#define N_MIDI_KEYS     128
#define N_MIDI_CHANNELS 16    /* remember, we ignore the channel dedicated to drums */
//#define MIDI_A3_INDEX   45    /* see https://www.noterepeat.com/articles/how-to/213-midi-basics-common-terms-explained */
#define MIDI_A3_INDEX   58    /* shifting everything down an octave sounds better */
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
typedef unsigned short  Time;     // measured in n samples, meaning x second is represented as x * SAMPLE_RATE
typedef unsigned int    WTime;    // measured in n*NOTE_LIFE_COEFF samples, meaning x second is represented as x * SAMPLE_RATE * NOTE_LIFE_COEFF


typedef enum Instrument { // we can expand this as much as we want
    SQUARE   = 0,
    TRIANGLE = 1,
    SAWTOOTH = 2,
    SINE     = 3,
} Instrument;

typedef struct Envelope { // either preset or controlled by knobs/buttons on the PCB
    Time   attack;
    Time   decay;
    byte   sustain; // 'perbyteage' of volume to sustain at, ranges from 0 to 0xFF
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
    // the following is registeres inside each generator:

    // The number of samples the note has been playing for, used to determine where in the envelope we are. It gets incremented for each sample generated by the FPGA
    // it is automatically reset to 0 every time the FPGA recieves an generator_state update packet which instructs it to do so
    WTime note_life;
    WTime wavelength_pos;

    // used to know where the release section if the envelope begins at
    ushort last_active_envelope_effect;
} __attribute__((packed)) FPGAGeneratorState;





// =======================================================
// ========== MICROCONTROLLER BEHAVIOUR ==================
// =======================================================






// microcontroller pin definition stuff

const uint BUTTON_INDEX_TO_PIN_MAP[] = { // change this as needed on the actual microcontroller
    0, //  note button 1
    1, //  note button 2
    2, //  note button 3
    3, //  note button 4
    4, //  note button 5
    5, //  note button 6
    6, //  note button 7
    7, //  note button 8
    8, //  note button 9
    9, //  note button 10
    10, // note button 11
    11, // note button 12
    12, // control button 1
    13, // control button 2
    14, // control button 3
    15, // control button 4
};
const uint BUTTON_COUNT = sizeof(BUTTON_INDEX_TO_PIN_MAP) / sizeof(uint);


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

    *(ushort*)(&data[1]) = generator_index;
    data[3] = (byte) reset_note_lifetime;
    memcpy(data+2+sizeof(ushort), &microcontroller_generator_states[generator_index], sizeof(MicrocontrollerGeneratorState));

    microcontroller_send_spi_packet(data, sizeof(data));
}

// this is just a helper function:

int microcontroller_find_vacant_generator_channel() {
    // we need to assign notes to generators in a round-robin fashion to avoid
    // overruling the generators which are still generating the release sound too much
    static int starting_pos = 0;
    int pos = starting_pos;
    while (microcontroller_generator_states[pos].enabled) {
        pos++;
        if (pos >= N_GENERATORS) pos = 0;
        if (pos == starting_pos) return -1;
    }
    starting_pos = (pos+1) % N_GENERATORS;
    return pos;
}

// The following three functions are our input handlers:

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
        break; case 0b1000: { note_off_event:

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
            if (velocity != 0) { // to not kill of the release
                microcontroller_generator_states[idx].velocity      = velocity;
            }
            microcontroller_send_generator_update(idx, true);
        }
        break; case 0b1001: { // note-on event
            assert(length == 3);
            ChannelIndex channel  = type_specifier;
            NoteIndex    note     = data[1];
            Velocity     velocity = data[2];

            if (channel == 9) return; // ignore drums
            if (velocity == 0) goto note_off_event; // people suck at following the midi standard

            // find vacant sound generator
            int idx = microcontroller_find_vacant_generator_channel();
            if (idx == -1) return; // out of generators

            // TODO: set instrument, probably just have it as a global variable
            // or in a array like the pitchwheels
            microcontroller_generator_states[idx].enabled       = true;
            microcontroller_generator_states[idx].note_index    = note;
            microcontroller_generator_states[idx].channel_index = channel;
            microcontroller_generator_states[idx].velocity      = velocity;
            microcontroller_send_generator_update(idx, true);

        }
        break; case 0b1010: /*IGNORE*/ // Polyphonic Key Pressure (Aftertouch) event
        break; case 0b1011: /*IGNORE*/ // Control Change event
        break; case 0b1100: /*IGNORE*/ // Program Change event
        break; case 0b1101: /*IGNORE*/ // Channel Pressure (After-touch) event
        break; case 0b1110: {          // Pitch Bend Change event
            assert(length == 3);
            ChannelIndex channel  = type_specifier;
            short decoded = ((data[2]&0x7F) << 7 | (data[1]&0x7F)) - 0x2000;
            sbyte pitchwheel = decoded >> 6;

            microcontroller_global_generator_state.pitchwheels[channel] = pitchwheel;
            microcontroller_send_global_state_update();
        }
        break; case 0b1111: /*IGNORE*/ // System Exclusive event
        break; default: break;         // unknown - ignored
    }
}

bool microcontroller_poll_pcb_button_state(uint button_id); // polls the button index for its state, returns true if it is currently held down

void microcontroller_handle_button_event() { // called when any button is either pushed down or released
    // TODO: how/where do we handle debounce?
    // The IO interrupt handler should call this function

    for (size_t button_index = 0; button_index < BUTTON_COUNT; button_index++) {
        bool button_pushed_down = microcontroller_poll_pcb_button_state(BUTTON_INDEX_TO_PIN_MAP[button_index]);
        switch (button_index) {
            break; case 0: { // note button 1
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x30\x7f"  // C4 note on,  midi channel 0
                    : (const byte*)"\x80\x30\x00", // C4 note off, midi channel 0
                    3);
            }
            break; case 1: { // note button 2
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x31\x7f"  // C#4 note on,  midi channel 0
                    : (const byte*)"\x80\x31\x00", // C#4 note off, midi channel 0
                    3);
            }
            break; case 2: { // note button 3
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x32\x7f"  // D4 note on,  midi channel 0
                    : (const byte*)"\x80\x32\x00", // D4 note off, midi channel 0
                    3);
            }
            break; case 3: { // note button 4
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x33\x7f"  // D#4 note on,  midi channel 0
                    : (const byte*)"\x80\x33\x00", // D#4 note off, midi channel 0
                    3);
            }
            break; case 4: { // note button 5
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x34\x7f"  // E4 note on,  midi channel 0
                    : (const byte*)"\x80\x34\x00", // E4 note off, midi channel 0
                    3);
            }
            break; case 5: { // note button 6
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x35\x7f"  // F4 note on,  midi channel 0
                    : (const byte*)"\x80\x35\x00", // F4 note off, midi channel 0
                    3);
            }
            break; case 6: { // note button 7
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x36\x7f"  // F#4 note on,  midi channel 0
                    : (const byte*)"\x80\x36\x00", // F#4 note off, midi channel 0
                    3);
            }
            break; case 7: { // note button 8
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x37\x7f"  // G4 note on,  midi channel 0
                    : (const byte*)"\x80\x37\x00", // G4 note off, midi channel 0
                    3);
            }
            break; case 8: { // note button 9
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x38\x7f"  // G#4 note on,  midi channel 0
                    : (const byte*)"\x80\x38\x00", // G#4 note off, midi channel 0
                    3);
            }
            break; case 9: { // note button 10
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x39\x7f"  // A4 note on,  midi channel 0
                    : (const byte*)"\x80\x39\x00", // A4 note off, midi channel 0
                    3);
            }
            break; case 10: { // note button 11
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x3a\x7f"  // A#4 note on,  midi channel 0
                    : (const byte*)"\x80\x3a\x00", // A#4 note off, midi channel 0
                    3);
            }
            break; case 11: { // note button 12
                microcontroller_handle_midi_event((button_pushed_down)
                    ? (const byte*)"\x90\x3b\x7f"  // B4 note on,  midi channel 0
                    : (const byte*)"\x80\x3b\x00", // B4 note off, midi channel 0
                    3);
            }
            break; case 12: { // control button 1
                /* CODE TODO */
            }
            break; case 13: { // control button 2
                /* CODE TODO */
            }
            break; case 14: { // control button 3
                /* CODE TODO */
            }
            break; case 15: { // control button 4
                /* CODE TODO */
            }
            break; default: break; // ignore rest
        }
    }

    // TODO
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
uint fpga_note_index_to_freq(NoteIndex note_index) {
    return round((1<<FREQ_SHIFT) * MIDI_A3_FREQ * pow(2.0, (note_index - MIDI_A3_INDEX) / 12.f));
}

uint freq_to_wavelength_in_samples(uint freq) {
    return (SAMPLE_RATE << FREQ_SHIFT) * NOTE_LIFE_COEFF / freq;
}

Sample fpga_apply_envelope(Sample sample, FPGAGeneratorState* generator) {
    uint life = generator->note_life / NOTE_LIFE_COEFF;
    Envelope env = fpga_global_state.envelope; // just a shorthand reference, no register intended in chisel
    ushort scaled_sustain = (env.sustain << 8) | env.sustain; // scale from 8 to 16 bits

    ushort envelope_effect; // this is a Wire
    when (!generator->data.enabled) { // release phase
        when (life < env.release) { // assuming note_life was reset on note_off
            envelope_effect = generator->last_active_envelope_effect * (env.release - life) / env.release;
        } otherwise {
            envelope_effect = 0;
        }
    } elsewhen (life < env.attack) { // attack phase
        envelope_effect = 0xffff * life / env.attack;
    } elsewhen (life < env.attack + env.decay) { // decay phase
        // x between [0, r), output should go linearly from a to b
        // x*(b-a)/r + a
        //  is equal to
        // (r-x)*(a-b)/r + b
        envelope_effect =  (env.decay - (life - env.attack)) * (0xffff - scaled_sustain) / env.decay + scaled_sustain;
    } otherwise { // sustain phase
        envelope_effect = scaled_sustain;
    }

    when(generator->data.enabled) {
        // this is to know at which volume the release should begin at
        generator->last_active_envelope_effect = envelope_effect;
    }
    return ((int)sample * envelope_effect) >> 16;
}


// This represets the FPGA's SPI input handler
void fpga_handle_spi_packet(const byte* data, size_t length) {
    byte packet_type = data[0];

    when(packet_type == 1) { // global_state update
        when(length >= 1 + sizeof(MicrocontrollerGlobalState)) {

            // write each byte into where they belong, this could perhaps be a bit more hardcoded on the FPGA on where the wires go
            for (size_t i = 0; i < sizeof(MicrocontrollerGlobalState); i++) {
                *(((byte*)&fpga_global_state) + i) = *(data + 1 + i);
            }
        }
    }
    elsewhen (packet_type == 2) { // generator_state update
        when (length >= 2 + sizeof(ushort) + sizeof(MicrocontrollerGeneratorState)) {

            ushort generator_index = *(ushort*)(data+1);
            bool reset_note_lifetime = (bool)data[3];

            // write each byte into where they belong, this could perhaps be a bit more hardcoded on the FPGA on where the wires go
            byte* generator_data_ptr = (byte*)&fpga_generators[generator_index].data;
            for (size_t i = 0; i < sizeof(MicrocontrollerGeneratorState); i++) {
                *(generator_data_ptr + i) = *(data + 2 + sizeof(ushort) + i);
            }

            when (reset_note_lifetime) {
                fpga_generators[generator_index].note_life = 0; // make sure this doesn't conflict with the incrmentation after generating a sample
                fpga_generators[generator_index].wavelength_pos = 0;
            }
        }
    }
    // ignore unknown packets
}


// this represents a single generator module, which there are N_GENERATORS of on the FPGA
Sample fpga_generate_sample_from_generator(uint generator_index) {
    FPGAGeneratorState* generator = &fpga_generators[generator_index]; // just a reference, not a copy

    // make sure this is only stepped up once per sample, meaning we might need
    // some kind of enable pin, because using multiple clock domains is a nightmare
    generator->note_life      += NOTE_LIFE_COEFF;
    generator->wavelength_pos += NOTE_LIFE_COEFF;

    // when enabled or during it's envelope release stage
    when (generator->data.enabled || (generator->note_life / NOTE_LIFE_COEFF) < fpga_global_state.envelope.release) {
        uint freq = fpga_note_index_to_freq(generator->data.note_index);

        // old pitchwheel implementation
        //float note_offset = 2.0 * ((float)(fpga_global_state.pitchwheels[generator->data.channel_index])) / 128.0;
        //uint freq_coeff = round(pow(2.0, note_offset/12.0) * (1 << FREQ_SHIFT));
        ///freq = ((unsigned long long)(freq) * freq_coeff) >> FREQ_SHIFT;

        int magic_linear_scale  = ((int)((pow(2, 2.0/12.0) - pow(2.0, -2.0/12.0))*(1<<8)));
        int magic_linear_offset = (1<<16);
        uint freq_coeff
            = fpga_global_state.pitchwheels[generator->data.channel_index]
            * magic_linear_scale
            + magic_linear_offset;
        freq = ((unsigned long long)(freq) * freq_coeff) >> (16);

        // this is not a LUT
        uint wavelength = freq_to_wavelength_in_samples(freq);

        // due to the way registers work, the chisel version requires the +1
        // here, feel free to tweak the operator instead
        when (generator->wavelength_pos/*+1*/ >= wavelength) {
            // this replaces our modulo of note_life, but it also accounts for
            // changing wavelengths due to it's accumulating nature.
            // sin(2 * pi * f * t) would likely see a discontinuous edge if f changes
            generator->wavelength_pos = 0;
        }

        Sample sample;
        when (generator->data.instrument == SQUARE) {
            //when (((generator->note_life * 2) / wavelength) % 2 == 1) {
            when ((generator->wavelength_pos << 1) >= wavelength) {
                sample = -SAMPLE_MAX;
            } otherwise {
                sample = SAMPLE_MAX;
            }
        }
        elsewhen (generator->data.instrument == TRIANGLE) {
            // if x has a wavelength of 4:
            //     f(x) = abs((x+1) % 4 - 2) - 1
            int half    = wavelength>>1;
            int quarter = wavelength>>2;
            int pos;
            when (generator->wavelength_pos > half + quarter) {
                pos = generator->wavelength_pos - half - quarter;
            } otherwise {
                pos = generator->wavelength_pos + quarter;
            }
            sample = (abs(pos - half) - quarter) * SAMPLE_MAX / quarter;
        }
        elsewhen (generator->data.instrument == SAWTOOTH) {
            //sample = ((generator->note_life % wavelength) * 2 - wavelength) * SAMPLE_MAX  / wavelength;
            sample = (generator->wavelength_pos * 2 - wavelength) * SAMPLE_MAX  / wavelength;
        }
        elsewhen (generator->data.instrument == SINE) {
            // todo: convert float to integer
            // sin(2*pi*x) should be a lookup-table, that ought to suffice
            sample = round(SAMPLE_MAX * sin(2 * PI * generator->note_life / wavelength));
        }

        // this doesn't have to be a separate module, it can be inlined into the generator
        return fpga_apply_envelope(sample, generator) * generator->data.velocity / VELOCITY_MAX;
    } otherwise {
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

    return out * fpga_global_state.master_volume << 4; // 4 bits headroom
}












// =======================================================
// ================== DEFINITIONS ========================
// =======================================================



void do_sanity_check() {
    //static_assert(sizeof(byte)    == 1); // There you go, Rikke
    //static_assert(sizeof(bool)    == 1);
    //static_assert(sizeof(ushort)  == 2);
    //static_assert(sizeof(Sample)  == 2); // bitdepth/samplewidth == 16 (2 bytes)
    //static_assert(sizeof(uint)    == 4); // 32 bit
    //static_assert(sizeof(Instrument) == 4); // 32 bit - todo: make this smaller?
}
