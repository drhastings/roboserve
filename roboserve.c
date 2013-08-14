#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "utility.h"
#include "roboserve.h"

#define CHILD_TYPE_TEXT 0
#define CHILD_TYPE_NODE 1
#define CHILD_TYPE_EMPTY 2

char * script = "function update(name) {"
"var form = document.createElement(\"form\");"
"form.setAttribute(\"method\", \"post\");"
"form.setAttribute(\"action\", \"tunings/update\");"
"var hiddenField = document.createElement(\"input\");"
"hiddenField.setAttribute(\"type\", \"hidden\");"
"hiddenField.setAttribute(\"name\", \"text_color\");"
"hiddenField.setAttribute(\"value\", \"red\");"
"form.appendChild(hiddenField);"
"document.body.appendChild(form);"
"form.submit();"
"}";

char * script2 = "function update(name)\n"
"{\n"
"var xmlhttp;\n"
"	xmlhttp=new XMLHttpRequest();\n"
"xmlhttp.onreadystatechange=function()\n"
"	{\n"
"		if (xmlhttp.readyState==4 && xmlhttp.status==200)\n"
"		{\n"
"			var vars = xmlhttp.responseText.split('&');\n"
"			for (var i = 0; i < vars.length; i++) {\n"
"				var pair = vars[i].split('=');\n"
"				var element	= null;\n"
"				element = document.getElementById(pair[0]);\n"
"				console.log(pair[0]);\n"
"				if (element)\n"
"				{\n"
"					element.value = pair[1].trim();\n"
"					console.log(pair[1]);\n"
"				}\n"
"			}\n"
"		}\n"
"	}\n"
"var body = \"\";\n"
"var input = document.getElementById(name);\n"
"if (input.name)\n"
"	{\n"
"		body += input.name + \"=\" + input.value + \"&\";\n"
"	}\n"
"xmlhttp.open(\"POST\",\"tunings/update\",true);\n"
"xmlhttp.send(body);\n"
"}\n"
"function update_all()\n"
"{\n"
"var xmlhttp;\n"
"	xmlhttp=new XMLHttpRequest();\n"
"xmlhttp.onreadystatechange=function()\n"
"	{\n"
"		if (xmlhttp.readyState==4 && xmlhttp.status==200)\n"
"		{\n"
"			var vars = xmlhttp.responseText.split('&');\n"
"			for (var i = 0; i < vars.length; i++) {\n"
"				var pair = vars[i].split('=');\n"
"				var element	= null;\n"
"				element = document.getElementById(pair[0]);\n"
"				console.log(pair[0]);\n"
"				if (element)\n"
"				{\n"
"					element.value = pair[1].trim();\n"
"					console.log(pair[1]);\n"
"				}\n"
"			}\n"
"		}\n"
"	}\n"
"var body = \"\";\n"
"var form = document.getElementById(\"the_form\");\n"
"for (i = 0; i < form.length; i++)\n"
"{\n"
"	if (form.elements[i].name)\n"
"	{\n"
"		body += form.elements[i].name + \"=\" + form.elements[i].value + \"&\";\n"
"	}\n"
"}\n;"
"xmlhttp.open(\"POST\",\"tunings/update\",true);\n"
"xmlhttp.send(body);\n"
"}\n";


// Class declarations

struct page
{
	unsigned int written;

	struct page *next;

	struct page *prev;

	char *buffer;
};

struct message_var
{
	char *name;

	void *var_value;

	void (*transfer_in)(char *in, void *out);

	void (*transfer_out)(void *in, char *out);

	struct message_var *next;
};

struct tag_attribute
{
	struct tag_attribute *next_attribute;

	char * name;

	char * value;

	struct message_var * var;
};

struct child_list_item;

struct node
{
	struct node *parent;

	struct child_list_item *children;

	char *type;

	struct tag_attribute *attributes;
};

struct text
{
	struct node * parent;

	struct message_var * var;

	char * text;
};

union child
{
	struct node *node;

	struct text *text;
};

