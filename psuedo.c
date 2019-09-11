// some plannig psuedo code just so that we're on the same page:

typedef unsigned char Velocity;


typedef struct Note {
	bool is_on;
	Velocity velocity;
} Note;

static_assert(sizeof(unsigned char) == 1);

// this struct will be passed to the FPGA using DMA over SPI
typedef struct FpgaData {
	Velocity master_volume;

	enum Instrument instrument;

	// sound envelope
	short attack; // time
	short decay;  // time
	Velocity sustain; // percentage of colume
	short release; // time

	// note Data
	// [note index][channel index]
	Note[128][16] notes;
} FpgaData;


// what the FPGA does:

//  sum(note.volume * sin(note.freq*t) for note in notes if note.is_on)
float note_index_to_freq(unsigned int n){
	return 440.0 * pow(2, (n - 58) / 12);
}

float make_sample(FpgaData data, time_t time){
	int sample = 0;
	int note_index = 0;
	for (Note note : data.notes) {
		sample += note.volume * sample_generator(time*note_index_to_freq(note_index) * time);
		note_index++;
	}
	return sample;
}
