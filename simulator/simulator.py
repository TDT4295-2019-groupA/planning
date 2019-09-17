#!/usr/bin/env python3
from mido import MidiFile
from functools import partial
import sys
import subprocess
from shlex import split, quote

SAMPLERATE = 44100

def run(cmd, *args, **kwargs):
	if isinstance(cmd, str):
		cmd = split(cmd)
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
		yield f"midi_event((const byte*) \"{data}\", {len(msg.bytes())});"


def write_song_c(lines):
	with open("song.c", "w") as f:
		f.write("\n".join(lines) + "\n")


#play
# aplay -c 1 -f S16_LE -r 44100
#wav
# sox -t raw -r 44100 -e unsigned -b 8 -c 1 - -r 44100 out.wav
#mp3
# lame -r -s 44.1 --bitwidth 8 --unsigned -m mono - out.mp3


def main():
	if len(sys.argv) <= 1:
		print(" "*3, __file__, "<midifile> [flags]")
		print("flags:")
		print("\t-p   play output (trumps the rest)")
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
		events = convert_mido_midi_to_c_simulator_events(sys.argv[1])
		write_song_c(events)

	run("gcc main.c -lm -O3 -o sim.out") # compile


	if "-p" in flags:
		run(["bash", "-c", f"./sim.out -r | aplay -c 1 -f S32_LE -r 44100"])
	elif "-w" in flags:
		run(["bash", "-c", f"./sim.out -r | sox -t raw -r 44100 -e signed -b 32 -c 1 - -r 44100 {quote(filename+'.wav')}"])
		print(f"output written to {filename+'.wav'}")
	elif "-3" in flags:
		run(["bash", "-c", f"./sim.out -r | lame -r -s 44.1 --bitwidth 32 --signed -m mono - {quote(filename+'.mp3')}"])
		print(f"output written to {filename+'.mp3'}")
	else:
		run(["./sim.out", *flags])

if __name__ == "__main__":
	main()