struct child_list_item
{
	union child the_node;

	int type;

	struct child_list_item *the_next;
};

struct header
{
	char *status_line;

	struct tag_attribute *header_lines;
};

struct message
{
	struct header *header;

	struct node *message_body;

	struct node *the_form;

	struct message *next_one;

	struct message_var *variables;

	char name[25];
};

struct serve_task
{
	struct server * server;

	int sockfd;

	pthread_mutex_t mutex;

	pthread_cond_t connected;
};

//Constructors

struct header * new_header(char *status_line)
{
	struct header *header;

	header = (struct header *)malloc(sizeof(struct header));

	if (!header)
		return NULL;

	header->status_line = (char *)malloc(strlen(status_line) + 1);

	if (!header->status_line)
		return NULL;

	memset(header->status_line, 0, strlen(status_line) + 1);

	strcpy(header->status_line, status_line);

	header->header_lines = NULL;

	return header;
}

struct message_var * new_message_var(char * name, void * var_value)
{
	struct message_var *var;
	int length = 0;

	var = (struct message_var *)malloc(sizeof(struct message_var));

	if (!var)
		return NULL;

	length = strlen(name) + 1;

	var->name = (char *)malloc(length);

	if (!var->name)
		return NULL;

	memset(var->name, 0, length);

	strcpy(var->name, name);

	var->var_value = var_value;

	var->next = NULL;

	var->transfer_in = NULL;

	var->transfer_out = NULL;
}

struct node *new_node(char *type, struct node *parent);

struct tag_attribute *add_attribute(struct node *the_node,
																		char * name, char * value);

struct node *add_node_child(struct node *the_node, char * type);

struct message *new_message(char *name)
{
	struct message *message;
	struct node *tree_walk;

	message = (struct message *)malloc(sizeof(struct message));

	memset(message->name, 0, 25);

	strcpy(message->name, name);

	if (!message)
		return NULL;

	message->header = new_header("HTTP/1.x 200 OK");

	message->message_body = new_node("html", NULL);

	tree_walk = add_node_child(message->message_body, "head");

	tree_walk = add_node_child(tree_walk, "script");

	add_text_child(tree_walk, script2);

	tree_walk = tree_walk->parent->parent;

	tree_walk = add_node_child(tree_walk, "body");

	message->the_form = add_node_child(tree_walk, "form");

	add_attribute(message->the_form, "id", "the_form");

	add_attribute(message->the_form, "onsubmit", "update_all(); return false;");

	tree_walk = add_node_child(message->the_form, "input");

	add_attribute(tree_walk, "type", "submit");

	message->variables = NULL;

	message->next_one = NULL;

	return message;
}

struct server * new_server()
{
	struct server *server;
	int value = 1;

	server = (struct server *)malloc(sizeof(struct server));

	if (!server)
		return NULL;

	server->pages = new_message("Home");

	add_node_child(server->pages->message_body, "head");
	struct node * my_node = add_node_child(server->pages->message_body, "body");
	add_text_child(add_node_child(my_node, "p"), "piserve");;
	my_node = add_node_child(my_node, "p");

	server->portno = 8000;
	getcwd(server->serve_dir, 128);

	return server;
}

void *accept_connections(void * server);

int start_server(struct server * server)
{
	int value = 1;

	server->sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (server->sockfd < 0)
		return -1;

	bzero((char *) &server->serv_addr, sizeof(server->serv_addr));

	server->serv_addr.sin_family = AF_INET;
	server->serv_addr.sin_port = htons(server->portno);
	server->serv_addr.sin_addr.s_addr = INADDR_ANY;

	setsockopt(server->sockfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));

	if (bind(server->sockfd, (struct sockaddr *) &server->serv_addr,
				sizeof(server->serv_addr)) < 0)
		return -1;

	pthread_create(&server->listen_thread, NULL, accept_connections,
									(void *)server);

	return 0;
}

struct message_var * get_var_by_name(char *name, struct message * message)
{
	struct message_var * ret = NULL;
	struct message_var * var = message->variables;

