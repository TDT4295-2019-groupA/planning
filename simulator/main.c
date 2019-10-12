#include "../reference_implementation.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// runtime flags:
bool enable_spi_dump        = false;
bool enable_n_samples_dump  = false;
bool enable_sample_dump     = false;
bool enable_raw_sample_dump = false;
bool enable_command_style_dump = false;
bool enable_starting_silence_skip = false;

// a print statement which is able to print even while outputting raw PCM data to stdout:
#define print(...) {if (enable_raw_sample_dump) {fprintf(stderr, __VA_ARGS__); fflush(stderr);} else {printf(__VA_ARGS__);}}

// define missing functions in reference implementation:

// hook the two parts of the reference implementation together:
void microcontroller_send_spi_packet(const byte* data, size_t length) {
	if (enable_spi_dump) {
		if (enable_command_style_dump) {
			print("send_spi([");
			for (size_t i = 0; i < length; i++) {
				if (i) print(", ");
				print("0x%02X", data[i]);
			}
			print("])\n");
		} else {
			print("SPI:");
			for (size_t i = 0; i < length; i++) {
				print(" %02X", data[i]);
			}
			print("\n");
		}
	}
	fflush(stderr);
	fpga_handle_spi_packet(data, length);
}

// no pcb buttons are pressed:
bool microcontroller_poll_pcb_button_state(uint button_id) {
	return false; // no buttons are held down
}


// our simulator events:

#define midi_event(data, length) \
	microcontroller_handle_midi_event((const byte*) data, length)

void generate_samples(size_t n) {
	if (enable_n_samples_dump &&  enable_command_style_dump) print("step_n_samples(%d)\n", n);
	if (enable_n_samples_dump && !enable_command_style_dump) print("Step: %d samples\n", n);
	if (!enable_sample_dump && !enable_raw_sample_dump) return;
	for (size_t i = 0; i < n; i++) {
		WSample s = fpga_generate_sound_sample();
		if (enable_starting_silence_skip && s == 0) continue;
		enable_starting_silence_skip = false;
		if (enable_sample_dump &&  enable_command_style_dump) print("expect_sample(%i)\n", s);
		if (enable_sample_dump && !enable_command_style_dump) print("Sample: %i\n", s);
		if (enable_raw_sample_dump) { // little endian 32bit signed output
			printf("%c", *(((byte*)&s)+0));
			printf("%c", *(((byte*)&s)+1));
			printf("%c", *(((byte*)&s)+2));
			printf("%c", *(((byte*)&s)+3));
		}
	}
}

// load in the events created by our python script
void simulate() { // 2hacky4u
#include "song.c"
}


int main(int argc, char const *argv[]) {
	for (size_t i = 0; i < argc; i++) {
		/**/ if (!strcmp(argv[i], "-s")) enable_spi_dump        = true;
		else if (!strcmp(argv[i], "-n")) enable_n_samples_dump  = true;
		else if (!strcmp(argv[i], "-o")) enable_sample_dump     = true;
		else if (!strcmp(argv[i], "-r")) enable_raw_sample_dump = true;
		else if (!strcmp(argv[i], "-c")) enable_command_style_dump = true;
		else if (!strcmp(argv[i], "-m")) enable_starting_silence_skip = true;
	}

	// hardcoded envelope settings for now

	microcontroller_global_generator_state.envelope.attack  = 0.0 * SAMPLE_RATE;
	microcontroller_global_generator_state.envelope.decay   = 0.0 * SAMPLE_RATE;
	microcontroller_global_generator_state.envelope.sustain = 0.5 * 0xff;
	microcontroller_global_generator_state.envelope.release = 0.0 * SAMPLE_RATE;

	//microcontroller_global_generator_state.envelope.attack  = 0.04 * SAMPLE_RATE;
	//microcontroller_global_generator_state.envelope.decay   = 0.2 * SAMPLE_RATE;
	//microcontroller_global_generator_state.envelope.sustain = 0.6 * 0xff;
	//microcontroller_global_generator_state.envelope.release = 0.2 * SAMPLE_RATE;

	microcontroller_global_generator_state.master_volume = 0xFF >> 1;
	microcontroller_send_global_state_update();

	// just some code to visualize the current envelope when the note is held half a second
	/*
	fpga_generators[0].note_life = 0;
	fpga_generators[0].data.enabled = true;
	for (size_t i = 0; i < SAMPLE_RATE; i++) {
		fpga_generators[0].note_life += NOTE_LIFE_COEFF;
		if (i == SAMPLE_RATE/2) fpga_generators[0].data.enabled = false;
		if (i == SAMPLE_RATE/2) fpga_generators[0].note_life = 0;
		Sample s = fpga_apply_envelope(SAMPLE_MAX, &fpga_generators[0]);
		printf("%d\n", s);
	}
	return 0;
	*/

	// hardcoded instruments for now

	for (size_t i = 0; i < N_GENERATORS; i++) {
		microcontroller_generator_states[i].instrument = SQUARE;
	}

	simulate();
	return 0;
}
