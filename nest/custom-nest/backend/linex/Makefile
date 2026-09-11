# PatternFlow custom nest — Linux build.
# Sources are a byte-copy of ../engine. Do not edit the .cpp/.hpp here.
# On Linux: ./build.sh   or   make

CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -DNDEBUG -Wall
LDFLAGS  ?=
TARGET    = nest

.PHONY: all clean

all: $(TARGET)

$(TARGET): pf_nest.cpp pf_json.hpp pf_geom.hpp pf_dev.hpp pf_dev_trials.hpp
	$(CXX) $(CXXFLAGS) pf_nest.cpp -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
