all: old-photo-parallel-A

old-photo-parallel-A: old-photo-parallel-A.c image-lib.c image-lib.h
	gcc old-photo-parallel-A.c image-lib.c image-lib.h -g -o old-photo-parallel-A -lgd

clean:
	rm old-photo-parallel-A

clean_all: clean
	rm -fr ./*-dir

run_all: all
	./old-photo-parallel-A