	while (var)
	{
		if (!strcmp(name, var->name))
			ret = var;
		var = var->next;
	}
	return ret;
}

struct message * get_message_by_name(char *name, struct server * server)
{
	struct message * ret = NULL;
	struct message * message = server->pages;

	while (message)
	{
		if (!strcmp(name, message->name))
		{
			ret = message;
		}
		message = message->next_one;
	}
	
	return ret;
}

struct child_list_item * new_child_list_item()
{
	struct child_list_item *item;

	item = (struct child_list_item *)malloc(sizeof(struct child_list_item));

	if (!item)
		return NULL;

	item->type = CHILD_TYPE_EMPTY;

	item->the_next = NULL;

	item->the_node.node = NULL;

	return item;
}

struct child_list_item *new_text_item(char *text)
{
	struct child_list_item *item;

	item = new_child_list_item();

	if (!item)
		return NULL;

	item->type = CHILD_TYPE_TEXT;

	item->the_node.text = (struct text *)malloc(sizeof(struct text));

	if (!item->the_node.text)
		return NULL;

	item->the_node.text->parent = NULL;

	item->the_node.text->var = NULL;

	item->the_node.text->text = (char *)malloc(strlen(text) + 1);

	if (!item->the_node.text)
		return NULL;

	memset(item->the_node.text->text, 0, strlen(text) + 1);

	strcpy(item->the_node.text->text, text);

	return item;
}

struct node *new_node(char *type, struct node *parent)
{
	struct node *new;
	int length = 0;

	new = (struct node *)malloc(sizeof(struct node));

	if (!new)
		return NULL;

	new->parent = new;
	
	if (parent)
		new->parent = parent;

	new->children = NULL;

	length = strlen(type) + 1;

	new->type = (char *)malloc(length);

	if (!new->type)
		return NULL;

	memset(new->type, 0, length);

	strcpy(new->type, type);

	new->attributes = NULL;

	return new;
}

struct child_list_item *new_node_item(char *type, struct node *parent)
{
	struct child_list_item *item;

	item = new_child_list_item();

	if (!item)
		return NULL;

	item->type = CHILD_TYPE_NODE;

	item->the_node.node = new_node(type, parent);

	return item;
}

struct tag_attribute *new_tag_attribute(const char * name, const char * value)
{
	struct tag_attribute *attribute;
	int length = 0;

	attribute = (struct tag_attribute *)malloc(sizeof(struct tag_attribute));

	if (!attribute)
		return NULL;

	length = strlen(name) + 1;

	attribute->name = (char *)malloc(length);

	if (!attribute->name)
		return NULL;

	memset(attribute->name, 0, length);

	strcpy(attribute->name, name);

	length = strlen(value) + 1;

	attribute->value = (char *)malloc(length);

	if (!attribute->value)
		return NULL;

	memset(attribute->value, 0, length);

	strcpy(attribute->value, value);

	attribute->var = NULL;

	attribute->next_attribute = NULL;

	return attribute;
}

int make_tag_vary(struct tag_attribute * tag, struct message_var * var,
										void (*transfer_func)(void * in, char* out))
{
	tag->var = var;

	var->transfer_out = transfer_func;
}

struct message_var * add_var(struct message * message, char * name, void *value)
{
	struct message_var * var = new_message_var(name, value);

	if (message->variables)
		var->next = message->variables;

	message->variables = var;

	return var;
}

int add_text_child(struct node *the_node, char * text)
{
	struct child_list_item *item = new_text_item(text);

	if (!item)
		return -1;

	item->the_node.text->parent = the_node;

	if (the_node->children)
	{
		struct child_list_item *current_item = the_node->children;

		while (current_item->the_next)
		{
			current_item = current_item->the_next;
		}
		current_item->the_next = item;
	}
	else
	{
		the_node->children = item;
	}

	return 0;
}

struct node *add_node_child(struct node *the_node, char * type)
{
	struct child_list_item *item = new_node_item(type, the_node);

	if (!item)
		return NULL;

