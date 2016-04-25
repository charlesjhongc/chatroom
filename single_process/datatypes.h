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

struct user_pipe_inbox
{
	int validate;
	int pipe_out_fd;
	int pipe_in_fd;
};

struct user_info
{
	int active;
	int port;
	int fd;
	char name[20];
	char ip[15];//or other type?
	char path[20];
	struct fdnode *usrqueue;
	struct user_pipe_inbox my_inbox[29];
};