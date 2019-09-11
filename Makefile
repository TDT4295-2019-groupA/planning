
all: dist/pcb.svg dist/pcb.png

dist/pcb.svg: pcb.dot
	dot -Tsvg pcb.dot > dist/pcb.svg
	
dist/pcb.png: pcb.dot
	dot -Tpng pcb.dot > dist/pcb.png
