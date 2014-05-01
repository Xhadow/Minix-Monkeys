/* This file contains the scheduling policy for SCHED
 *
 * The entry points are:
 *   do_noquantum:        Called on behalf of process' that run out of quantum
 *   do_start_scheduling  Request to start scheduling a proc
 *   do_stop_scheduling   Request to stop scheduling a proc
 *   do_nice		  Request to change the nice level on a proc
 *   init_scheduling      Called from main.c to set up/prepare scheduling
 */
#include "sched.h"
#include "schedproc.h"
#include <assert.h>
#include <minix/com.h>
#include <minix/syslib.h>
#include <machine/archtypes.h>
#include <stdio.h>
#include "kernel/proc.h" /* for queue constants */

PRIVATE timer_t sched_timer;
PRIVATE unsigned balance_timeout;
PRIVATE unsigned winning_proc_nr;

#define BALANCE_TIMEOUT	5 /* how often to balance queues in seconds */

FORWARD _PROTOTYPE( int schedule_process, (struct schedproc * rmp)	);
FORWARD _PROTOTYPE( void balance_queues, (struct timer *tp)		);

#define DEFAULT_USER_TIME_SLICE 200

/*===========================================================================*
 *                            is_user_process                                *
 *===========================================================================*/
 /*Function that we made in order to check if a process is a user process and not a kernel one. 
 We do this by checking if the priority is in the correct boundaries  */

PUBLIC int is_user_process(struct schedproc* rmp)
{
	return ((rmp->priority <= MIN_USER_Q) && (rmp->priority >= MAX_USER_Q));
}

/*===========================================================================*
 *				do_noquantum				     *
 *===========================================================================*/

PUBLIC int do_noquantum(message *m_ptr)
{
	register struct schedproc* rmp;
	register struct schedproc* rmp2;
	int rv, proc_nr_n;

	if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",
		m_ptr->m_source);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
/*Code to first check if this is a user process that is running. If it is then it sees if it is in the winnign queue. 
If it is it is given more tickets so it will have more time to run, otherwise it will take tickets if it is in the loser
queue */
	if (is_user_process(rmp)) {
		printf("Do NoQ  {P : %3d, T : %3d, E : %5d}\n",
			rmp->priority, rmp->num_tickets, rmp->endpoint);

		if(rmp->priority == MAX_USER_Q) {
			if(rmp->num_tickets > 1)
				rmp->num_tickets -= 1;
		} else {
			rmp2 = &schedproc[winning_proc_nr];

			if(rmp2->num_tickets < rmp2->max_tickets)
				rmp2->num_tickets += 1;
		}

		rmp->priority = USER_Q;
	} else if(rmp->priority < (MAX_USER_Q - 1)) {
		rmp->priority += 1; /* lower priority */
	}

	if ((rv = schedule_process(rmp)) != OK) {
		return rv;
	}
/*Call of our lottery function at the end of do_noquantum so a new lottery can be held and a new winner will be chosen
sincethe last one is completed */
	if((rv = do_lottery()) != OK) {
		return rv;
	}

	return OK;
}

/*===========================================================================*
 *				do_stop_scheduling			     *
 *===========================================================================*/
PUBLIC int do_stop_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	rmp->flags = 0; /*&= ~IN_USE;*/

	if(is_user_process(rmp))
		printf("Stop    {P : %3d, T : %3d, E : %5d}\n",
			rmp->priority, rmp->num_tickets, rmp->endpoint);

	if((rv = do_lottery()) != OK) {
		return rv;
	}

	return OK;
}

/*===========================================================================*
 *				do_start_scheduling			     *
 *===========================================================================*/
PUBLIC int do_start_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n, parent_nr_n, nice;
	
	/* we can handle two kinds of messages here */
	assert(m_ptr->m_type == SCHEDULING_START || 
		m_ptr->m_type == SCHEDULING_INHERIT);

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	/* Resolve endpoint to proc slot. */
	if ((rv = sched_isemtyendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n))
			!= OK) {
		return rv;
	}
	rmp = &schedproc[proc_nr_n];

	/* Populate process slot */
	rmp->endpoint     = m_ptr->SCHEDULING_ENDPOINT;
	rmp->parent       = m_ptr->SCHEDULING_PARENT;

	rmp->max_priority = (unsigned) m_ptr->SCHEDULING_MAXPRIO;
	/* Setting the base ticket specifications */
	rmp->max_tickets  = 20;
	rmp->num_tickets  = 20;

	if (rmp->max_priority >= NR_SCHED_QUEUES) {
		return EINVAL;
	}
	
	switch (m_ptr->m_type) {

	case SCHEDULING_START:
		/* We have a special case here for system processes, for which
		 * quanum and priority are set explicitly rather than inherited 
		 * from the parent */
		rmp->priority   = rmp->max_priority;
		rmp->time_slice = (unsigned) m_ptr->SCHEDULING_QUANTUM;
		break;
		
	case SCHEDULING_INHERIT:
		/* Inherit current priority and time slice from parent. Since there
		 * is currently only one scheduler scheduling the whole system, this
		 * value is local and we assert that the parent endpoint is valid */
		if ((rv = sched_isokendpt(m_ptr->SCHEDULING_PARENT,
				&parent_nr_n)) != OK)
			return rv;

		rmp->priority = USER_Q;
		rmp->time_slice = schedproc[parent_nr_n].time_slice;

		printf("Start   {P : %3d, T : %3d, E : %5d}\n",
			rmp->priority, rmp->num_tickets, rmp->endpoint);

		break;
		
	default: 
		/* not reachable */
		assert(0);
	}

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((rv = sys_schedctl(0, rmp->endpoint, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
			rmp->endpoint, rv);
		return rv;
	}
	rmp->flags = IN_USE;

	/* Schedule the process, giving it some quantum */
	if ((rv = schedule_process(rmp)) != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",
			rv);
		return rv;
	}

	if((rv = do_lottery()) != OK) {
		return rv;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into SCHEDULING_SCHEDULER
	 */

	m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;

	return OK;
}

