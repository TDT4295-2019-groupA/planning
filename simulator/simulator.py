#!/usr/bin/env python3
from functools import partial
from mido import MidiFile
from shlex import split, quote
import subprocess
import sys

SAMPLERATE = 44100

print_status = partial(print, file=sys.stderr)

def run(cmd, *args, **kwargs):
	if isinstance(cmd, str):
		cmd = split(cmd)
	print_status(f"+ {' '.join(map(quote, cmd))}")
	return subprocess.run(cmd, *args, check=True, **kwargs)

def convert_midi_to_c_simulator_events(filename):
	t = 0
	n_samples = 0
	for msg in MidiFile(filename):
		if msg.time > 0: # round-about way of tracking time to avoid cumulating rounding errors
			t += msg.time
			n = int(t * SAMPLERATE)
			yield f"generate_samples({n - n_samples});\n"
			n_samples = n

		if msg.is_meta: continue

		data = "".join(f"\\x{i:02X}" for i in msg.bytes())
		yield f"midi_event(\"{data}\", {len(msg.bytes())});\n"

def convert_black_midi_to_c_simulator_events(filename):
	# this won't cause gcc to balloon up to use 20+ GB of RAM on black midis
	yield "struct asd {const char* data; unsigned char len; unsigned int n_samples;};\n"
	yield "static const struct asd data[] = {\n"

	t = 0
	n_samples = 0
	prev_n_samples = 0
	for msg in MidiFile(filename):
		if msg.time > 0: # round-about way of tracking time to avoid cumulating rounding errors
			t += msg.time
			n_samples = int(t * SAMPLERATE)

		if msg.is_meta: continue

		data = "".join(f"\\x{i:02X}" for i in msg.bytes())
		yield f"\t{{\"{data}\", {len(msg.bytes())}, {n_samples - prev_n_samples}}},\n"
		prev_n_samples = n_samples

	yield "};\n\n"
	yield "for (size_t i = 0; i < sizeof(data)/sizeof(struct asd); i++) {\n"
	yield "\tif (data[i].n_samples) generate_samples(data[i].n_samples);\n"
	yield "\tmidi_event(data[i].data, data[i].len);\n"
	yield "}\n"

def write_song_c(lines):
	with open("song.c", "w") as f:
		f.writelines(lines)

def show_help():
	print()
	print(" "*3, __file__, "<midifile> [flags]\n")
	print("I will convert the provided midi file into simulator events and")
	print("write them to song.c, then compile main.c, then run it.")
	print("")
	print("flags:")
	print("\t-h   show this")
	print("\t-p   play output (using APLAY)")
	print("\t-w   make wav (using SOX)")
	print("\t-3   make mp3 (using LAME)")
	print("\t-T   short for -s -o -m, used for making test data")
	print("\t-C   short for -c -s -n, used for playback from RPi")
	print("\t-c   dump as python commands instead of chisel3 test friendly text")
	print("\t-s   enable spi dump")
	print("\t-n   enable n samples dump")
	print("\t-o   enable sample dump")
	print("\t-r   enable raw sample dump")
	print("\t-m   skip silence at beginning (intended for -o)")
	print("\t-b   write song.c in compiler-friendly format")
	print(f"\nExample usage for making chisel tests:\n\t{__file__} my_midi_file.mid -T | head -n 4000 > test_data.txt\n")
	print(f"\nExample usage for RPi:\n\t{__file__} my_midi_file.mid -C | ssh pi.local python3\n")

def main():
	if len(sys.argv) <= 2:
		show_help()
		return

	filename = sys.argv[1]
	flags = sys.argv[2:]

	if "-h" in flags:
		show_help()
		return

	if filename != "-":
		print_status("Parsing", filename, "into c...")
		if "-b" in flags:
			events = convert_black_midi_to_c_simulator_events(sys.argv[1])
		else:
			events = convert_midi_to_c_simulator_events(sys.argv[1])
		print_status("Writing song.c...")
		write_song_c(events)

	print_status("Compiling simulator...")
	# compile
	if "-b" in flags:
		run("gcc main.c -lm -o main.out")
	else:
		run("gcc main.c -lm -o main.out -O0")

	if "-T" in flags:
		#flags = [i for i in flags if i != "-T"] + ["-s", "-n", "-o", "-m"]
		flags = [i for i in flags if i != "-T"] + ["-s", "-o", "-m"]
	if "-C" in flags:
		flags = [i for i in flags if i != "-C"] + ["-c", "-s", "-n"]

	if "-c" in flags:
		import textwrap
		print(textwrap.dedent("""
			#!/usr/bin/env python3
			import time, spidev, sys
			CHIP_SELECT = 0
			SPI = spidev.SpiDev()
			SPI.open(0, CHIP_SELECT)
			SPI.max_speed_hz = 100000
			SPI.mode = 0
			SPI.bits_per_word = 8
			SPI.cshigh = True
			TIME = time.time()
			def step_n_samples(n):
				global TIME
				TIME += n / 44100
				try:
					time.sleep(TIME - time.time())
				except ValueError:
					pass
			def send_spi(data:list):
				print("SPI:", " ".join(map(lambda n: "%02x"%n, data)))
				sys.stdout.flush()
				SPI.xfer(data)
			send_spi([0]*32) # flush
		""").strip())
		sys.stdout.flush()

	print_status("Running simulator...")
	cmd_flags = " ".join(flags)
	if "-p" in flags:
		run(["bash", "-c", f"./main.out -r {cmd_flags} | aplay -c 1 -f S32_LE -r 44100"])
	elif "-w" in flags:
		run(["bash", "-c", f"./main.out -r {cmd_flags} | sox -t raw -r 44100 -e signed -b 32 -c 1 - -r 44100 {quote(filename+'.wav')}"])
		print_status(f"output written to {filename+'.wav'}")
	elif "-3" in flags:
		run(["bash", "-c", f"./main.out -r {cmd_flags} | lame -r -s 44.1 --bitwidth 32 --signed -m mono - {quote(filename+'.mp3')}"])
		print_status(f"output written to {filename+'.mp3'}")
	else:
		run(["./main.out", *flags])

if __name__ == "__main__":
	main()