	if (the_node->children)
	{
		struct child_list_item *current_item = the_node->children;

		while (current_item->the_next)
		{
			current_item = current_item->the_next;
		}
		current_item->the_next = item;
	}
	else
	{
		the_node->children = item;
	}

	return item->the_node.node;
}

struct message *add_message(struct server *server, char * name)
{
	struct message * message;

	message = new_message(name);

	if (!message)
		return NULL;

	message->next_one = server->pages;

	server->pages = message;

	return message;
}

struct tag_attribute *add_attribute(struct node *the_node,
																		char * name, char * value)
{
	struct tag_attribute *attribute = NULL;

	attribute = new_tag_attribute(name, value);

	if (!attribute)
		return NULL;

	if (the_node->attributes)
		attribute->next_attribute = the_node->attributes;

	the_node->attributes = attribute;

	return attribute;
}

int add_header_line(struct header *header, char * name, char * value)
{
	struct tag_attribute *attribute = NULL;

	attribute = new_tag_attribute(name, value);

	if (!attribute)
		return -1;

	if (header->header_lines)
	{
		struct tag_attribute *current_line = header->header_lines;

		while (current_line->next_attribute)
		{
			current_line = current_line->next_attribute;
		}
		current_line->next_attribute = attribute;
	}
	else
	{
		header->header_lines = attribute;
	}

	return 0;
}

struct page *new_page()
{
	struct page *page;

	page = (struct page *)malloc(sizeof(struct page));

	if (!page)
		return NULL;

	page->written = 0;

	page->next = page;

	page->prev = page;

	page->buffer = (char *)malloc(PAGE_SIZE);

	if (!page->buffer)
		return NULL;

	memset(page->buffer, 0, PAGE_SIZE);

	return page;
}

//Static functions

static void __free_tag_attributes(struct tag_attribute *tag)
{
	if (tag)
	{
		while (tag->next_attribute)
		{
			struct tag_attribute *temp = tag->next_attribute;

			free(temp->name);
			free(temp->value);

			tag->next_attribute = temp->next_attribute;
			free(temp);
		}
		free(tag->name);
		free(tag->value);
		free(tag);
	}
}

void __free_vars(struct message_var* var)
{
	struct message_var *current_var = var;

	while(var)
	{
		struct message_var *temp = var->next;

		free(var->name);

		free(var);

		var = temp;
	}
}

static void __free_header(struct header *header)
{
	if (header->header_lines)
		__free_tag_attributes(header->header_lines);

	if (header->status_line)
		free(header->status_line);
}

static void __free_child_list(struct node *node)
{
	while (node->children)
	{
		struct child_list_item *temp = node->children;

		if (temp->the_node.text)
			free(temp->the_node.text);

		node->children = temp->the_next;
		free(temp);
	}
}

static void __free_node(struct node * the_node)
{
	free(the_node->type);

	if (the_node->attributes)
	{
		__free_tag_attributes(the_node->attributes);
	}

	__free_child_list(the_node);

	free(the_node);
}

static void __free_message(struct message * message)
{
	if (message->header)
		__free_header(message->header);

	if (message->message_body)
		__free_node(message->message_body);

	__free_vars(message->variables);

	while (message->next_one)
	{
		message->next_one = message->next_one->next_one;

		__free_message(message->next_one);

	}
}

static void __free_page(struct page * page)
{
	page->next->prev = page->prev;
	page->prev->next = page->next;

	free(page->buffer);
	free(page);
}

static void __free_pages(struct page * page)
{
	while (page->next != page)
	{
		__free_page(page->next);
	}

	__free_page(page);
}

