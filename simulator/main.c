#include "../reference_implementation.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool enable_spi_dump        = false;
bool enable_n_samples_dump  = false;
bool enable_sample_dump     = false;
bool enable_raw_sample_dump = false;

/*
TODO:
 - read midi (preferably only with a header-only library)
 - make the microcontroller code process the events
 - pass events to fpga
 - step the fpga each time we see a midifile time-step event
 - write sound output to stdout/file
*/

// a print statement which is able to print even while outputting raw PCM data to stdout:
#define print(...) {if (enable_raw_sample_dump) {fprintf(stderr, __VA_ARGS__); fflush(stderr);} else {printf(__VA_ARGS__);}}


// define missing functions in reference implementation:

// hook the two parts of the reference implementation together:
void microcontroller_send_spi_packet(const byte* data, size_t length) {
	if (enable_spi_dump) {
		print("SPI packet:");
		for (size_t i = 0; i < length; i++) {
			print(" %02X", data[i]);
		}
		print("\n");
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
	if (enable_n_samples_dump)  print("Step %d samples\n", n);
	for (size_t i = 0; i < n; i++) {
		WSample s = fpga_generate_sound_sample();
		if (enable_sample_dump)     print("Sample: %i\n", s);
		if (enable_raw_sample_dump) { // little endian 32bit signed output
			printf("%c", *(((byte*)&s)+0));
			printf("%c", *(((byte*)&s)+1));
			printf("%c", *(((byte*)&s)+2));
			printf("%c", *(((byte*)&s)+3));
		}
	}
}

// load in the events created by python script
void simulate() { // 2hacky4u
#include "song.c"
}


int main(int argc, char const *argv[]) {
	for (size_t i = 0; i < argc; i++) {
		/**/ if (!strcmp(argv[i], "-s")) enable_spi_dump        = true;
		else if (!strcmp(argv[i], "-n")) enable_n_samples_dump  = true;
		else if (!strcmp(argv[i], "-o")) enable_sample_dump     = true;
		else if (!strcmp(argv[i], "-r")) enable_raw_sample_dump = true;
	}

	microcontroller_global_generator_state.master_volume = 0xFF;
	microcontroller_send_global_state_update();

	simulate();
	return 0;
}
