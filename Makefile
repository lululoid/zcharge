# Define the compiler and flags
CC = g++
CFLAGS = -static-libgcc -static-libstdc++
LDFLAGS = -L$(PREFIX)/aarch64-linux-android/lib -llog

# Define directories and files
TARGET = system/bin/zcharge
SRCS = system/bin/zcharge.cpp
LIBS = sqlite-amalgamation/libsqlite3.a
SHARED_LIB = $(PREFIX)/aarch64-linux-android/lib/libc++_shared.so

# Define the default target
all: check_lib $(TARGET)

# Rule to check for the shared library
check_lib:
	@if [ ! -f $(SHARED_LIB) ]; then \
		echo "Error: $(SHARED_LIB) not found. Please install ndk-multilib if you are in termux."; \
		exit 1; \
	fi

# Rule to build the target
$(TARGET): $(SRCS) $(LIBS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS) $(LDFLAGS)

test: $(SRCS) $(LIBS)
	$(CC) $(CFLAGS) -g -o $(TARGET)_test $(SRCS) $(LIBS) $(LDFLAGS)

# Clean rule
clean:
	rm -f $(TARGET)

# PHONY targets
.PHONY: all clean check_lib test