static int __copy_to_page(struct page * page, char *buffer, int *poffset,
															int length)
{
	struct page *current_page = page;

	int offset = *poffset;

	*poffset = offset + length;

	while (length)
	{
		if (offset > PAGE_SIZE - 1)
		{
			if (current_page->next == page)
			{
				struct page *new = new_page();

				if (!new)
					return -1;

				new->prev = current_page;	
				new->next = current_page->next;
				current_page->next->prev = new;
				current_page->next = new;

				current_page->written = PAGE_SIZE;

				current_page = new;
			}
			else
			{
				current_page = current_page->next;
			}
			offset -= PAGE_SIZE;
		}
		else
		{
			if (length > PAGE_SIZE - current_page->written)
			{
				struct page *new = new_page();

				if (!new)
					return -1;

				new->prev = current_page;	
				new->next = current_page->next;
				current_page->next->prev = new;
				current_page->next = new;

				memcpy(current_page->buffer + offset, buffer,
								PAGE_SIZE - current_page->written);
				length -= PAGE_SIZE - current_page->written;
				current_page->written = PAGE_SIZE;
				current_page = new;
				offset = 0;
			}
			else
			{
				memcpy(current_page->buffer + offset, buffer, length);
				current_page->written += length;
				length -= length;
			}
		}
	}
				
	return 0;
}

static int __print_pages(struct page * page)
{
	struct page * current_page = page;

	do
	{
		fwrite(current_page->buffer, page->written, 1, stdout);

		current_page = current_page->next;
	} while (current_page != page);

	return 0;
}

static int __send_pages(struct page * page, int sockfd)
{
	struct page * current_page = page;

	do
	{
		send(sockfd, current_page->buffer, page->written, 0);

		current_page = current_page->next;
	} while (current_page != page);

	return 0;
}

static int __send_file(int file, int sockfd)
{
	struct page *header_page, *content_page;
	struct header *header = new_header("HTTP/1.1 200 OK");
	char buffer[256];
	struct stat my_stat;
	char size[16];

	int offset = 0;

	header_page = new_page();

	fstat(file, &my_stat);

	sprintf(size, "%i", my_stat.st_size);

	add_header_line(header, "Content-Length", size);

	copy_header(header, header_page, &offset);
	__copy_to_page(header_page, "\r\n", &offset, 2);

	__send_pages(header_page, sockfd);

	sendfile(sockfd, file, NULL, my_stat.st_size);

	__free_pages(header_page);
	__free_header(header);

	return 0;
}

static int __send_vars(struct message * message, int sockfd)
{
	struct page *header_page, *content_page;
	struct header *header = new_header("HTTP/1.1 200 OK");
	struct message_var * var = message->variables;
	char buffer[32];
	char eof = EOF;

	int offset = 0;

	content_page = new_page();

	while (var)
	{
		__copy_to_page(content_page, var->name, &offset, strlen(var->name));
		__copy_to_page(content_page, "=", &offset, 1);
		var->transfer_out(var->var_value, buffer);
		__copy_to_page(content_page, buffer, &offset, strlen(buffer));
		__copy_to_page(content_page, "&", &offset, 1);
		var = var->next;
	}

	offset--;

	__copy_to_page(content_page, "\r\n\r\n", &offset, 4);

	sprintf(buffer, "%i", offset);

	add_header_line(header, "Content-Length", buffer);
	add_header_line(header, "Content-Type", "text/html");

	offset = 0;

	header_page = new_page();

	copy_header(header, header_page, &offset);
	__copy_to_page(header_page, "\r\n", &offset, 2);

	__send_pages(header_page, sockfd);
	__send_pages(content_page, sockfd);

	__free_pages(header_page);
	__free_pages(content_page);
	__free_header(header);

	return 0;
}

static int __send_message(struct message * message, int sockfd)
{
	struct page *header_page, *content_page;
	struct header *header = new_header("HTTP/1.1 200 OK");
	char buffer[32];
	char eof = EOF;

	int offset = 0;

	content_page = new_page();

	copy_node(message->message_body, content_page, &offset);
	__copy_to_page(content_page, "\r\n", &offset, 2);

	sprintf(buffer, "%i", offset);

	add_header_line(header, "Content-Length", buffer);
	add_header_line(header, "Content-Type", "text/html");

	offset = 0;

	header_page = new_page();

	copy_header(header, header_page, &offset);
	__copy_to_page(header_page, "\r\n", &offset, 2);

	__send_pages(header_page, sockfd);
	__send_pages(content_page, sockfd);

	__free_pages(header_page);
	__free_pages(content_page);
	__free_header(header);

	return 0;
}

