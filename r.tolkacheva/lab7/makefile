PROJECT_NAME := lab7
EXECUTABLE := $(PROJECT_NAME)

CC := gcc
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -Wpedantic -Wno-unused-variable
# CFLAGS += -Wno-unused-parameter -Wno-sign-compare -Qunused-arguments -Wshadow
# CFLAGS += -fsanitize=address
# CFLAGS += -g -fsanitize=undefined 
CFLAGS += -O2
CLIBS := -pthread

SOURCE_DIR := ./
SOURCES := main.c copy_info.c sleep.c task_stack.c

BUILD_DIR := ./build
OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)

lab: $(BUILD_DIR)/$(EXECUTABLE)

run: lab
	@./$(BUILD_DIR)/$(PROJECT_NAME) 

rebuild: clean lab

$(BUILD_DIR)/$(EXECUTABLE) : $(OBJECTS)
	@echo "Compiling" $@ 
	@$(CC) $(CFLAGS) $(OBJECTS) -o $@

%.o: %.c
$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling" $@
	@$(CC) $(CFLAGS) -c $< -o $@

clean: 
	@rm -rf $(BUILD_DIR)