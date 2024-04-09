CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS =

# List of source files
SOURCES = systemMonitoringSignals.c stats_functions.c
# List of object files (automatically generated)
OBJECTS = $(SOURCES:.c=.o)
# Name of the executable
EXECUTABLE = systemMonitoringSignals

# Main target
all: $(EXECUTABLE)

# Compile each source file into an object file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link all object files into the executable
$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

# Clean up intermediate object files and executable
clean:
	rm -f $(EXECUTABLE) $(OBJECTS)
