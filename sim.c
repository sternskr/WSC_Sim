#include <stdlib.h>
#include <stdio.h>

#define DEFAULT_LATENCY 	10
//#define DEBUG

static int global_time;
static int global_end_time;
static int num_levels;
static int* children_per_level;
static int max_requests;

struct list_node
{
	struct list_node* next;
	void* data;
};

struct request
{
	int r_type; //page view, image view, text post, image post
	int r_data; //number of kilobytes
	int r_start_time; //time request entered warehouse
	int r_end_time; //time request left warehouse
#ifdef DEBUG
	int lists_added_to;
	int num_lat_updates;
	int req_num;
#endif
};

struct net_device
{
	int n_type; //server, non-server
	struct net_device* parent;

	int num_children;
	struct net_device* children;

	int num_in;
	struct list_node* reqs_in;

	int num_out;
	struct list_node* reqs_out;

	int (*latency_fn)(struct request* /*req_in*/,
	                  struct net_device* /*pointer to current device*/,
	                  int /*current load*/);
};


int def_latency(struct request* req, struct net_device* n_dev, int load)
{
#ifdef DEBUG
	printf("def_latency\n");
#endif

	req->r_end_time += rand() % DEFAULT_LATENCY + 1;
#ifdef DEBUG
	req->num_lat_updates++;
	printf("\tend_time = %d, req_num = %d\n", req->r_end_time, req->req_num);
#endif
	return req->r_end_time;
}


//setup
void setup(int argc, char* argv[])
{
#ifdef DEBUG
	printf("setup\n");
#endif
	global_time = 0;
	global_end_time = 10000;
	num_levels = 3;
	children_per_level = calloc(num_levels, sizeof(int));
	children_per_level[0] = 1; //clusters
	children_per_level[1] = 1; //racks
	children_per_level[2] = 1; //servers

	max_requests = 2000;

	srand(0);
}

void make_children(struct net_device* n_dev, int level)
{
#ifdef DEBUG
	printf("make_children, level %d, num_levels = %d\n", level, num_levels);
#endif
	if (level == num_levels)
		return;

	int i = 0;
	n_dev->children = calloc(children_per_level[level], sizeof(struct net_device));
	if (n_dev->children == NULL)
	{
		printf("failed to allocate memory for children at level %d, aborting\n", level);
		return;
	}
	n_dev->num_children = children_per_level[level];

	for (i = 0; i < n_dev->num_children; i++)
	{
		make_children(&n_dev->children[i], level + 1);
		n_dev->children[i].parent = n_dev;
		n_dev->children[i].latency_fn = def_latency;
	}
}

struct net_device* generate_architecture()
{
#ifdef DEBUG
	printf("generate_architecture\n");
#endif
	struct net_device* top_dev = calloc(1, sizeof(struct net_device));

	top_dev->latency_fn = def_latency;

	//recursively populate tree with devices
	make_children(top_dev, 0);
	return top_dev;
}

struct request* generate_requests()
{
#ifdef DEBUG
	printf("generate_requests\n");
#endif
	struct request* req_list = calloc(max_requests, sizeof(struct request));
	if (req_list == NULL)
	{
		printf("failed to allocate memory for req_list, aborting\n");
		return NULL;
	}

	int i = 0;
	for (i = 0; i < max_requests; i++)
	{
#ifdef DEBUG
		req_list[i].req_num = i;
#endif
		req_list[i].r_start_time = rand() % (global_end_time / 2);
		req_list[i].r_end_time = req_list[i].r_start_time;
	}

	return req_list;
}

//end setup


//sim
void add_to_queue(struct request* req, struct list_node** list_loc)
{
#ifdef DEBUG
	printf("add to queue\n");
#endif

	//struct list_node** list_head = list_loc;
	struct list_node* temp = *(list_loc);
	struct list_node* to_add = calloc(1, sizeof(struct list_node));
	if (!to_add)
		printf("allocation failed\n");

	to_add->next = NULL;
	to_add->data = req;

	if (temp == NULL)
	{
		*(list_loc) = to_add;
	}
	else
	{
		while (temp->next != NULL)
		{
			temp = temp->next;
		}
		temp->next = to_add;
	}
#ifdef DEBUG
	req->lists_added_to++;
	printf("add to queue end\n");
#endif
}

struct net_device* find_min_child(struct net_device* n_dev)
{
#ifdef DEBUG
	printf("find_min_child\n");
#endif
	if (n_dev->children == NULL)
	{
#ifdef DEBUG
		printf("no children found\n");
#endif

		return NULL;
	}
#ifdef DEBUG
	printf("found children\n");
#endif

	int i = 0;
	struct net_device* min = &n_dev->children[0];

	for (i = 0; i < n_dev->num_children; ++i)
	{
		if (n_dev->children[i].num_in < min->num_in)
		{
			min = &n_dev->children[i];
		}
	}
	return min;
}

