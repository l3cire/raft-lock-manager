CC     := gcc
CFLAGS := -Wall -Werror 

TEST_DIR			:= ./tests
TEST_CLIENTS_DIR		:= ./test_clients

SRCS_COMMON			:= udp.c
SRCS_CLIENT			:= client_rpc.c
SRCS_LOCK_SERVER		:= spinlock.c server_rpc.c timer.c tmdspinlock.c raft.c raft_leader.c raft_follower.c raft_candidate.c raft_utils.c raft_storage_manager.c

SRCS_TESTS			:= test_long_requests.c test_clients.c test1_packet_delay.c test2_packet_drop.c
SRCS_TEST_CLIENTS		:= client_long_requests.c client_mult_sessions.c client_no_release.c client_normal.c 

BUILD_DIR			:= ./build
BIN_DIR				:= ./bin
FILES_DIR			:= ./server_files_1 ./server_files_2 ./server_files_3 ./server_files_4/ ./server_files_5/

OBJ_CLIENT			:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_CLIENT:.c=.o)))
OBJ_LOCK_SERVER			:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_LOCK_SERVER:.c=.o)))
OBJ_COMMON			:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_COMMON:.c=.o)))

OBJ_TEST			:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_TESTS:.c=.o)))
OBJ_TEST_CLIENTS		:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_TEST_CLIENTS:.c=.o)))

CLIENT_TARGETS			:= $(notdir $(SRCS_TEST_CLIENTS:.c=))

#if the command starts with run_, use all other words as arguments to "run" command
ifeq (run,$(firstword $(subst _, , $(MAKECMDGOALS)) ))
  RUN_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(RUN_ARGS):;@:)
endif


.PHONY: all
all: $(client_tartgets) server

server: $(OBJ_COMMON) $(OBJ_LOCK_SERVER) $(BUILD_DIR)/server.o
	$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@

test%: $(BUILD_DIR)/test%.o server $(CLIENT_TARGETS) clean_files 
	$(CC) $(CFLAGS) $< $(OBJ_COMMON) $(OBJ_CLIENT) $(OBJ_LOCK_SERVER) -o $(BIN_DIR)/$@

client_%: $(BUILD_DIR)/client_%.o $(OBJ_COMMON) $(OBJ_CLIENT) 
	$(CC) $(CFLAGS) $< $(OBJ_COMMON) $(OBJ_CLIENT) -o $(BIN_DIR)/$@

run_%: %
	$(BIN_DIR)/$< $(RUN_ARGS) 


$(OBJ_CLIENT) $(OBJ_LOCK_SERVER) $(OBJ_COMMON) $(BUILD_DIR)/client.o $(BUILD_DIR)/server.o: $(BUILD_DIR)/%.o : %.c 
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_TEST): $(BUILD_DIR)/%.o : $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_TEST_CLIENTS): $(BUILD_DIR)/%.o : $(TEST_CLIENTS_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@


clean_files:
	find $(FILES_DIR) -type f -maxdepth 1 -not -name "raft_state" -exec cp /dev/null {} \;

clean_snapshots:
	find $(FILES_DIR) -mindepth 1 -maxdepth 1 -type d -exec rm -R {} +

clean: clean_files
	rm -f $(BUILD_DIR)/* $(BIN_DIR)/*