int copy_node_type(struct node *the_node, struct page *page, int *offset)
{
	__copy_to_page(page, the_node->type, offset, strlen(the_node->type));
}

int copy_node_attributes(struct node *the_node, struct page *page, int *offset)
{
	struct tag_attribute *current_attribute = the_node->attributes;

	while (current_attribute)
	{
		__copy_to_page(page, " ", offset, 1);
		__copy_to_page(page, current_attribute->name, offset,
			 strlen(current_attribute->name));
		__copy_to_page(page, "=\"", offset, 2);

		if (current_attribute->var)
		{
			free(current_attribute->value);

			current_attribute->value = (char *)malloc(128);

			current_attribute->var->transfer_out(current_attribute->var->var_value,
																				current_attribute->value);
		}
		

		__copy_to_page(page, current_attribute->value, offset,
			 strlen(current_attribute->value));

		__copy_to_page(page, "\"", offset, 1);

		current_attribute = current_attribute->next_attribute;
	}

	return 0;
}

int copy_header_lines(struct header *header, struct page *page, int *offset)
{
	struct tag_attribute *current_attribute = header->header_lines;

	while (current_attribute)
	{
		__copy_to_page(page, current_attribute->name, offset,
			 strlen(current_attribute->name));
		__copy_to_page(page, ": ", offset, 2);
		__copy_to_page(page, current_attribute->value, offset,
			 strlen(current_attribute->value));

		__copy_to_page(page, "\n", offset, 1);

		current_attribute = current_attribute->next_attribute;
	}

	return 0;
}	

int copy_node(struct node *the_node, struct page *page,	int *offset);

int copy_text(char *the_text, struct page *page, int *offset)
{
	__copy_to_page(page, the_text, offset, strlen(the_text));

	return 0;
}

int copy_child(struct child_list_item *item, struct page *page, int *offset)
{
	if (item->type == CHILD_TYPE_TEXT)
		if (item->the_node.text->var)
		{

		}
		else
			copy_text(item->the_node.text->text, page, offset);

	if (item->type == CHILD_TYPE_NODE)
		copy_node(item->the_node.node, page, offset);

	return 0;
}

int copy_children(struct child_list_item *items, struct page *page, int *offset)
{
	while (items)
	{
		copy_child(items, page, offset);

		items = items->the_next;
	}

	return 0;
}

int copy_node(struct node *the_node, struct page *page,	int *offset)
{
	__copy_to_page(page, "<", offset, 1);
	copy_node_type(the_node, page, offset);
	copy_node_attributes(the_node, page, offset);
	__copy_to_page(page, ">", offset, 1);

	copy_children(the_node->children, page, offset);

	__copy_to_page(page, "</", offset, 2);
	copy_node_type(the_node, page, offset);
	__copy_to_page(page, ">\n", offset, 2);

	return 0;
}

int copy_header(struct header *header, struct page *page,	int *offset)
{
	copy_text(header->status_line, page, offset);
	__copy_to_page(page, "\n", offset, 1);
	copy_header_lines(header, page, offset);

	return 0;
}

void update_var_from_querystring(struct message * message, char *begin, char * buffer)
{
	char tag_name[32];
	char tag_value[128];
	char * stop;

	while (begin && begin - buffer < strlen(buffer))
	{
		memset(tag_name, 0, 32);
		memset(tag_value, 0, 128);

		stop = NULL;

		stop = strchr(begin, '=');
		if (stop && begin < buffer + strlen(buffer))
		{
			begin++;

			memcpy(tag_name, begin, stop - begin);

			stop = NULL;

			stop = strchr(begin, '&');

			begin = begin + strlen(tag_name) + 1;

			if (stop)
			{
				memcpy(tag_value, begin, stop - begin);
			}
			else
			{
				strcpy(tag_value, begin);
			}
			begin = begin + strlen(tag_value);
		}
		else
		{
			break;
		}
		if (message)
		{
			struct message_var *my_var = get_var_by_name(tag_name, message);

			if (my_var)
				if (my_var->transfer_in)
					my_var->transfer_in(tag_value, my_var->var_value);
		}
	}
}


