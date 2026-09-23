CXX := g++
CPPFLAGS := -Icodes
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -pthread
LDLIBS := -lrt

BIN_DIR := bin
SERVER := $(BIN_DIR)/server
CLIENT := $(BIN_DIR)/client
LOAD_CLIENT := $(BIN_DIR)/client_load
METRICS_TEST := $(BIN_DIR)/client_load_metrics_test
WORKLOAD_TEST := $(BIN_DIR)/client_load_workload_test
EXPERIMENT_HARNESS_TEST := tests/experiment_harness_test.sh
HEADERS := $(wildcard codes/*.hpp)

.PHONY: all server client load test clean

all: $(SERVER) $(CLIENT) $(LOAD_CLIENT)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(SERVER): codes/server.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDLIBS) -o $@

$(CLIENT): codes/client.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDLIBS) -o $@

$(LOAD_CLIENT): codes/client_load.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDLIBS) -o $@

server: $(SERVER)

client: $(CLIENT)

load: $(LOAD_CLIENT)

test: $(METRICS_TEST) $(WORKLOAD_TEST)
	./$(METRICS_TEST)
	./$(WORKLOAD_TEST)
	bash $(EXPERIMENT_HARNESS_TEST)

$(METRICS_TEST): tests/client_load_metrics_test.cpp codes/load_metrics.hpp | $(BIN_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(WORKLOAD_TEST): tests/client_load_workload_test.cpp codes/load_workload.hpp codes/common.hpp | $(BIN_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(SERVER) $(CLIENT) $(LOAD_CLIENT) $(METRICS_TEST) $(WORKLOAD_TEST)
