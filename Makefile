
all: dist/pcb.svg dist/pcb.png dist/processing_pipeline.svg dist/processing_pipeline.png

dist/pcb.svg: pcb.dot
	dot -Tsvg pcb.dot > dist/pcb.svg

dist/pcb.png: pcb.dot
	dot -Tpng pcb.dot > dist/pcb.png

dist/processing_pipeline.svg: processing_pipeline.dot
	dot -Tsvg processing_pipeline.dot > dist/processing_pipeline.svg

dist/processing_pipeline.png: processing_pipeline.dot
	dot -Tpng processing_pipeline.dot > dist/processing_pipeline.png
