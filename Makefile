.PHONY: all demo test benchmark hardware os os-benchmark
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra
all: marketflow/marketflow
marketflow/marketflow: marketflow/main.cpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test: all
	./marketflow/marketflow 1000000
benchmark: all
	./marketflow/marketflow 100000000 balanced
