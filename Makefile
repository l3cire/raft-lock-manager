CC     := gcc
CFLAGS := -Wall -Werror 


SRCS_COMMON			:= udp.c
SRCS_CLIENT			:= ${SRCS_COMMON} rpc.c client.c
SRCS_LOCK_SERVER	:= ${SRCS_COMMON} lock_server.c spinlock.c

BUILD_DIR			:= ./build
BIN_DIR				:= ./bin

OBJ_CLIENT			:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_CLIENT:.c=.o)))
OBJ_LOCK_SERVER		:= $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS_LOCK_SERVER:.c=.o)))

.PHONY: all
all: client lock_server

client: $(OBJ_CLIENT)
	$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@

run_client: client
	$(BIN_DIR)/$<

lock_server: $(OBJ_LOCK_SERVER)
	$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@

run_lock_server: lock_server
	$(BIN_DIR)/$<

$(BUILD_DIR)/%.o : %.c 
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f ${OBJ_CLIENT} ${OBJ_LOCK_SERVER} $(BIN_DIR)/client $(BIN_DIR)/lock_server

