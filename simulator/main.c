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

void microcontroller_send_spi_packet(const byte* data, size_t length) {
	if (enable_spi_dump) {
		printf("SPI packet:");
		for (size_t i = 0; i < length; i++) {
			printf(" %02X", data[i]);
		}
		printf("\n");
	}
	fpga_handle_spi_packet(data, length);
}

void midi_event(const byte* data, size_t length) {
	microcontroller_handle_midi_event(data, length);
}

void generate_samples(size_t n) {
	if (enable_n_samples_dump)  printf("Step %d samples\n", n);
	for (size_t i = 0; i < n; i++) {
		WSample s = fpga_generate_sound_sample();
		if (enable_sample_dump)     printf("Sample: %i\n", s);
		if (enable_raw_sample_dump) {

			printf("%c", (s>> 24) & 0xFF);
			printf("%c", (s>> 16) & 0xFF);
			printf("%c", (s>> 8)  & 0xFF);
			printf("%c", (s>> 0)  & 0xFF);
		}

	}
}

// too hacky 4 u
void simulate() {
#include "song.c"
}


int main(int argc, char const *argv[]) {
	for (size_t i = 0; i < argc; i++) {
		/**/ if (!strcmp(argv[i], "-s")) enable_spi_dump        = true;
		else if (!strcmp(argv[i], "-n")) enable_n_samples_dump  = true;
		else if (!strcmp(argv[i], "-o")) enable_sample_dump     = true;
		else if (!strcmp(argv[i], "-r")) enable_raw_sample_dump = true;
	}

	microcontroller_global_generator_state.master_volume = 0xffff;
	microcontroller_send_global_state_update();

	simulate();
	return 0;
}