void *serve_message(void * serve_task)
{
	struct serve_task * task = (struct serve_task *)serve_task;
	int message_length = -1;
	char *buffer = (char *)malloc(PAGE_SIZE);
	char *begin, *stop, to_copy;
	char *incoming = (char *)malloc(PAGE_SIZE);
	char file_name[128];
	char message_name[32];
	char command[16];

	char *white_spaceplus = " \t\r\n?/";
	char *white_space =  " \t\n\r";

	pthread_mutex_lock (&task->mutex);

	pthread_cond_wait(&task->connected, &task->mutex);

	pthread_mutex_unlock(&task->mutex);

	while (message_length)
	{
		message_length = 0;
		char *line, token;

		memset(buffer, 0, PAGE_SIZE);
		memset(incoming, 0, PAGE_SIZE);
		memset(file_name, 0, 128);
		memset(message_name, 0, 128);
		memset(command, 0, 32);
		message_length = read(task->sockfd, incoming, PAGE_SIZE);

		begin = incoming;
		stop = NULL;
		to_copy = 15;

		if (to_copy > message_length)
			to_copy = message_length;

		stop = strpbrk(incoming, white_space);
		if (stop - begin < 16 && stop)
			to_copy = stop - begin;

		memcpy(command, incoming, to_copy);

		if (begin < incoming + message_length)
		{
			begin = incoming + strlen(command) + 1;

			begin += strspn(begin, white_space);

			stop = strpbrk(begin, white_space);
		}
		else
			begin = incoming;

		if (stop > incoming + message_length)
			stop = incoming + message_length;

	  if (stop)
			memcpy(buffer, begin, stop - begin);

		begin = strchr(buffer, '?');

		if (begin)
			memcpy(file_name, buffer, begin - buffer);
		else
			strcpy(file_name, buffer);

		stop = strchr(file_name + 1, '/');

		if (stop)
			memcpy(message_name, file_name, stop - file_name);
		else
			strcpy(message_name, file_name);

		struct message * my_message;
		my_message = get_message_by_name(message_name + 1, task->server);

		struct stat my_stat;

		char name[256];

		strcpy(name, task->server->serve_dir);
		strcat(name, file_name);

		begin = strchr(buffer, '?');	

		stat(name, &my_stat);

		if (!strcmp(command, "GET"))
		{
			if (my_message)
			{
				update_var_from_querystring(my_message, begin, buffer);
				__send_message(my_message, task->sockfd);
			}
			else if (S_ISREG(my_stat.st_mode))
			{
				int fd = open(name, 0);

				__send_file(fd, task->sockfd);
				close(fd);
			}
			else
			{
				struct message * message = new_message("404");

				add_text_child(message->message_body, "404");

				__send_message(message, task->sockfd);
				__free_message(message);
			}
		}
		else if (!strcmp(command, "POST"))
		{
			//printf("post ");

			if(my_message)
			{
				char * body = strstr(incoming, "\r\n\r\n");

				//printf("%s ", my_message->name);
				if (body)
				{
					char *start = body + 3;

					//printf("%s\n", start);

					update_var_from_querystring(my_message, start, body);
				}
				if (!strcmp(file_name, message_name))
				{
					__send_message(my_message, task->sockfd);
				}
				else 
				{
					char * sub = file_name + strlen(message_name);					

					if (!strcmp(sub, "/update"))
					{
						__send_vars(my_message, task->sockfd);
				
						//printf("sending vars\n");
					}
					else
					{
						struct message * message = new_message("404");

						add_text_child(message->message_body, "404");

						__send_message(message, task->sockfd);
						//printf("sending 404\n");
						__free_message(message);
					}
				}
			}
		}
	}

	free(task);

	free(buffer);
	free(incoming);
}

