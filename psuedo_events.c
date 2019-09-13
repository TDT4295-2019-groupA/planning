// some planning psuedo code just so that we're on the same page:
// 'freq' is short for frequency



// =======================================================
// ================== DEFINITIONS ========================
// =======================================================


// shared by FPGA and the microcontroller

#define PI              3.2415926535
#define SAMPLE_RATE     44100
#define N_MIDI_KEYS     128
#define N_MIDI_CHANNELS 16
#define MIDI_A3_INDEX   45    /* see https://www.noterepeat.com/articles/how-to/213-midi-basics-common-terms-explained */
#define MIDI_A3_FREQ    440.0 /* no, i won't listen to your A=432Hz bullshit */
#define VELOCITY_MAX    128
#define SAMPLE_MAX      0x7FFF
#define N_GENERATORS    8 /* number of supported notes playing simultainiously  (polytones), \
                             subject to change, chisel and microcontroller code \
                             should scale from this single variable alone */

typedef unsigned int    uint;
typedef unsigned char   byte;
typedef byte            NoteIndex;
typedef byte            ChannelIndex;
typedef byte            Velocity; // goes from 0 to 127
typedef signed short    Sample;   // To represent a single audio "frame"
typedef unsigned int    Time;     // measured in n samples, meaning x second is represented as x * SAMPLE_RATE

static_assert(sizeof(byte)    == 1); // There you go, Rikke
static_assert(sizeof(Sample)  == 2); // bitdepth/samplewidth == 16 (2 bytes)
static_assert(sizeof(uint)    == 4); // 32 bit

typedef enum Instrument { // we can expand this as much as we want
    SQUARE   = 0,
    TRIANGLE = 1,
    SAWTOOTH = 2,
    SINE     = 3,
} Instrument;

typedef struct Envelope { // either preset or controlled by knobs/buttons on the PCB
    Time attack;
    Time decay;
    Sample sustain; // 'percentage' of volume to sustain at, from 0 to 0x7FFF
    Time release;
} __attribute__((packed)) Envelope;


// The following two structs represent the two packet types to be transmitted
// from the microcontroller to the FPGA:

typedef struct MicrocontrollerGlobalState {
    Velocity   master_volume;
    Envelope   envelope;
    float      pitchwheels [N_MIDI_CHANNELS];
} __attribute__((packed)) MicrocontrollerGlobalState;


typedef struct MicrocontrollerGeneratorState {
    // this is the state of a single fpga generator as seen on the microprocessor,
    // it is also the packet sent over SPI from the microcontroller to the FPGA
    // It represents the state of a single sound generator
    bool       enabled;           // whether the sound generator should be generating audio or not
    Instrument instrument;        // the index determining which waveform to use
    NoteIndex  note_index;        // to determine pitch/frequency
    uint       channel_index;     // to know which pitchwheel to use
    Velocity   velocity;          // to know which pitchwheel to use
} __attribute__((packed)) MicrocontrollerGeneratorState;

// the following two types are the internal state of the FPGA

typedef MicrocontrollerGlobalState FPGAGlobalState;

typedef struct FPGAGeneratorState {
    MicrocontrollerGeneratorState data; // packet from microcontroller stored here

    // The number of samples the note has been playing for, used to determine where in the envelope we are. It gets incremented for each sample generated by the FPGA
    // it is automatically reset to 0 every time the FPGA recieves an generator_state update packet which instructs it to do so
    Time note_life;
} __attribute__((packed)) FPGAGeneratorState;








// =======================================================
// ========== MICROCONTROLLER BEHAVIOUR ==================
// =======================================================







// microcontroller state

static MicroprocessorGlobalState    microcontroller_global_generator_state;
static MicroprocessorGeneratorState microcontroller_generator_states[N_GENERATORS];

// This endpoint is responsible for pushing data over SPI to the FPGA
void microcontroller_send_spi_packet(const uint* data, size_t length); // intentionally left undefined. To be implemented by the simulator

// The following two functions sends update packets to the FPGA
void microcontroller_send_fpga_global_state_update() {
    byte data[1 + sizeof(FPGAGlobalState)];

    data[0] = 1; // global_state update

    memcpy(data+1, &microcontroller_global_generator_state, sizeof(FPGAGlobalState));

    microcontroller_send_spi_packet(data, sizeof(data));
}

