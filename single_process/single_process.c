#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>


#include "func.h"

#define PORT 5001

int main(void)
{
	struct sockaddr_in user_addr;
	struct fdnode *free_temp ,*nth_node ,*queue;
	struct user_info user_list[30];
	int listenfd ,connfd ,i=0 ,j=0 ,current_usr_id;
	int pipefd[2];
	int pipe_num=0;
	int child_return;
	int nfds;
	int do_fork;
	int need_brocast;
	int space_count = 0;
	char sendbuff[1024],recebuff[11000];
	char origin_msg[11000] ,broadcast_buffer[1024];
	char **command_s;
	char *env_got;
	char *line_end;
	char end_line[]="end of line";
	fd_set rfds;//ready socket
	fd_set afds;//socket list

	init_user_list(user_list);

	listenfd = connection_setup();
	if(listenfd == 0)
		return 0;
	printf("listen socket fd is %d\n",listenfd);
	command_s = (char **)malloc(10000*sizeof(char *));

	chdir("../ras");
	nfds = getdtablesize();
	FD_ZERO(&afds);
	FD_SET(listenfd,&afds);

	while(1)//keep select(listening)
	{
		memcpy(&rfds,&afds,sizeof(rfds));
		if(select(nfds ,&rfds ,(fd_set*)0 ,(fd_set*)0 ,(struct timeval*)0 ) <0 )
			printf("errror");
		if(FD_ISSET(listenfd,&rfds))
		{
			int new_fd;
			int len = sizeof(struct sockaddr);
			if((new_fd = accept(listenfd,(struct sockaddr *)&user_addr,&len)) < 0)
			{
				printf("connect() call error\n");
			}
			FD_SET(new_fd,&afds);
			current_usr_id = add_usr(user_list);
			user_list[current_usr_id].fd = new_fd;
			//user_list[current_usr_id].port = ntohs(user_addr.sin_port);
			//strcpy(user_list[current_usr_id].ip,inet_ntoa(user_addr.sin_addr));

			bzero(sendbuff,sizeof(sendbuff));
			strcpy(sendbuff,"****************************************\n** Welcome to the information server. **\n****************************************\n");
			write(new_fd,sendbuff,strlen(sendbuff));

			bzero(sendbuff,sizeof(sendbuff));
			snprintf(sendbuff,1024,"*** User '(no name)' entered from %s/%d. ***\n",user_list[current_usr_id].ip,user_list[current_usr_id].port);
			broadcast(user_list,sendbuff);

			bzero(sendbuff,sizeof(sendbuff));
			strcpy(sendbuff,"% ");
			write(new_fd,sendbuff,strlen(sendbuff));
			continue;//keep select
		}
		for(connfd=0;connfd<nfds;connfd++)
		{
			if(FD_ISSET(connfd,&rfds) == 1)
				break;
		}
		//****************user profile loading*******************************************//
		current_usr_id = search_user_by_fd(user_list ,connfd);
		queue = user_list[current_usr_id].usrqueue;
		setenv("PATH",user_list[current_usr_id].path,1);//change PATH to usr's path
		//****************user profile loading*******************************************//
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
			continue;//keep listening
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
			snprintf(sendbuff,1024,"*** User '%s' left. ***\n",user_list[current_usr_id].name);
			broadcast(user_list ,sendbuff);
			clear_user(user_list ,current_usr_id);
			FD_CLR(connfd,&afds);
			close(connfd);
			continue;//continue select
		}
		else if(strcmp(command_s[0],"setenv") == 0)
		{
			if(setenv(command_s[1],command_s[2],1) < 0)//***********************may delete,when load user's file,setenv
			{
				bzero(sendbuff,sizeof(sendbuff));
				strcpy(sendbuff,"setenv error\n");
				write(connfd,sendbuff,strlen(sendbuff));
				continue;
			}
			if(strcmp(command_s[1],"PATH") == 0)
				strcpy(user_list[current_usr_id].path,command_s[2]);
			free_temp = queue;
			if(queue->next == NULL)
				queue->next = get_fdnode();//-1 is a special usage
			queue = queue->next;
			user_list[current_usr_id].usrqueue = queue;
			free(free_temp);
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
			user_list[current_usr_id].usrqueue = queue;
			free(free_temp);
		}
		else if(strcmp(command_s[0],"who") == 0)
		{
			list_user(user_list ,current_usr_id);
			free_temp = queue;
			if(queue->next == NULL)
				queue->next = get_fdnode();//-1 is a special usage
			queue = queue->next;
			user_list[current_usr_id].usrqueue = queue;
			free(free_temp);
		}
		else if(strcmp(command_s[0],"name") == 0)
		{
			if(check_user_name(user_list ,command_s[1]) < 0)
			{
				snprintf(sendbuff,1024,"*** User '%s' already exists. ***\n",command_s[1]);
				write(connfd,sendbuff,strlen(sendbuff));
			}
			else
			{
				strcpy(user_list[current_usr_id].name,command_s[1]);
				bzero(sendbuff ,sizeof(sendbuff));
				snprintf(sendbuff,1024,"*** User from %s/%d is named '%s'. ***\n",user_list[current_usr_id].ip ,user_list[current_usr_id].port ,user_list[current_usr_id].name);
				broadcast(user_list ,sendbuff);
				free_temp = queue;
				if(queue->next == NULL)
					queue->next = get_fdnode();//-1 is a special usage
				queue = queue->next;
				user_list[current_usr_id].usrqueue = queue;
				free(free_temp);
			}
		}
		else if(strcmp(command_s[0],"tell") == 0)
		{
			bzero(sendbuff ,sizeof(sendbuff));
			snprintf(sendbuff,1024,"*** %s told you ***: %s\n",user_list[current_usr_id].name ,origin_msg+strlen(command_s[0])+strlen(command_s[1])+2);
			if(tell_usr(user_list ,atoi(command_s[1]) ,sendbuff) <0)
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
				user_list[current_usr_id].usrqueue = queue;
				free(free_temp);
			}
		}
		else if(strcmp(command_s[0],"yell") == 0)
		{
			bzero(sendbuff ,sizeof(sendbuff));
			snprintf(sendbuff,1024,"*** %s yelled ***: %s\n",user_list[current_usr_id].name,origin_msg+strlen(command_s[0])+1);
			broadcast(user_list ,sendbuff);
			free_temp = queue;
			if(queue->next == NULL)
				queue->next = get_fdnode();//-1 is a special usage
			queue = queue->next;
			user_list[current_usr_id].usrqueue = queue;
			free(free_temp);
		}
		else
		{
			while(command_s[0] != end_line)
			{
				do_fork = 1;
				j=0;
				pipe_num=0;
				need_brocast = 0;
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
						if(user_list[current_usr_id].my_inbox[atoi(command_s[j]+1)-1].validate == 1)
						{
							bzero(sendbuff ,sizeof(sendbuff));
							snprintf(sendbuff,1024,"*** %s (#%d) just received from %s (#%d) by '%s' ***\n",user_list[current_usr_id].name ,current_usr_id+1 ,user_list[atoi(command_s[j]+1)-1].name ,atoi(command_s[j]+1) ,origin_msg);
							strcat(broadcast_buffer,sendbuff);
							need_brocast = 1;
							queue->validate = 1;
							queue->p_out_fd = user_list[current_usr_id].my_inbox[atoi(command_s[j]+1)-1].pipe_out_fd;
							queue->p_in_fd = user_list[current_usr_id].my_inbox[atoi(command_s[j]+1)-1].pipe_in_fd;
							user_list[current_usr_id].my_inbox[atoi(command_s[j]+1)-1].validate = 0;
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
							if(user_list[atoi(command_s[j]+1)-1].active != 0)
							{
								if(user_list[atoi(command_s[j]+1)-1].my_inbox[current_usr_id].validate != 1)
								{
									bzero(sendbuff ,sizeof(sendbuff));
									snprintf(sendbuff,1024,"*** %s (#%d) just piped '%s' to %s (#%d) ***\n",user_list[current_usr_id].name ,current_usr_id+1 ,origin_msg ,user_list[atoi(command_s[j]+1)-1].name ,atoi(command_s[j]+1));
									strcat(broadcast_buffer,sendbuff);
									need_brocast = 1;
									pipe(pipefd);
									user_list[atoi(command_s[j]+1)-1].my_inbox[current_usr_id].validate = 1;
									user_list[atoi(command_s[j]+1)-1].my_inbox[current_usr_id].pipe_out_fd = pipefd[0];
									user_list[atoi(command_s[j]+1)-1].my_inbox[current_usr_id].pipe_in_fd = pipefd[1];
									queue->validate = 1;
									queue->outputfd = pipefd[1];
									queue->errofd = pipefd[1];
									command_s[j] = NULL;
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
								snprintf(sendbuff,1024,"*** Error: user #%d does not exist yet. ***\n",atoi(command_s[j]+1));
								write(connfd,sendbuff,strlen(sendbuff));
								bzero(sendbuff ,sizeof(sendbuff));
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
						close(listenfd);

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
									dup(queue->outputfd);
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
										
						//if(WEXITSTATUS(child_return) != 0)//child_return != 0
						if(WIFEXITED(child_return) && WEXITSTATUS(child_return) == 255)
						{
							command_s[j+1] = end_line;
							queue->outputfd = 1;
						}
						else
						{
							if(queue->validate == 1)//if child success,means now parent can close the pipe
							{
								if(queue->p_out_fd != 0)
								{
									close(queue->p_out_fd);
								}
							}
							//command_s = &(command_s[j+1]);
							free_temp = queue;
							if(queue->next == NULL)
								queue->next = get_fdnode();//-1 is a special usage
							queue = queue->next;
							user_list[current_usr_id].usrqueue = queue;
							free(free_temp);
						}
					}
				}
				command_s = &(command_s[j+1]);
				if(need_brocast != 0)
				{
					broadcast(user_list ,broadcast_buffer);
					bzero(broadcast_buffer ,sizeof(broadcast_buffer));
				}
			}
		}
		write(connfd,"% ",2);
	}//while(1) keep listening
	close(listenfd);
	printf("End of my NP server\n");
	return 0;
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

