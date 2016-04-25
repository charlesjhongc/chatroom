#include "datatypes.h"

struct fdnode* get_fdnode(void);
struct fdnode* get_nth_node(struct fdnode *head,int n);
int tell_usr(int des_id ,char *msg);
int connection_setup(void);
int add_usr(void);
int check_user_name(char *name);
void init_user_list(void);
void list_user(int caller_id);
void broadcast_to_usrs(struct broadcast_buffer *input);
void clear_user(int index);
void handle_signal1(int sig);
void handle_signal2(int sig);
void handle_child_termi(int sig);
void term_parent(int sig);