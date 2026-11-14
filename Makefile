CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -Iinclude

SRCS = src/main.cpp src/network.cpp src/buffer.cpp src/hashtable.cpp src/utils.cpp src/serialisation.cpp src/pubsub.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = redis_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
