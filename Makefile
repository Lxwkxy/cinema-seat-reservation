CXX := g++
CPPFLAGS := -Icodes
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -pthread
LDLIBS := -lrt

BIN_DIR := bin
SERVER := $(BIN_DIR)/server
CLIENT := $(BIN_DIR)/client
LOAD_CLIENT := $(BIN_DIR)/client_load
HEADERS := $(wildcard codes/*.hpp)

.PHONY: all server client load clean

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

clean:
	rm -f $(SERVER) $(CLIENT) $(LOAD_CLIENT)