void *accept_connections(void * server)
{
	struct server * the_server = (struct server *) server;
	int newsockfd, clilen;
	struct sockaddr_in cli_addr;
	pthread_t serve_thread;

	listen(the_server->sockfd, 5);

	while(1)
	{
		struct serve_task * task = (struct serve_task *)malloc(
																							sizeof(struct serve_task));
		clilen = sizeof(cli_addr);

		task->server = the_server;

		pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
		task->mutex = mutex;
	
		pthread_cond_t connected = PTHREAD_COND_INITIALIZER;
		task->connected = connected;

		pthread_create(&serve_thread, NULL, serve_message, (void *) task);

		newsockfd = accept(the_server->sockfd,
												(struct sockaddr *) &cli_addr, &clilen);

		if (newsockfd < 0)
			break;

		task->sockfd = newsockfd;

		pthread_mutex_lock(&task->mutex);

		pthread_cond_signal(&task->connected);

		pthread_mutex_unlock(&task->mutex);
	}

	return NULL;
}

void add_int_box(struct message *message, char *name, int * variable)
{
	struct node * tree_walk = message->the_form;
	struct message_var *var = add_var(message, name, variable);
	char javascript[64] = "update('";

	strcat(javascript, name);

	strcat(javascript, "')");

	void copy_string(void * in, char * out)
	{
		sprintf(out, "%i", *(int *)in);
	}

	void copy_string2(char * in, void * out)
	{
		float input;
		char * end;

		input = atoi(in);

		*(int *)out = input;
	}

	add_text_child(message->the_form, name);

	tree_walk = add_node_child(message->the_form, "input");

	add_attribute(tree_walk, "type", "text");

	add_attribute(tree_walk, "name", name);

	add_attribute(tree_walk, "id", name);

//	add_attribute(tree_walk, "onblur", javascript);

	struct tag_attribute *my_tag = add_attribute(tree_walk, "value", "");

	make_tag_vary(my_tag, var, copy_string);

	var->transfer_in = copy_string2;
}
void add_float_box(struct message *message, char *name, float * variable)
{
	struct node * tree_walk = message->the_form;
	struct message_var *var = add_var(message, name, variable);
	char javascript[64] = "update('";

	strcat(javascript, name);

	strcat(javascript, "')");

	void copy_string(void * in, char * out)
	{
		sprintf(out, "%f", *(float *)in);
	}

	void copy_string2(char * in, void * out)
	{
		float input;
		char * end;

		input = strtod(in, &end);

		*(float *)out = input;
	}

	add_text_child(message->the_form, name);

	tree_walk = add_node_child(message->the_form, "input");

	add_attribute(tree_walk, "type", "text");

	add_attribute(tree_walk, "name", name);

	add_attribute(tree_walk, "id", name);

//	add_attribute(tree_walk, "onblur", javascript);

	struct tag_attribute *my_tag = add_attribute(tree_walk, "value", "");

	make_tag_vary(my_tag, var, copy_string);

	var->transfer_in = copy_string2;
}

void add_string_box(struct message *message, char *name, char * variable)
{
	struct node * tree_walk = message->the_form;
	struct message_var *var = add_var(message, name, variable);
	char javascript[64] = "update('";

	strcat(javascript, name);

	strcat(javascript, "')");

	void copy_string(void * in, char * out)
	{
		strcpy(out, (char *)in);
	}

	void copy_string2(char * in, void * out)
	{
		strcpy((char *) out, in);
	}

	add_text_child(message->the_form, name);

	tree_walk = add_node_child(message->the_form, "input");

	add_attribute(tree_walk, "type", "text");

	add_attribute(tree_walk, "name", name);

	add_attribute(tree_walk, "id", name);

//	add_attribute(tree_walk, "onblur", javascript);

	struct tag_attribute *my_tag = add_attribute(tree_walk, "value", "");

	make_tag_vary(my_tag, var, copy_string);

	var->transfer_in = copy_string2;
}
