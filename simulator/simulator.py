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
	return subprocess.run(cmd, *args, **kwargs)

def convert_mido_midi_to_c_simulator_events(filename):
	t = 0
	n_samples = 0
	for msg in MidiFile(filename):
		if msg.time > 0:
			t += msg.time
			n = int(t * SAMPLERATE)
			yield f"generate_samples({n - n_samples});"
			n_samples = n

		if msg.is_meta: continue

		data = "".join(f"\\x{i:X}" for i in msg.bytes())
		yield f"midi_event(\"{data}\", {len(msg.bytes())});"

def write_song_c(lines):
	with open("song.c", "w") as f:
		f.write("\n".join(lines) + "\n")

def main():
	if len(sys.argv) <= 1:
		print(" "*3, __file__, "<midifile> [flags]")
		print("flags:")
		print("\t-p   play output")
		print("\t-w   make wav (using SOX)")
		print("\t-3   make mp3 (using LAME)")
		print("\t-s   enable spi dump")
		print("\t-n   enable n samples dump")
		print("\t-o   enable sample dump")
		print("\t-r   enable raw sample dump")
		return

	filename = sys.argv[1]
	flags = sys.argv[2:]

	if filename != "-":
		print_status("Parsing", filename, "into c...")
		events = convert_mido_midi_to_c_simulator_events(sys.argv[1])
		print_status("Writing song.c...")
		write_song_c(events)

	print_status("Compiling simulator...")
	run("gcc main.c -lm -o sim.out") # compile

	print_status("Running simulator...")
	safe_flags = " ".join(quote(i) for i in flags if i not in ("-p", "-w", "-3", "-r"))
	if "-p" in flags:
		run(["bash", "-c", f"./sim.out -r {safe_flags} | aplay -c 1 -f S32_LE -r 44100"])
	elif "-w" in flags:
		run(["bash", "-c", f"./sim.out -r {safe_flags} | sox -t raw -r 44100 -e signed -b 32 -c 1 - -r 44100 {quote(filename+'.wav')}"])
		print_status(f"output written to {filename+'.wav'}")
	elif "-3" in flags:
		run(["bash", "-c", f"./sim.out -r {safe_flags} | lame -r -s 44.1 --bitwidth 32 --signed -m mono - {quote(filename+'.mp3')}"])
		print_status(f"output written to {filename+'.mp3'}")
	else:
		run(["./sim.out", *flags])

if __name__ == "__main__":
	main()
