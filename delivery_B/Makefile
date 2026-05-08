all: old-photo-parallel-B

old-photo-parallel-B: old-photo-parallel-B.c image-lib.c image-lib.h
	gcc old-photo-parallel-B.c image-lib.c image-lib.h -g -o old-photo-parallel-B -lgd

clean:
	rm old-photo-parallel-B

clean_all: clean
	rm -fr ./*-dir

run_all: all
	./old-photo-parallel-B