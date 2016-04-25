#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "func.h"

#define PORT 5001
#define SHMKEY ((key_t) 7789)

struct user_info *user_list;
int current_usr_id;
int temp;
int sig_fd;

int main(void)
{
	struct sockaddr_in user_addr;
	struct fdnode *free_temp ,*nth_node ,*queue;
	struct broadcast_buffer broadcast_agent;
	int listenfd ,connfd ,i=0 ,j=0;
	int shm_id;
	int pipefd[2];
	int pipe_num=0;
	int child_return;
	int nfds;
	int pid;
	int do_fork;
	int close_fifo_fd ,unlink_fifo;
	int space_count = 0;
	char *line_end;
	char sendbuff[1024],recebuff[11000];
	char fifo_path[50];
	char origin_msg[11000];
	char **command_s;
	char *env_got;
	char end_line[]="end of line";
	char fifo_unlink_path[50];
	signal(SIGUSR1 ,handle_signal1);
	signal(SIGUSR2 ,handle_signal2);
	signal(SIGCHLD ,handle_child_termi);
	signal(SIGINT ,term_parent);

	temp = shm_id = shmget(SHMKEY ,30*sizeof(struct user_info) ,IPC_CREAT | IPC_EXCL | 0666);
	printf("shm id = %d\n",shm_id);
	user_list = (struct user_info*)shmat(shm_id ,(char*)0 ,0);

	init_user_list();

	listenfd = connection_setup();
	if(listenfd == 0)
		return 0;
	printf("listen socket fd is %d\n",listenfd);
	command_s = (char **)malloc(10000*sizeof(char *));

	chdir("../ras");
	clearenv();
	setenv("PATH","bin:.",1);

	while(1)//keep listening
	{
		int len = sizeof(struct sockaddr);
		if((connfd = accept(listenfd,(struct sockaddr *)&user_addr,&len)) < 0)
		{
			printf("connect() call error\n");
			return 0;
		}
		current_usr_id = add_usr();//return new user's index
		sig_fd = connfd;
		//user_list[current_usr_id].port = ntohs(user_addr.sin_port);
		//strcpy(user_list[current_usr_id].ip,inet_ntoa(user_addr.sin_addr));
		pid = fork();
		if(pid == 0)//fork a child that connect with client
		{
			close(2);
			dup(connfd);
			close(1);
			dup(connfd);
			close(listenfd);

			signal(SIGCHLD, SIG_DFL);
			queue=get_fdnode();

			bzero(sendbuff,sizeof(sendbuff));
			strcpy(sendbuff,"****************************************\n");
			write(connfd,sendbuff,strlen(sendbuff));

			bzero(sendbuff,sizeof(sendbuff));
			strcpy(sendbuff,"** Welcome to the information server. **\n");
			write(connfd,sendbuff,strlen(sendbuff));

			bzero(sendbuff,sizeof(sendbuff));
			strcpy(sendbuff,"****************************************\n");
			write(connfd,sendbuff,strlen(sendbuff));

			snprintf(broadcast_agent.msg,1024,"*** User '(no name)' entered from %s/%d. ***\n",user_list[current_usr_id].ip,user_list[current_usr_id].port);
			broadcast_agent.need_broadcast = 1;
			broadcast_to_usrs(&broadcast_agent);

			while(1)
			{
				write(connfd,"% ",2);

				bzero(recebuff,sizeof(recebuff));
				read(connfd,recebuff,sizeof(recebuff));

				space_count = 0;
				line_end = strstr(recebuff,"\n");
				if(line_end != NULL)
				{
					*line_end = ' ';
					space_count++;
				}

				line_end = strstr(recebuff,"\r");
				if(line_end != NULL)
				{
					*line_end = ' ';
					space_count++;
				}
				recebuff[strlen(recebuff)-space_count] = '\0';
				strcpy(origin_msg,recebuff);

				if(strrchr(recebuff,'/') != NULL)
				{
					bzero(sendbuff,sizeof(sendbuff));
					strcpy(sendbuff,"/ is not acceptable !\n");
					write(connfd,sendbuff,strlen(sendbuff));
					continue;//keep listening to client
				}

				command_s[0]=strtok(recebuff," ");

				i=1;
				while(1)
				{
					command_s[i]=strtok(NULL," ");
					if(command_s[i]==NULL)
					{
						command_s[i+1]=end_line;//....../NULL/end of line
						break;
					}
					else
						i++;
				}
				if(strcmp(command_s[0],"exit") == 0)
				{
					snprintf(broadcast_agent.msg,1024,"*** User '%s' left. ***\n",user_list[current_usr_id].name);
					broadcast_agent.need_broadcast = 1;
					broadcast_to_usrs(&broadcast_agent);
					clear_user(current_usr_id);
					close(connfd);
					exit(0);
				}
				else if(strcmp(command_s[0],"setenv") == 0)
				{
					if(setenv(command_s[1],command_s[2],1) < 0)
					{
						bzero(sendbuff,sizeof(sendbuff));
						strcpy(sendbuff,"setenv error\n");
						write(connfd,sendbuff,strlen(sendbuff));
						continue;//keep listening to client
					}
					else
					{
						free_temp = queue;
						if(queue->next == NULL)
							queue->next = get_fdnode();//-1 is a special usage
						queue = queue->next;
						free(free_temp);
					}
				}
				else if(strcmp(command_s[0],"printenv") == 0)
				{
					if(command_s[1] == NULL)
					{
						printf("printenv error\n");
						continue;
					}
					env_got = getenv(command_s[1]);
					if(env_got == NULL)
					{
						printf("env does not exist\n");
						continue;
					}
					strcpy(sendbuff ,command_s[1]);
					strcat(sendbuff ,"=");
					strcat(sendbuff ,env_got);
					strcat(sendbuff ,"\n");
					write(connfd ,sendbuff ,strlen(sendbuff));
					free_temp = queue;
					if(queue->next == NULL)
						queue->next = get_fdnode();//-1 is a special usage
					queue = queue->next;
					free(free_temp);
				}
				else if(strcmp(command_s[0],"who") == 0)
				{
					list_user(current_usr_id);
					free_temp = queue;
					if(queue->next == NULL)
						queue->next = get_fdnode();//-1 is a special usage
					queue = queue->next;
					free(free_temp);
				}
				else if(strcmp(command_s[0],"name") == 0)
				{
					if(check_user_name(command_s[1]) < 0)
					{
						snprintf(sendbuff,1024,"*** User '%s' already exists. ***\n",command_s[1]);
						write(connfd,sendbuff,strlen(sendbuff));
					}
					else
					{
						strcpy(user_list[current_usr_id].name,command_s[1]);
						snprintf(broadcast_agent.msg,1024,"*** User from %s/%d is named '%s'. ***\n",user_list[current_usr_id].ip ,user_list[current_usr_id].port ,user_list[current_usr_id].name);
						broadcast_agent.need_broadcast = 1;
						broadcast_to_usrs(&broadcast_agent);
						free_temp = queue;
						if(queue->next == NULL)
							queue->next = get_fdnode();//-1 is a special usage
						queue = queue->next;
						free(free_temp);
					}
				}
				else if(strcmp(command_s[0],"tell") == 0)
				{
					bzero(sendbuff ,sizeof(sendbuff));
					snprintf(sendbuff,1024,"*** %s told you ***: %s\n",user_list[current_usr_id].name ,origin_msg+strlen(command_s[0])+strlen(command_s[1])+2);
					if(tell_usr(atoi(command_s[1])-1 ,sendbuff) <0)//recever active or not?
					{
						bzero(sendbuff ,sizeof(sendbuff));
						snprintf(sendbuff,1024,"*** Error: user #%d does not exist yet. ***\n",atoi(command_s[1]));
						write(connfd ,sendbuff ,strlen(sendbuff));
					}
					else
					{
						free_temp = queue;
						if(queue->next == NULL)
							queue->next = get_fdnode();//-1 is a special usage
						queue = queue->next;
						free(free_temp);
					}
				}
				else if(strcmp(command_s[0],"yell") == 0)
				{
					bzero(sendbuff ,sizeof(sendbuff));
					snprintf(broadcast_agent.msg,1024,"*** %s yelled ***: %s\n",user_list[current_usr_id].name,origin_msg+strlen(command_s[0])+1);
					broadcast_agent.need_broadcast = 1;
					broadcast_to_usrs(&broadcast_agent);
					free_temp = queue;
					if(queue->next == NULL)
						queue->next = get_fdnode();//-1 is a special usage
					queue = queue->next;
					free(free_temp);
				}
				else
				{
					while(command_s[0] != end_line)
					{
						close_fifo_fd = 0;
						unlink_fifo = 0;
						do_fork = 1;
						pipe_num=0;
						j=0;
						while(command_s[j] != NULL)
						{
							if(strrchr(command_s[j],'|') != NULL)// | sysmbol detected
							{
								if(strlen(command_s[j]) == 1)
									pipe_num = 1;
								else
									pipe_num = atoi(command_s[j]+1);
								queue->validate = 1;
								nth_node = get_nth_node(queue,pipe_num);//trace n nodes
								if(nth_node -> validate == 1)//if node validate(exist a pipe to nth command),use this pipe 
								{
									queue->outputfd = nth_node->p_in_fd;
								}
								else//create a pipe to nth command
								{
									nth_node->validate = 1;
									pipe(pipefd);
									nth_node->p_in_fd = pipefd[1];
									nth_node->p_out_fd = pipefd[0];
									nth_node->outputfd = 1;
									queue->outputfd = pipefd[1];//change current fdnode
								}
								command_s[j] = NULL;//flowing are important steps!!!!
								if(command_s[j+1] == NULL)//means command NULL NULL end_line
									command_s[j+1] = end_line;//change to command NULL end_line end_line
								break;
							}
							else if(strrchr(command_s[j],'<') != NULL)
							{
								//printf("pipe from usr #%d\n",atoi(command_s[j]+1));
								if(user_list[current_usr_id].my_inbox[atoi(command_s[j]+1)-1].validate == 1)
								{
									snprintf(broadcast_agent.msg,1024,"*** %s (#%d) just received from %s (#%d) by '%s' ***\n",user_list[current_usr_id].name ,current_usr_id+1 ,user_list[atoi(command_s[j]+1)-1].name ,atoi(command_s[j]+1) ,origin_msg);
									broadcast_agent.need_broadcast = 1;
									queue->validate = 1;
									queue->p_out_fd = user_list[current_usr_id].my_inbox[atoi(command_s[j]+1)-1].fifo_fd;
									user_list[current_usr_id].my_inbox[atoi(command_s[j]+1)-1].validate = 0;
									strcpy(fifo_unlink_path,user_list[current_usr_id].my_inbox[atoi(command_s[j]+1)-1].myFIFO);
									unlink_fifo = 1;
									command_s[j] = NULL;
								}
								else
								{
									bzero(sendbuff ,sizeof(sendbuff));
									snprintf(sendbuff,1024,"*** Error: the pipe #%d->#%d does not exist yet. ***\n",atoi(command_s[j]+1) ,current_usr_id+1);
									write(connfd,sendbuff,strlen(sendbuff));
									command_s[j] = NULL;
									do_fork = 0;
								}
							}
							else if(strrchr(command_s[j],'>') != NULL)
							{//***************need modified
								if(strlen(command_s[j]) == 1)//file dir
								{
									queue->redir_flag = 1;
									command_s[j] = NULL;//replace > By NULL
									j++;//(get file name)
									queue->validate = 1;
									queue->outputfd = open(command_s[j],O_CREAT|O_WRONLY|O_TRUNC,00644);
									if(command_s[j+1] == NULL)//important!! means command >(NULL) file(NULL) NULL 
										command_s[j+1] = end_line;//change to command >(NULL) file(end_line) NULL
									break;//while(command_s[j] != NULL)
								}
								else//pipe to user
								{
									//printf("pipe to usr #%d\n",atoi(command_s[j]+1));
									if(user_list[atoi(command_s[j]+1)-1].active != 0)
									{
										if(user_list[atoi(command_s[j]+1)-1].my_inbox[current_usr_id].validate != 1)
										{
											snprintf(broadcast_agent.msg,1024,"*** %s (#%d) just piped '%s' to %s (#%d) ***\n",user_list[current_usr_id].name ,current_usr_id+1 ,origin_msg ,user_list[atoi(command_s[j]+1)-1].name ,atoi(command_s[j]+1));
											broadcast_agent.need_broadcast = 1;

											int des = atoi(command_s[j]+1)-1;
											bzero(fifo_path ,sizeof(fifo_path));
											snprintf(fifo_path,1024,"../fifos/#%dto#%dfifo",current_usr_id,des);
											strcpy(user_list[des].my_inbox[current_usr_id].myFIFO,fifo_path);
											user_list[des].my_inbox[current_usr_id].validate = 1;
											user_list[des].sig_1_raiser = current_usr_id;
											mknod(fifo_path, S_IFIFO | 0666, 0);
											kill(user_list[des].pid ,SIGUSR1);
											int temp = open(fifo_path,1);

											queue->validate = 1;
											queue->outputfd = temp;
											queue->errofd = temp;

											command_s[j] = NULL;
											close_fifo_fd = 1;
										}
										else
										{
											bzero(sendbuff ,sizeof(sendbuff));
											snprintf(sendbuff,1024,"*** Error: the pipe #%d->#%d already exists. ***\n",current_usr_id+1 ,atoi(command_s[j]+1));
											write(connfd,sendbuff,strlen(sendbuff));
											command_s[j] = NULL;
											do_fork = 0;
										}
									}
									else
									{
										bzero(sendbuff ,sizeof(sendbuff));
										snprintf(sendbuff,1024,"*** Error: user #%d does not exist yet. ***\n",atoi(command_s[j]+1));
										write(connfd,sendbuff,strlen(sendbuff));
										command_s[j] = NULL;
										do_fork = 0;
									}
								}
							}
							j++;
						}
						if(do_fork == 1)
						{
							if(fork() == 0)//fork a child that execute the command
							{
								close(2);
								dup(connfd);
								close(1);
								dup(connfd);

								if(queue->validate == 1)//change fd base on a validate fdnode
								{
									if(queue->p_in_fd != (-1))//close pipe's input ,ready to execute
										close(queue->p_in_fd);
									if(queue->p_out_fd != 0)//change input to pipe_out
									{
										close(0);//in
										dup(queue->p_out_fd);
										close(queue->p_out_fd);
									}
									if(queue->outputfd != 1)//change output to a pipe
									{
										close(1);//out
										dup(queue->outputfd);
										if(queue->errofd != 2)//*******location may not perfect
										{
											close(2);
											dup(queue->errofd);
										}
										close(queue->outputfd);
									}
								}
								execvp(command_s[0],command_s);
								strcpy(sendbuff,"Unknown command: [");
								strcat(sendbuff,command_s[0]);
								strcat(sendbuff,"].\n");
								write(connfd,sendbuff,strlen(sendbuff));
								exit(-1);
							}
							else
							{
								if(queue->validate == 1)
								{
									if(queue->p_in_fd != (-1))
									{//close pipe's input,leave it for child
										close(queue->p_in_fd);
										queue->p_in_fd = -1;//prevent from re-close occur!!!!!!!!
									}
									if(queue->redir_flag != 0)//if output is a file , close it!!!!
										close(queue->outputfd);
								}
								wait(&child_return);
								if(WIFEXITED(child_return) && WEXITSTATUS(child_return) == 255)//child_return != 0
								{
									command_s[j+1] = end_line;
									queue->outputfd = 1;
								}
								else
								{
									broadcast_to_usrs(&broadcast_agent);
									if(queue->validate == 1)//if child success,means now parent can close the pipe
									{
										if(queue->p_out_fd != 0)
										{
											close(queue->p_out_fd);
										}
									}
									if(close_fifo_fd != 0)
									{
										close(queue->outputfd);
									}
									if(unlink_fifo != 0)
									{
										unlink(fifo_unlink_path);
									}
									free_temp = queue;
									if(queue->next == NULL)
										queue->next = get_fdnode();//-1 is a special usage
									queue = queue->next;
									free(free_temp);
								}
							}
						}
						command_s = &(command_s[j+1]);
					}
				}
			}
		}
		else//listener
		{
			user_list[current_usr_id].pid = pid;
			close(connfd);
		}
	}//while(1) keep listening
	shmdt(user_list);
	shmctl(shm_id ,IPC_RMID ,NULL);
	close(listenfd);
	printf("End of my NP server\n");
	return 0;
}

