#include "datatypes.h"

struct fdnode* get_fdnode(void);
struct fdnode* get_nth_node(struct fdnode *head,int n);
int tell_usr(struct user_info *list ,int des_id ,char *msg);
int connection_setup(void);
int add_usr(struct user_info* list);
int search_user_by_fd(struct user_info *list ,int fd);
int check_user_name(struct user_info* list ,char *name);
void init_user_list(struct user_info *list);
void list_user(struct user_info *list ,int caller_id);
void broadcast(struct user_info *list ,char *msg);
void clear_user(struct user_info *list ,int index);