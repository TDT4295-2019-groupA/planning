// some planning psuedo code just so that we're on the same page:

#define PI              3.2415926535
#define SAMPLE_RATE     44100
#define POLYTONALITY    8 /* number of supported notes playing simultainiously, \
                             subject to change, code should scale from a single variable */
#define N_MIDI_KEYS     128
#define N_MIDI_CHANNELS 16
#define MIDI_A3_INDEX   45 /* see https://www.noterepeat.com/articles/how-to/213-midi-basics-common-terms-explained */
#define SAMPLE_MAX      0x7FFF


typedef unsigned int    uint;
typedef unsigned char   NoteIndex;
typedef unsigned char   Velocity;
typedef signed short    Sample; // To represent a single audio "frame"
typedef unsigned int    Time; // measured in n samples

static_assert(sizeof(unsigned char) == 1); // There you go, Rikke
static_assert(sizeof(signed short)  == 2);



typedef enum Instrument {
	SQUARE   = 0,
	TRIANGLE = 1,
	SAWTOOTH = 2,
	SINE     = 3,
} Instrument;

typedef struct Note {
	bool     is_on;
	Velocity velocity;
} Note;

typedef struct Envelope {
	Time attack;
	Time decay;
	Sample sustain; // percentage of volume
	Time release;
} Envelope;


// this is the struct which will be passed to the FPGA using DMA over SPI
typedef struct FpgaData {
	Velocity   master_volume;
	Instrument instruments  [N_MIDI_CHANNELS];
	Envelope   envelopes    [N_MIDI_CHANNELS];
	Note       notes        [N_MIDI_KEYS][N_MIDI_CHANNELS];
} FpgaData;





// what the FPGA does:





// Python code for the generation of a single sample, using 'sin' as its instrument:
// >>> sample = sum(note.velocity * sin(2 * pi * note.freq * note.time_alive) for note in notes if note.is_on)

// TODO: get rid of the remaining floats
//       one solution is to convert everything to ints, and just scale it up by
//       like 1000 or something to have _some_ decimal accuracy

float note_index_to_freq(NoteIndex note_index) {
	// this could be a lookup table
	return 440.0 * pow(2, (note_index - MIDI_A3_INDEX) / 12);
}

uint freq_to_wavelength_in_samples(float freq) {
	// if we decide not to support MIDI pitch bending, the input could be changed into a
	return round(SAMPLE_RATE / freq); // the rounding might need some dithering
}

Sample apply_envelope(Sample sample, Time note_life, Envelope envelope) {
	// TODO
	return sample; // does nothing
}

Sample sample_generator(Time note_life /* measured in samples */, NoteIndex note_index, Instrument instrument) {
	float freq      = note_index_to_freq(note_index);
	uint wavelength = freq_to_wavelength_in_samples(freq);

	switch (instrument) {
		case SQUARE:   return (((note_life * 2) / wavelength) % 2 == 1) ? 1 : 0;
		case TRIANGLE: return 0 /* TODO */;
		case SAWTOOTH: return 0 /* TODO */;
		case SINE:     return sin(2 * PI * note_life / (wavelength));
	}
}

// this is the good shit we do for each sample
struct SoundGeneratorState {
	// this is state stored on the FPGA
	// It represents the state of each sound generator, like if it's on, how long it's been playing for and which note it is assigned to
	bool      in_use        = false;
	NoteIndex note_index    = 0; // Where to check if the note has been turned off, used to determine frequency
	uint      channel_index = 0; // Where to check if the note has been turned off
	Time      note_life     = 0; // The number of samples the note has been playing for
};
Sample make_sound_sample(const FpgaData* data, float time) {
	static SoundGeneratorState generators[POLYTONALITY]; // static means it's essentially a global variable

	// this part is hard to parralelize due to race conditions
	// it assigns the playing notes to sound generators
	// if we instead took in midi as events, this part would be greatly reduce dissapear
	// if we have room for POLYTONALITY=2048, then we could dedicate each generator to a single note, removing the need for this step
	size_t note_index = 0;
	size_t channel_index = 0;

	#define current_note (data->notes[note_index][channel_index])
	for (size_t generator_index = 0; generator_index < POLYTONALITY; generator_index++) {
		SoundGeneratorState* generator = &generators[generator_index];
		if (generator->in_use && ! note->is_on) {
			generator->in_use = false;
		}
		if (!generator->in_use) {
			while ()
			// this is a huge mess
		}
	}


	// this can be done in parallel quite easy
	Sample out = 0;
	for (size_t generator_index = 0; generator_index < POLYTONALITY; generator_index++) {
		SoundGeneratorState* generator = &generators[generator_index];
		if (generator->in_use) {
			out += note.volume * sample_generator();
			generator->note_life++;
		}
	}

	return sample * data->master_volume;
}