void microcontroller_send_fpga_generator_update(uint generator_index, bool reset_note_lifetime) {
    // set reset_note_lifetime to true when sending note-on events
    byte data[2 + sizeof(uint) + sizeof(MicroprocessorGeneratorState)];

    data[0] = 2; // generator update

    data[1] = (byte) reset_note_lifetime;
    *(uint*)(&data[1]) = generator_index;
    memcpy(data+2+sizeof(uint), &microcontroller_generator_states[generator_index], sizeof(MicroprocessorGeneratorState));

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
    for (size_t i = 1; i < length; i++)
        if (bool(data[i] & 0x80) == (i==0))
            return; // ignore invalid packets

    // interpret and handle packet:
    switch (packet_type) {
        // see https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
        break; case 0x1000: { // note-off event
            assert(length == 3);
            ChannelIndex   channel  = type_specifier;
            NoteIndex      note     = data[1];
            Velocity       velocity = data[2];

            // find the sound generator currenty playing this note
            byte idx = 0; // sound_generator_index
            while (idx < N_GENERATORS && !(
                microcontroller_generator_states[idx].enabled
                && microcontroller_generator_states[idx].note_index    == note
                && microcontroller_generator_states[idx].channel_index == channel
            )) idx++;
            if (idx >= N_GENERATORS) return; // none found, probably due to the note-on being ignored due to lack of generators

            microcontroller_generator_states[idx].enabled       = true;
            microcontroller_generator_states[idx].note_index    = note;
            microcontroller_generator_states[idx].channel_index = channel;
            microcontroller_generator_states[idx].velocity      = velocity;
            microcontroller_send_fpga_generator_update(idx, false);
        }
        break; case 0x1001: { // note-on event
            assert(length == 3);
            ChannelIndex   channel  = type_specifier;
            NoteIndex      note     = data[1];
            Velocity       velocity = data[2];

            // find vacant sound generator
            byte idx = 0; // sound_generator_index
            while (idx < N_GENERATORS && microcontroller_generator_states[idx].enabled) idx++;
            if (idx >= N_GENERATORS) return; // out of sound generators, ignore

            microcontroller_generator_states[idx].enabled       = true;
            microcontroller_generator_states[idx].note_index    = note;
            microcontroller_generator_states[idx].channel_index = channel;
            microcontroller_generator_states[idx].velocity      = velocity;
            microcontroller_send_fpga_generator_update(idx, true);
        }
        break; case 0x1010: /*IGNORE*/ // Polyphonic Key Pressure (Aftertouch) event
        break; case 0x1011: /*IGNORE*/ // Control Change event
        break; case 0x1100: /*IGNORE*/ // Program Chang event
        break; case 0x1101: /*IGNORE*/ // Channel Pressure (After-touch) event
        break; case 0x1110: /*IGNORE*/ // Pitch Bend Change event
        break; case 0x1111: /*IGNORE*/ // System Exclusive event
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


// This represets the FPGA's SPI input handler
void fpga_handle_spi_packet(const byte* data, size_t length) {
    packet_type = data[0];

    if (packet_type == 1) { // global_state update
        if (length >= 1 + sizeof(FPGAGlobalState)) {
            memcpy(&fpga_global_state, data+1, sizeof(FPGAGlobalState));
        }
    }
    else if (packet_type == 2) { // generator_state update
        if (length >= 2 + sizeof(uint) + sizeof(MicroprocessorGeneratorState)) {

            uint generator_index = *(uint*)(data+1);
            memcpy(&fpga_generators[generator_index], data+1, sizeof(FPGAGeneratorState));
        }
    }
    // ignore unknown packets
}


// This represents the 'mux' module, which combines the sound from all the generators
Sample fpga_generate_sound_sample() { // is run once per sound sample
    Sample out = 0;

    // this is trivial to do in parallel
    for (size_t generator_index = 0; generator_index < N_GENERATORS; generator_index++) {
        out += fpga_generate_sample_from_generator(generator_index);
        fpga_generators[generator_index].note_life++; // tick time. IMPORTANT: do this only once per sample!
    }

    return sample * fpga_global_state->master_volume;
}


// this represents a single generator module, which there are N_GENERATORS of on the FPGA
Sample fpga_generate_sample_from_generator(uint generator_index) {
    FPGAGeneratorState generator = &fpga_generators[generator_index];

    if (generator->enabled) {
        float freq = fpga_note_index_to_freq(fpga_generator->note_index);
        freq *= fpga_global_state.pitchwheels[generator->channel_index]
        uint wavelength = freq_to_wavelength_in_samples(freq);

        Sample sample;
        // NOTE: perhaps move these generators to separate chisel modules
        if (generator->instrument == SQUARE) {
            if (((generator->note_life * 2) / wavelength) % 2 == 1) {
                sample = -SAMPLE_MAX;
            } else {
                sample = SAMPLE_MAX;
            }
        }
        else if (generator->instrument == TRIANGLE) {
            sample = 0; // TODO
        }
        else if (generator->instrument == SAWTOOTH) {
            sample = 0; // TODO
        }
        else if (generator->instrument == SINE) {
            // todo: convert float to integer
            // sin(2*pi*x) should be a lookup-table, that ought to suffice
            sample = round(SAMPLE_MAX * sin(2 * PI * generator->note_life / wavelength));
        }
        return generator->velocity * fpga_apply_envelope(sample, generator->note_life);
    } else {
        return 0;
    }
}


// this should be lookup table, only 128 possible input values
float fpga_note_index_to_freq(NoteIndex note_index) {
    return MIDI_A3_FREQ * pow(2, (note_index - MIDI_A3_INDEX) / 12);
}

uint freq_to_wavelength_in_samples(float freq) {
    // todo: convert freq to an integer
    return round(SAMPLE_RATE / freq); // the rounding might need some dithering
}

Sample fpga_apply_envelope(Sample sample, Time note_life) {
    state->envelope;
    // TODO
    return sample; // currently does nothing
}
