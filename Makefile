BINARIES = counter stack queue
SOURCES = ${BINARIES:=.cpp}
all: ${BINARIES}

${BINARIES}: ${SOURCES}
	g++ -std=c++11 -W -Wall -O3 -ltbb -Wl,--no-as-needed -pthread $@.cpp -o $@ 
clean:
	rm -f ${BINARIES}