void handle_signal1(int sig)//receive FIFOs,set some fds....
{
	int sender_id = user_list[current_usr_id].sig_1_raiser;
	user_list[current_usr_id].my_inbox[sender_id].fifo_fd = open(user_list[current_usr_id].my_inbox[sender_id].myFIFO,0);
	return;
}

void handle_signal2(int sig)//print something
{
	write(sig_fd ,user_list[current_usr_id].msg,strlen(user_list[current_usr_id].msg));//maybe write to fd = 4 directly?
	return;
}

void handle_child_termi(int sig)
{
	int rv,pid;
	pid = wait(&rv);
	//printf("child %d termincated\n",pid);
	return;
}

void term_parent(int sig)
{
	shmdt(user_list);
	shmctl(temp ,IPC_RMID ,NULL);
	close(3);
	printf("End of my NP server\n");
	exit(0);
}

struct fdnode* get_fdnode(void)
{
	struct fdnode* rc= (struct fdnode*)malloc(sizeof(struct fdnode));
	rc->validate = 0;//0 means default value
	rc->redir_flag = 0;
	rc->p_in_fd = -1;//-1 for special usage
	rc->p_out_fd = 0;
	rc->outputfd = 1;
	rc->errofd = 2;
	rc->next = NULL;
	return rc;
}

