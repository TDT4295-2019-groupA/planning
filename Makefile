
.PHONY: all
all: $(patsubst %.dot,dist/%.png,$(wildcard *.dot)) $(patsubst %.dot,dist/%.svg,$(wildcard *.dot))

dist/%.png: %.dot
	dot -Tpng $< -o $@

dist/%.svg: %.dot
	dot -Tsvg $< -o $@

.PHONY: all
clean:
	rm -v dist/*.svg
	rm -v dist/*.png
