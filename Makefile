CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude -finput-charset=UTF-8 -fexec-charset=UTF-8
LDFLAGS ?=

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CXXFLAGS += -DOMNI_HAS_ALSA=1
  LDFLAGS += -lasound -lpthread
endif

ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif

.PHONY: all test demo clean

all: stage1_test$(EXE) stage1$(EXE)

CORE := src/wav.cpp src/stage1.cpp src/pedal.cpp

stage1_test$(EXE): src/test.cpp src/test_fx.cpp $(CORE) include/stage1.hpp include/pedal.hpp include/wav.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/test.cpp src/test_fx.cpp $(CORE) $(LDFLAGS)

stage1$(EXE): src/main.cpp $(CORE) include/stage1.hpp include/pedal.hpp include/wav.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp $(CORE) $(LDFLAGS)

test: stage1_test$(EXE)
	./stage1_test$(EXE)

demo: stage1$(EXE)
	./stage1$(EXE)

clean:
	rm -f stage1_test$(EXE) stage1$(EXE)