struct fdnode* get_nth_node(struct fdnode *head,int n)
{
	//call by value?
	int s;
	struct fdnode *temp = head;
	for(s=0;s<n;s++)
	{
		if(temp->next == NULL)
			temp->next = get_fdnode();//-1 is a special usage
		temp = temp ->next;
	}
	return temp;
}

int connection_setup(void)
{
	int fd;
	struct sockaddr_in serv_addr;

	if((fd = socket(AF_INET , SOCK_STREAM , 0)) < 0)
	{
		printf("socket() call error\n");
		return 0;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(PORT);

	if(bind(fd , (struct sockaddr *) &serv_addr , sizeof(serv_addr)) < 0)
	{
		printf("bind() call error\n");
		return 0;
	}
	if(listen(fd,1) < 0)
	{
		printf("listen() call error\n");
		return 0;
	}
	return fd;
}

void list_user(int caller_id)
{
	char send[1024];
	int i;
	bzero(send,sizeof(send));
	strcpy(send,"<ID>\t<nickname>\t<IP/port>\t<indicate me>\n");
	write(4 ,send,strlen(send));
	for(i=0;i<30;i++)
	{
		if(user_list[i].active != 0)
		{
			bzero(send,sizeof(send));
			if(i == caller_id)
				snprintf(send,1024,"%d\t%-14s\t%s/%d\t<-me\n",i+1 ,user_list[i].name ,user_list[i].ip ,user_list[i].port);//-14s
			else
				snprintf(send,1024,"%d\t%-14s\t%s/%d\n",i+1 ,user_list[i].name ,user_list[i].ip ,user_list[i].port);//-14s
			write(4 ,send ,strlen(send));
		}
	}
	return;
}

int tell_usr(int des_id ,char *msg)
{
	if(user_list[des_id].active == 0)
		return -1;
	else
	{
		bzero(user_list[des_id].msg ,1024);
		strcpy(user_list[des_id].msg,msg);
		kill(user_list[des_id].pid ,SIGUSR2);
		return 1;
	}
}

void broadcast_to_usrs(struct broadcast_buffer *input)
{
	int j;
	if(input->need_broadcast == 1)
	{
		for(j=0;j<30;j++)
		{
			if(user_list[j].active != 0)
			{
				bzero(user_list[j].msg ,1024);
				strcpy(user_list[j].msg,input->msg);
				kill(user_list[j].pid ,SIGUSR2);
			}
		}
		input->need_broadcast = 0;
		bzero(input->msg ,sizeof(input->msg));
	}
	return;
}

void init_user_list(void)
{
	int i ,j;
	for(i=0;i<30;i++)
	{
		user_list[i].active = 0;
		user_list[i].port = 511;
		user_list[i].pid = 0;
		user_list[i].sig_1_raiser = 0;
		strcpy(user_list[i].name,"(no name)");
		strcpy(user_list[i].ip,"CGILAB");
		for(j=0;j<29;j++)
		{
			user_list[i].my_inbox[j].validate = 0;
		}
	}
	return;
}

int add_usr(void)//return a unused user node's index
{
	int k;
	for(k=0;k<30;k++)
	{
		if(user_list[k].active == 0)
		{
			user_list[k].active = 1;
			return k;
		}
	}
}

void clear_user(int index)
{
	int j;
	user_list[index].active = 0;
	strcpy(user_list[index].name,"(no name)");
	for(j=0;j<29;j++)
	{//****************************************************double check close******@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
		if(user_list[index].my_inbox[j].validate != 0)
		{
			user_list[index].my_inbox[j].validate = 0;
			close(user_list[index].my_inbox[j].fifo_fd);
			unlink(user_list[index].my_inbox[j].myFIFO);
		}
	}
	for(j=0;j<29;j++)
	{//****************************************************double check close******@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
		if(user_list[j].my_inbox[index].validate != 0)
		{
			user_list[j].my_inbox[index].validate = 0;
			close(user_list[j].my_inbox[index].fifo_fd);
			unlink(user_list[j].my_inbox[index].myFIFO);
		}
	}
	return;
}

int check_user_name(char *name)
{
	int i;
	for(i=0;i<30;i++)
	{
		if(user_list[i].active != 0)
			if(strcmp(user_list[i].name ,name) == 0)
				return -1;
	}
	return 0;
}