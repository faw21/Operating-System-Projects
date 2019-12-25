#ifndef _SEM_
#define _SEM_

/*
 *	CS1550 Project 1
 *	Fangzheng Wu
 *	faw21
 */
//semaphore struct
struct cs1550_sem 
{
	int value;		//value contained within the counting semaphore
	struct Node *head;
	struct Node *tail;
};
#endif