void update(struct net_device* n_dev)
{
#ifdef DEBUG
	printf("update\n");
#endif
	int i = 0;

	//update all of our children first
	for (i = 0; i < n_dev->num_children; ++i)
	{
#ifdef DEBUG
	printf("updating child %d\n", i);
#endif
		update(&n_dev->children[i]);
	}

	//if we have inbound requests (top->down)
	if (n_dev->reqs_in != NULL)
	{
#ifdef DEBUG
	printf("update: handling inbound\n");
#endif
		//get the first one from the list
		struct request* req = ((struct request*)n_dev->reqs_in->data);
#ifdef DEBUG
		printf("req_num = %d\n", req->req_num);
#endif
		//if it's completed by now, remove the node from the list (but we keep the req pointer)
		if (req->r_end_time <= global_time)
		{
#ifdef DEBUG
	printf("update: handling inbound: request completed\n");
#endif
			struct list_node* temp = n_dev->reqs_in;
			n_dev->reqs_in = n_dev->reqs_in->next;
			free(temp);
			n_dev->num_in--;

			//if we have children, decide who to give it to based on who has the least inbound requests
			struct net_device* next_dest = find_min_child(n_dev);

			//if we don't have children (then we are a server), add it to our outbound queue
			if (next_dest == NULL)
			{
				add_to_queue(req, &n_dev->reqs_out);
				n_dev->num_out++;
#ifdef DEBUG
	printf("update: handling inbound: server move to output\n");
#endif
				n_dev->latency_fn(req, n_dev, n_dev->num_in);
			}
			else
			{
				add_to_queue(req, &next_dest->reqs_in);
				next_dest->num_in++;
#ifdef DEBUG
	printf("update: handling inbound: device hand to child\n");
#endif
				next_dest->latency_fn(req, next_dest, next_dest->num_in);
			}
		}
		//else just wait
	}

	//if we have outbound requests (bottom->up)
	if (n_dev->reqs_out != NULL)
	{
#ifdef DEBUG
	printf("update: handling outbound\n");
#endif
		//get the first one from the list
		struct request* req = ((struct request*)n_dev->reqs_out->data);

		//if it's completed by now, remove the node from the list (but we keep the req pointer)
		if (req->r_end_time <= global_time)
		{
#ifdef DEBUG
	printf("update: handling outbound: request completed\n");
#endif
			struct list_node* temp = n_dev->reqs_out;
			n_dev->reqs_out = n_dev->reqs_out->next;
			free(temp);
			n_dev->num_out--;


			//if we have a parent (i.e. we're not the top-level device)
			if (n_dev->parent != NULL)
			{
				add_to_queue(req, &n_dev->parent->reqs_out);
				n_dev->parent->num_out++;
				n_dev->parent->latency_fn(req, n_dev->parent, n_dev->parent->num_out);
			}
		}
		//if we're the top-level device, we're done tracking this request
		req = NULL;
	}
}

//sim

void print_results(struct request* req_list)
{
	if (req_list == NULL)
	{
		printf("invalid list, sim failed\n");
		return;
	}
#ifdef DEBUG
	printf("print_results\n");
#endif
	int incomplete = 0;
	int latency = req_list[0].r_end_time - req_list[0].r_start_time;
	int max_lat = latency;
	long long avg_lat = 0;
	//int nf_lat = latency;

	int i = 0;
	for (i = 0; i < max_requests; i++)
	{
#ifdef DEBUG
	printf("i = %d, ", i);
#endif
		latency = req_list[i].r_end_time - req_list[i].r_start_time;
#ifdef DEBUG
	printf("latency = %d\n", latency);
			printf("latency = %d, req# = %d, start = %d, end = %d, lists = %d, updates = %d\n", 
				latency, req_list[i].req_num, req_list[i].r_start_time, req_list[i].r_end_time, 
				req_list[i].lists_added_to, req_list[i].num_lat_updates);
#endif

		if (req_list[i].r_end_time > global_end_time)
		{
			printf("latency = %d, req# = %d, start = %d, end = %d\n", latency, i,
			       req_list[i].r_start_time, req_list[i].r_end_time);
			incomplete++;
			continue;
		}

		avg_lat += latency;

		if (latency > max_lat)
			max_lat = latency;
	}

	avg_lat /= (max_requests - incomplete);

	printf("\n\n\nresults:\n");
	printf("sim time: %d\n", global_end_time);
	printf("num requests in: %d\n", max_requests);
	printf("num incomplete requests: %d\n", incomplete);
	printf("maximum latency: %d\n", max_lat);
	printf("average latency: %lld\n", avg_lat);
	//printf("0.95 latency: %d\n", nf_lat);
	printf("\n\n\n");
}

void cleanup(struct net_device* n_dev)
{
#ifdef DEBUG
	printf("cleanup\n");
#endif
	if (n_dev == NULL)
		return;

	int i = 0;
	//recursively free children and their data
	for (i = 0; i < n_dev->num_children; i++)
	{
		cleanup(&n_dev->children[i]);
	}
	free(n_dev->children);

	while (n_dev->reqs_in != NULL)
	{
		struct list_node* temp = n_dev->reqs_in;
		n_dev->reqs_in = n_dev->reqs_in->next;
		free(temp);
	}
	while (n_dev->reqs_out != NULL)
	{
		struct list_node* temp = n_dev->reqs_out;
		n_dev->reqs_out = n_dev->reqs_out->next;
		free(temp);
	}
}

int main(int argc, char* argv[])
{
	setup(argc, argv);

	//create the architecture
	struct net_device* arch_input = generate_architecture();

	//create the requests
	struct request* req_list = generate_requests();

	int i = 0;
	for (i = 0; i < max_requests; i++)
	{
		add_to_queue(&req_list[i], &arch_input->reqs_in);
		arch_input->latency_fn(&req_list[i], arch_input, arch_input->num_in);
	}
	arch_input->num_in = max_requests;

	while (global_time < global_end_time)
	{
		update(arch_input);
		global_time++;
	}

	//do calculations and output to user
	print_results(req_list);

	//free allocated memory
	cleanup(arch_input);

	free(req_list);
	return 0;
}
