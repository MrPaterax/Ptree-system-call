#include <linux/prinfo.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/string.h>

static struct task_struct *get_root(int root_pid)
{
	if (root_pid == 0) 
	       	return &init_task;
	
	return find_task_by_vpid(root_pid);
}
struct task_node
{
	struct task_struct *data;
	struct list_head list_node;
	int level;
};
void setinfo(struct prinfo *cur_prinfo, struct task_node *curr_node){
        cur_prinfo -> parent_pid = curr_node -> data -> parent -> pid;
	cur_prinfo -> pid = curr_node -> data -> pid;
	cur_prinfo -> uid = curr_node -> data -> cred -> uid.val;
        strcpy(cur_prinfo -> comm, curr_node -> data -> comm);
	cur_prinfo -> level = curr_node -> level;
}


SYSCALL_DEFINE3(ptree, struct prinfo __user *, buf, int __user*, nr, int, root_pid)
{
	int numofentries;
	int i;
	int index;
	int entries_read;
	struct task_node* list_of_tn;
	struct prinfo *cur_prinfo;
	struct task_node* cur_node;
	struct task_node* temp;
	struct task_node* current_node;
	struct task_node* newnode;
	struct list_head* pos;
	struct task_struct* task;
	struct prinfo* bufcpy;

	bufcpy = NULL;
	
	if (copy_from_user(&numofentries, nr, sizeof(int))) {
		printk("fail at copy from user for numofentries\n");
		return -EFAULT;
	}
	if(numofentries < 1){ 
		printk("numofentries is less than 1 or null");
		return -EINVAL;
	}

	bufcpy = kmalloc(sizeof(struct prinfo) * numofentries, GFP_KERNEL);

	if (copy_from_user(bufcpy, buf, sizeof(struct prinfo) * numofentries)){
		printk("fail at copy from user for buf\n");
		return -EFAULT;
	}
	if(bufcpy == NULL){
		printk("buf is null");
		return -EINVAL;
	}
	kfree(bufcpy);
	
	list_of_tn = kmalloc(sizeof(struct task_node), GFP_KERNEL);
	temp = NULL;
	rcu_read_lock();
	list_of_tn -> data = get_root(root_pid);
	if(list_of_tn -> data == NULL){
		printk("couldn't find process");
		return -ESRCH;
	}
	rcu_read_unlock();
	list_of_tn -> level = 0;
	INIT_LIST_HEAD(&(list_of_tn->list_node));
	for(i = 1; i < numofentries; i++){
		temp = kmalloc(sizeof(struct task_node), GFP_KERNEL);
		temp-> data = NULL;
		list_add_tail(&(temp -> list_node), &(list_of_tn -> list_node));
	}

	current_node = list_of_tn;
	//current is the current node in front of the queue that we use when traversing to find task struct children
	newnode = list_next_entry(current_node, list_node);
       	//newnode is the node we will assign data to when we have the current task struct 
	pos = NULL;
	task = NULL;
	entries_read = 1;
	printk("check1 clear: kmalloc worked and was able to create a list\n");

	rcu_read_lock();
	while(current_node -> data != NULL && entries_read < numofentries){
		list_for_each(pos, &(current_node->data->children)) {
			if(entries_read >= numofentries) 
				break;
			task = list_entry(pos, struct task_struct, sibling);
			newnode -> data = task; 
			newnode -> level = current_node -> level + 1;
			newnode = list_next_entry(newnode, list_node);
			entries_read++;
			
		}
		current_node = list_next_entry(current_node, list_node);
	}
	rcu_read_unlock();
	printk("check 2 clear: able to lock and get task structs to put into list\n");

	if(copy_to_user(nr, &entries_read, sizeof(int))){
		printk("couldnt copy to user for entries read");
		return -EFAULT;
	}
	printk("check 3 clear: copy to user\n");
	cur_prinfo = kmalloc(sizeof(struct prinfo), GFP_KERNEL);
	printk("check 4 clear: able to malloc\n");
	setinfo(cur_prinfo, list_of_tn);
	//printk("check 5 clear: able to setinfo\n");
	//first allocation to buffer of prinfos
	if (copy_to_user(&(buf[0]), cur_prinfo, sizeof(struct prinfo))) {
		printk("copy_to_user failled\n");
		return -EFAULT;
	}
	
	cur_node = NULL;
	index = 1;

	//copy to the buffer.
	list_for_each_entry(cur_node, &list_of_tn->list_node, list_node) {
		if(index == numofentries) 
			break;
		if(index == entries_read)
			break;
		if(cur_node -> data == NULL)
			break;
		setinfo(cur_prinfo, cur_node);
	        if (copy_to_user(&(buf[index++]), cur_prinfo, sizeof(struct prinfo))) {
			printk("copy_to_user failled\n");
			return -EFAULT;
		}
	}
	
	temp = NULL; 
	
	//free allocated kmapped memory
	list_for_each_entry_safe(cur_node, temp, &list_of_tn->list_node, list_node) {
		list_del(&cur_node -> list_node);
		kfree(cur_node);
	}
	kfree(cur_prinfo);
	return 0;
}
