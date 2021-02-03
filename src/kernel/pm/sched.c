/*
 * Copyright(C) 2011-2016 Pedro H. Penna   <pedrohenriquepenna@gmail.com>
 *              2015-2016 Davidson Francis <davidsondfgl@hotmail.com>
 *
 * This file is part of Nanvix.
 *
 * Nanvix is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nanvix is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Nanvix. If not, see <http://www.gnu.org/licenses/>.
 */

#include <nanvix/clock.h>
#include <nanvix/const.h>
#include <nanvix/klib.h>
#include <nanvix/hal.h>
#include <nanvix/pm.h>
#include <signal.h>


PUBLIC void copyProc(struct process *src, struct process *dst){
	int i;
	dst->kesp = src->kesp;
	dst->cr3 = src->cr3;
	dst->intlvl = src->intlvl;
	dst->flags = src->flags;
	dst->received = src->received;
	dst->kstack = src->kstack;
	dst->restorer = src->restorer;
	for (i = 0; i < NR_SIGNALS; i++)
		dst->handlers[i] = src->handlers[i];
	dst->irqlvl = src->irqlvl;
	dst->fss = src->fss;
	dst->pgdir = src->pgdir;
	for (i = 0; i < NR_PREGIONS; i++)
		dst->pregs[i].reg = src->pregs[i].reg;
	dst->size = src->size;
	for (i = 0; i < NR_PREGIONS; i++)
		dst->ofiles[i] = src->ofiles[i];
	dst->pwd = src->pwd;
	dst->root = src->root;
	for(i = 0; i < OPEN_MAX; i++)
		dst->ofiles[i] = src->ofiles[i];
	dst->close = src->close;
	dst->umask = src->umask;
	dst->tty = src->tty;
	dst->status = src->status;
	dst->errno = src->errno;
	dst->nchildren = src->nchildren;
	dst->uid = src->uid;
	dst->euid = src->euid;
	dst->suid = src->suid;
	dst->gid = src->gid;
	dst->egid = src->egid;
	dst->sgid = src->sgid;
	dst->pid = src->pid;
	dst->pgrp = src->pgrp;
	dst->father = src->father;
	kstrncpy(dst->name, src->name, NAME_MAX);
	dst->utime = src->utime;
	dst->ktime = src->ktime;
	dst->cutime = src->cutime;
	dst->cktime = src->cktime;
	dst->state = src->state;
	dst->counter = src->counter;
	dst->priority = src->priority;
	dst->nice = src->nice;
	dst->alarm = src->alarm;
	dst->next = src->next;
	dst->chain = src->chain;
}

PUBLIC void changeQueue(struct process *proc, int from, int to){
	struct process* p;

	for (int j = 0; j < PROC_MAX; j++){
		p = &queues[to][j];
		if (!IS_VALID(p)){
			struct process *tmp = p;
			p = proc;
			proc = tmp;
			break;
		}
	}

	switch(from){
		case FIRST_QUEUE:
			nq1--;
			break;
		case SECOND_QUEUE:
			nq2--;
			break;
		default:
			nq3--;
	}

	switch(to){
		case SECOND_QUEUE:
			nq2++;
			break;
		case THIRD_QUEUE:
			nq3++;
			break;
		default:
			nq4++;
	}

	proc->flags = 0;
	proc->state = PROC_DEAD;
	
}

/**
 * @brief Schedules a process to execution.
 * 
 * @param proc Process to be scheduled.
 */
PUBLIC void sched(struct process *proc)
{
	proc->state = PROC_READY;
	proc->counter = 0;
}

/**
 * @brief Stops the current running process.
 */
PUBLIC void stop(void)
{
	curr_proc->state = PROC_STOPPED;
	sndsig(curr_proc->father, SIGCHLD);
	yield();
}

/**
 * @brief Resumes a process.
 * 
 * @param proc Process to be resumed.
 * 
 * @note The process must stopped to be resumed.
 */
PUBLIC void resume(struct process *proc)
{	
	/* Resume only if process has stopped. */
	if (proc->state == PROC_STOPPED)
		sched(proc);
}

/**
 * @brief Yields the processor.
 */
PUBLIC void yield(void)
{
	struct process *p;    /* Working process.     */
	struct process *next; /* Next process to run. */
	int queue, j;

	/* Re-schedule process for execution. */
	if (curr_proc->state == PROC_RUNNING)
		sched(curr_proc);

	/* Remember this process. */
	last_proc = curr_proc;

	/* Check alarm. */
	for(queue = 0; queue < 4; queue++)
		for (j = 0; j < PROC_MAX; j++){
			p = &queues[queue][j];
			/* Skip invalid processes. */
			if (!IS_VALID(p))
				continue;
			
			/* Alarm has expired. */
			if ((p->alarm) && (p->alarm < ticks))
				p->alarm = 0, sndsig(p, SIGALRM);
		}

	/* Choose a process to run next. */
	next = IDLE;

	/* Updating queue */
	for(queue = 0; queue < 4; queue++)
		for (j = 0; j < PROC_MAX; j++){
			p = &queues[queue][j];

			if(p->state != PROC_READY)
				continue;

			switch(queue){
				case FIRST_QUEUE:
					nq1++;
					break;
				case SECOND_QUEUE:
					nq2++;
					break;
				case THIRD_QUEUE:
					nq3++;
					break;
				default:
					nq4++;
					break;
			}

			/*
				Update the process queue position
			*/
			if (queue == 0 && j == 0)
				continue;
				
			if (p->ktime + p->utime > 1000 && queue < FOURTH_QUEUE)
				changeQueue(p, queue, FOURTH_QUEUE);
			else if (p->ktime + p->utime > 500 && queue < THIRD_QUEUE)
				changeQueue(p, queue, THIRD_QUEUE);
			else if (p->ktime + p->utime > 250 && queue < SECOND_QUEUE)
				changeQueue(p, queue, SECOND_QUEUE);
		}
	queue = FIRST_QUEUE;
	/*
		Looking for the most prioritary non-empty queue 
	*/
	if (nq1 > 1)
		queue = FIRST_QUEUE;
	else if (nq2 != 0)
		queue = SECOND_QUEUE;
	else if (nq3 != 0)
		queue = THIRD_QUEUE;
	else if (nq4 != 0)
		queue = FOURTH_QUEUE;

	/* Choose a process in the queue */
	for (j = 0; j < PROC_MAX; j++){
		p = &queues[queue][j];
		/* Skip non-ready process. */
		if (p->state != PROC_READY)
			continue;

		/*
		 * Process with higher
		 * waiting time found.
		 */
		if (p->counter > next->counter)
		{
			next->counter++;
			next = p;
		}
			
		/*
		 * Increment waiting
		 * time of process.
		 */
		else
			p->counter++;
	}
	
	/* Switch to next process. */
	next->priority = PRIO_USER;
	next->state = PROC_RUNNING;
	next->counter = PROC_QUANTUM;
	if (curr_proc != next)
		switch_to(next);
}
