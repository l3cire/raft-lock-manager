CC     := gcc
CFLAGS := -Wall -Werror 

TEST_DIR			:= ./tests

SRCS_COMMON			:= udp.c
SRCS_CLIENT			:= client_rpc.c
SRCS_LOCK_SERVER		:= spinlock.c server_rpc.c timer.c tmdspinlock.c raft.c raft_leader.c raft_follower.c raft_candidate.c raft_utils.c raft_storage_manager.c
SRCS_TESTS			:= test_lock.c test_long_requests.c test_timer.c test_tmdspinlock.c test_raft_election.c

BUILD_DIR			:= ./build
BIN_DIR				:= ./bin
FILES_DIR			:= ./server_files_1 ./server_files_2 ./server_files_3 ./server_files_4/ ./server_files_5/

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
all: client server

client: $(OBJ_COMMON) $(OBJ_CLIENT) $(BUILD_DIR)/client.o
	$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@

server: $(OBJ_COMMON) $(OBJ_LOCK_SERVER) $(BUILD_DIR)/server.o
	$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@

test_%: $(BUILD_DIR)/test_%.o server client clean_files 
	$(CC) $(CFLAGS) $< $(OBJ_COMMON) $(OBJ_CLIENT) $(OBJ_LOCK_SERVER) -o $(BIN_DIR)/$@

run_%: %
	$(BIN_DIR)/$< $(RUN_ARGS) 


$(OBJ_CLIENT) $(OBJ_LOCK_SERVER) $(OBJ_COMMON) $(BUILD_DIR)/client.o $(BUILD_DIR)/server.o: $(BUILD_DIR)/%.o : %.c 
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_TEST): $(BUILD_DIR)/%.o : $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@


clean_files:
	find $(FILES_DIR) -type f -maxdepth 1 -not -name "raft_state" -exec cp /dev/null {} \;

clean_snapshots:
	find $(FILES_DIR) -mindepth 1 -maxdepth 1 -type d -exec rm -R {} +

clean: clean_files
	rm -f $(BUILD_DIR)/* $(BIN_DIR)/*

