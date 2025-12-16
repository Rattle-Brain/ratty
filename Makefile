###############
### CONFIG ###
###############

CC      := gcc
TARGET  := pty_test

SRC_DIR := src
BUILD_DIR := build
DEBUG_DIR := $(BUILD_DIR)/debug
RELEASE_DIR := $(BUILD_DIR)/release

CFLAGS_COMMON := -Wall -Wextra
CFLAGS_DEBUG  := -g -O0
CFLAGS_RELEASE := -O2

INCLUDES := -I$(SRC_DIR)
LIBS := -lglfw -lGL -lutil

###############
### SOURCES ###
###############

SRCS := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/core/pty.c

OBJS_DEBUG := $(SRCS:$(SRC_DIR)/%.c=$(DEBUG_DIR)/%.o)
OBJS_RELEASE := $(SRCS:$(SRC_DIR)/%.c=$(RELEASE_DIR)/%.o)

###############
### TARGETS ###
###############

.PHONY: all debug release clean clean_debug clean_release

all: debug

debug: $(DEBUG_DIR)/$(TARGET)

release: $(RELEASE_DIR)/$(TARGET)

###############
### LINKING ###
###############

$(DEBUG_DIR)/$(TARGET): $(OBJS_DEBUG)
	$(CC) $^ $(LIBS) -o $@

$(RELEASE_DIR)/$(TARGET): $(OBJS_RELEASE)
	$(CC) $^ $(LIBS) -o $@

###############
### COMPILATION ###
###############

$(DEBUG_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) $(CFLAGS_DEBUG) $(INCLUDES) -c $< -o $@

$(RELEASE_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) $(CFLAGS_RELEASE) $(INCLUDES) -c $< -o $@

###############
### CLEAN ###
###############

clean:
	rm -rf $(BUILD_DIR)

clean_debug:
	rm -rf $(DEBUG_DIR)

clean_release:
	rm -rf $(RELEASE_DIR)