/*===========================================================================*
 *				do_nice					     *
 *===========================================================================*/
PUBLIC int do_nice(message *m_ptr)
{
	struct schedproc *rmp;
	int rv;
	int proc_nr_n;
	unsigned new_q, old_q, old_max_q, old_num_tickets;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	new_q = (unsigned) m_ptr->SCHEDULING_MAXPRIO;
	if (new_q >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Store old values, in case we need to roll back the changes */
	old_q           = rmp->priority;
	old_max_q       = rmp->max_priority;
	old_num_tickets = rmp->num_tickets;

	/* Update the proc entry and reschedule the process */
	if(is_user_process(rmp)) {
		rmp->max_tickets = new_q;
		rmp->max_tickets =
			(rmp->max_tickets ? (rmp->max_tickets % 100) : 0);
		rmp->num_tickets = rmp->max_tickets;

		printf("Nice    {P : %3d, T : %3d, E : %5d}\n",
			rmp->priority, rmp->num_tickets, rmp->endpoint);
	} else {
		rmp->max_priority = rmp->priority = new_q;
	}

	if ((rv = schedule_process(rmp)) != OK) {
		/* Something went wrong when rescheduling the process, roll
		 * back the changes to proc struct */
		rmp->priority     = old_q;
		rmp->max_priority = old_max_q;
		rmp->num_tickets  = old_num_tickets;
	}

	rv = do_lottery();

	return rv;
}

/*===========================================================================*
 *				schedule_process			     *
 *===========================================================================*/
PRIVATE int schedule_process(struct schedproc * rmp)
{
	int rv;

	if ((rv = sys_schedule(rmp->endpoint, rmp->priority,
			rmp->time_slice)) != OK) {
		printf("SCHED: An error occurred when trying to schedule %d: %d\n",
		rmp->endpoint, rv);
	}

	return rv;
}


/*===========================================================================*
 *				start_scheduling			     *
 *===========================================================================*/

PUBLIC void init_scheduling(void)
{
	u64_t r;

	balance_timeout = BALANCE_TIMEOUT * sys_hz();
	init_timer(&sched_timer);
	set_timer(&sched_timer, balance_timeout, balance_queues, 0);

	read_tsc_64(&r);
	srand((unsigned)r.lo);
}

/*===========================================================================*
 *				balance_queues				     *
 *===========================================================================*/

/* This function in called every 100 ticks to rebalance the queues. The current
 * scheduler bumps processes down one priority when ever they run out of
 * quantum. This function will find all proccesses that have been bumped down,
 * and pulls them back up. This default policy will soon be changed.
 */
PRIVATE void balance_queues(struct timer *tp)
{
	struct schedproc *rmp;
	int proc_nr;
	int rv;

	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if((rmp->flags & IN_USE) && (rmp->priority > rmp->max_priority)
			&& (!is_user_process(rmp))) {
				rmp->priority -= 1; /* increase priority */
				schedule_process(rmp);
		}
	}

	set_timer(&sched_timer, balance_timeout, balance_queues, 0);
}

/*=========================================================================*
 *                              do_lottery                                 *
 *=========================================================================*/
/* Our lottery function will be called every time a process from the winners queue has finished or has become blocked
It will then loop through all of the processes in the userqueue that are not already blocked and schedule them. It will
choose a winner based off a lottery where a random number is chosen and modded by the total number of tickets in the
pool. Simple algebra is then used to choose a winner based off this number of tickets by subtracting it from each entry
until 0 has been reached*/
PUBLIC int do_lottery()
{
	struct schedproc* rmp;

	int winner, w;
	unsigned num_tickets = 0;
	unsigned proc_nr;

	for(proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if((rmp->flags & IN_USE) && (is_user_process(rmp))) {
			if(rmp->priority != USER_Q) {
				rmp->priority = USER_Q;
				schedule_process(rmp);
			}

			num_tickets += rmp->num_tickets;
		}
	}

	if(!num_tickets)
		return OK;

	winner = (num_tickets ? (rand() % num_tickets) : 0);
	w = winner;

	for(proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if(!((rmp->flags & IN_USE) && (is_user_process(rmp))
			&& (rmp->priority == USER_Q)))
				continue;

		winner -= rmp->num_tickets;

		if(winner <= 0) {
			winning_proc_nr = proc_nr;

			rmp->priority   = MAX_USER_Q;

			printf("Winner  {P : %3d, T : %3d, E : %5d}\n",
				rmp->priority, w, rmp->endpoint);
			return schedule_process(rmp);
		}
	}

	return OK;
}
