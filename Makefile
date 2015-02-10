CXXFLAGS= -I $(BOOST)/include/boost-1_55
LDFLAGS= -L $(BOOST)/lib

BOOSTLIBS= -lboost_filesystem-gcc47-mt-1_55 -lboost_regex-gcc47-mt-1_55 \
		   -lboost_system-gcc47-mt-1_55 -lboost_system-gcc47-mt-1_55 \
		   -lboost_program_options-gcc47-mt-1_55 -lboost_thread-gcc47-mt-1_55 \
		   -lboost_chrono-gcc47-mt-1_55

LIBS=$(BOOSTLIBS) 

all: apps/reader

apps/%: obj/%.o
	$(CXX) -std=c++0x -o $@ $^ $(LDFLAGS) $(LIBS)

obj/%.o: src/%.cpp
	$(CXX) -std=c++0x  $(CXXFLAGS) -o $@ -c $^

clean:
	rm -f obj/*
	rm -f apps/*
