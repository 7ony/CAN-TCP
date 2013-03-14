SRCS = main.c libcan.c CServerTcpIP.c
EXEC = CAN-TCP
CFLAGS=  # -std=c99
LDFLAGS= -lpthread
CC=gcc
######################################################################
# should not be modified below this line.
######################################################################
OBJ_DIR = .build

ALL: OBJ_DIR_CREATE $(EXEC)

OBJS = $(addprefix $(OBJ_DIR)/,$(SRCS:.c=.o))

clean:
	@rm -rf $(OBJ_DIR) *~

$(EXEC): $(OBJS)
	@echo "linking .. $@"
	@$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c Makefile
	@echo "compiling.. $<"
	@$(CC) -MD -MF $(OBJ_DIR)/$<.dep $(CFLAGS) -c $< -o $@

OBJ_DIR_CREATE:
	@if [ ! -d $(OBJ_DIR) ]; then mkdir $(OBJ_DIR); fi;

$(foreach source,$(SRCS),$(eval -include $(OBJ_DIR)/${source}.dep))
