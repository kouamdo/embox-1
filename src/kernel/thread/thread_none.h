/*
 * thread_none.h
 *
 *  Created on: Dec 18, 2018
 *      Author: anton
 */

#ifndef SRC_KERNEL_THREAD_THREAD_NONE_H_
#define SRC_KERNEL_THREAD_THREAD_NONE_H_

struct task;

struct thread {
	struct task *task;
};

#endif /* SRC_KERNEL_THREAD_THREAD_NONE_H_ */
