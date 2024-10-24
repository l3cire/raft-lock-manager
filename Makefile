CC     := gcc
CFLAGS := -Wall -Werror 

TEST_DIR			:= ./tests

SRCS_COMMON			:= udp.c
SRCS_CLIENT			:= rpc.c
SRCS_LOCK_SERVER	:= spinlock.c
SRCS_TESTS			:= test_lock.c

BUILD_DIR			:= ./build
BIN_DIR				:= ./bin
FILES_DIR			:= ./server_files

OBJ_CLIENT			:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_CLIENT:.c=.o)))
OBJ_LOCK_SERVER		:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_LOCK_SERVER:.c=.o)))
OBJ_COMMON			:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_COMMON:.c=.o)))
OBJ_TEST			:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_TESTS:.c=.o)))


#if the command starts with run_, use all other words as arguments to "run" command
ifeq (run,$(firstword $(subst _, , $(MAKECMDGOALS)) ))
  RUN_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(RUN_ARGS):;@:)
endif


.PHONY: all
all: client lock_server

client: $(OBJ_COMMON) $(OBJ_CLIENT) $(BUILD_DIR)/client.o
	$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@

lock_server: $(OBJ_COMMON) $(OBJ_LOCK_SERVER) $(BUILD_DIR)/lock_server.o
	$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@

test_%: $(BUILD_DIR)/test_%.o lock_server client clean_files 
	$(CC) $(CFLAGS) $< $(OBJ_COMMON) $(OBJ_CLIENT) $(OBJ_LOCK_SERVER) -o $(BIN_DIR)/$@

run_%: %
	$(BIN_DIR)/$< $(RUN_ARGS) 


$(OBJ_CLIENT) $(OBJ_LOCK_SERVER) $(OBJ_COMMON) $(BUILD_DIR)/client.o $(BUILD_DIR)/lock_server.o: $(BUILD_DIR)/%.o : %.c 
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_TEST): $(BUILD_DIR)/%.o : $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@


clean_files:
	find $(FILES_DIR) -type f -exec cp /dev/null {} \;

clean: clean_files
	rm -f $(BUILD_DIR)/* $(BIN_DIR)/*