void list_user(struct user_info *list ,int caller_id)
{
	char send[1024];
	int i;
	bzero(send,sizeof(send));
	strcpy(send,"<ID>\t<nickname>\t<IP/port>\t<indicate me>\n");
	write(list[caller_id].fd ,send,strlen(send));
	for(i=0;i<30;i++)
	{
		if(list[i].active != 0)
		{
			bzero(send,sizeof(send));
			if(i == caller_id)
				snprintf(send,1024,"%d\t%-14s\t%s/%d\t<-me\n",i+1 ,list[i].name ,list[i].ip ,list[i].port);//-14s
			else
				snprintf(send,1024,"%d\t%-14s\t%s/%d\n",i+1 ,list[i].name ,list[i].ip ,list[i].port);//-14s
			write(list[caller_id].fd ,send ,strlen(send));
		}
	}
	return;
}

int tell_usr(struct user_info *list ,int des_id ,char *msg)
{
	if(list[des_id-1].active == 0)
		return -1;
	else
	{
		write(list[des_id-1].fd ,msg ,strlen(msg));
		return 1;
	}
}

void broadcast(struct user_info *list ,char *msg)
{
	int j;
	for(j=0;j<30;j++)
	{
		if(list[j].active != 0)
		{
			write(list[j].fd,msg ,strlen(msg));
		}
	}
	return;
}

