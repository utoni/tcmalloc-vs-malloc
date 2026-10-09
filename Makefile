CC  = gcc
CXX = g++
CFLAGS = -Wall -Wextra -O3

STATIC_TCMALLOC := gperftools-install/lib/libtcmalloc_minimal.a

all: tcmalloc benchmark_mthread_std benchmark_mthread_tcmalloc

tcmalloc: $(STATIC_TCMALLOC)

$(STATIC_TCMALLOC):
	git submodule update --init
	cd gperftools && ./autogen.sh
	mkdir -p gperftools-build
	cd gperftools-build && ../gperftools/configure --disable-shared --enable-static \
		--prefix="$$(realpath ../gperftools-install)" \
		--disable-debugalloc --enable-minimal
	cd gperftools-build && make install

benchmark_mthread_std: benchmark.cpp
	$(CXX) $(CFLAGS) -std=c++20 $< -o $@

benchmark_mthread_tcmalloc: benchmark.cpp $(STATIC_TCMALLOC)
	$(CXX) $(CFLAGS) -std=c++20 -DENABLE_TCMALLOC -o $@ $< $(STATIC_TCMALLOC)

benchmark: benchmark_mthread_std benchmark_mthread_tcmalloc
	sudo ./benchmark_mthread_std >benchmark_mthread_std.dat
	sudo ./benchmark_mthread_tcmalloc >benchmark_mthread_tcmalloc.dat
	./benchmark.gnuplot
	xdg-open ./benchmark.png

clean:
	rm -f benchmark_mthread_std benchmark_mthread_tcmalloc

distclean:
	rm -rf ./gperftools-build ./gperftools-install
	rm -f $(STATIC_TCMALLOC)

.PHONY: all tcmalloc benchmark distclean
