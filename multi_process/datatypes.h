struct fdnode
{
	int validate;
	int redir_flag;
	int p_in_fd;
	int p_out_fd;
	int outputfd;
	int errofd;
	struct fdnode *next;
};

struct user_fifo_inbox
{
	int validate;
	int fifo_fd;
	char myFIFO[20];
};

struct broadcast_buffer
{
	int need_broadcast;
	char msg[1024];
};

struct user_info
{
	int active;
	int port;
	int pid;
	int sig_1_raiser;
	char name[20];
	char ip[15];//or other type?
	char msg[1024];//receive yell or tell
	struct user_fifo_inbox my_inbox[29];
};