void init_user_list(struct user_info* list)
{
	int i ,j;
	for(i=0;i<30;i++)
	{
		list[i].active = 0;
		for(j=0;j<29;j++)
		{
			list[i].my_inbox[j].validate = 0;
		}
		list[i].port = 511;
		strcpy(list[i].name,"(no name)");
		strcpy(list[i].path,"bin:.");
		strcpy(list[i].ip,"CGILAB");
		list[i].usrqueue = get_fdnode();
	}
	return;
}

int add_usr(struct user_info* list)//return a unused user node's index
{
	int k;
	for(k=0;k<30;k++)
	{
		if(list[k].active == 0)
		{
			list[k].active = 1;
			return k;
		}
	}
}

int search_user_by_fd(struct user_info* list ,int fd)
{
	int i;
	for(i=0;i<30;i++)
	{
		if(list[i].fd == fd)
			return i;
	}
}

void clear_user(struct user_info* list ,int index)
{
	int j;
	struct fdnode *temp;
	list[index].active = 0;
	strcpy(list[index].name,"(no name)");
	strcpy(list[index].path,"bin:.");
	while(list[index].usrqueue != NULL)
	{
		temp = list[index].usrqueue;
		list[index].usrqueue = list[index].usrqueue->next;
		free(temp);
	}
	list[index].usrqueue = get_fdnode();
	for(j=0;j<29;j++)
	{
		if(list[index].my_inbox[j].validate != 0)
		{
			list[index].my_inbox[j].validate = 0;
			close(list[index].my_inbox[j].pipe_out_fd);
			close(list[index].my_inbox[j].pipe_in_fd);
		}
		
	}
	for(j=0;j<29;j++)
	{
		if(list[j].my_inbox[index].validate != 0)
		{
			list[j].my_inbox[index].validate = 0;
			close(list[j].my_inbox[index].pipe_out_fd);
			close(list[j].my_inbox[index].pipe_in_fd);
		}
		
	}
	return;
}

int check_user_name(struct user_info* list ,char *name)
{
	int i;
	for(i=0;i<30;i++)
	{
		if(list[i].active != 0)
			if(strcmp(list[i].name ,name) == 0)
				return -1;
	}
	return 0;
}