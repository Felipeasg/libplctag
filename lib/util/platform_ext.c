/***************************************************************************
 *   Copyright (C) 2015 by OmanTek                                         *
 *   Author Kyle Hayes  kylehayes@omantek.com                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <libplctag.h>
#include <platform.h>
#include <util/platform_ext.h>

/*
 * This file contains platform-level functions that are not platform-specific.
 */

/*
 * Spin Lock
 */

/* dangerous spin lock.  It will not time out! */
extern int lock_acquire_spin(lock_t *lock)
{
    int count = 0;

    while (!lock_acquire(lock)) {
        count++;

        if (count > 10000) { /* MAGIC */
            sleep_ms(1);     /* just enough to task switch */
            count = 0;
        }
    }

    return 1;
}

/*
 * Tasklet
 */

static lock_t tasklet_thread_pool_lock = LOCK_INIT;
static volatile int tasklet_thread_pool_initialized = 0;
static thread_p *tasklet_thread_pool = NULL;

static lock_t tasklet_list_lock = LOCK_INIT;
static volatile tasklet_p tasklet_list = NULL;

static void *tasklet_runner(void *arg);
static void add_tasklet(tasklet_p tasklet);
static void remove_tasklet(tasklet_p tasklet);


static void tasklet_init_pool(int num_thread)
{
    int already_done = 0;
    
    spin_block(tasklet_thread_pool_lock)
    {
        if (tasklet_thread_pool_initialized) {
            already_done = 1;
            break;
        } else {
            tasklet_thread_pool_initialized = 1;
        }
    }

    if (!already_done) {
        tasklet_thread_pool = (thread_p *)mem_alloc(sizeof(thread_p) * num_thread);

        if (tasklet_thread_pool) {
            int i;
            for (i = 0; i < num_thread; i++) {
                thread_create(&tasklet_thread_pool[i], tasklet_runner, 32 * 1024, NULL);
            }
        }

        /* FIXME - what to do if the allocation fails? */
    }
}

tasklet_p tasklet_create(tasklet_func run_func, void *data)
{
    tasklet_p tasklet = (tasklet_p)mem_alloc(sizeof(struct tasklet_t));

pdebug(1, "Starting");

    tasklet_init_pool(1); /* MAGIC - should be configurable somehow */

    if (tasklet) {
        tasklet->run_func = run_func;
        tasklet->data = data;

        add_tasklet(tasklet);
    }

pdebug(1,"Done");

    return tasklet;
}




/* internal functions */

void add_tasklet(tasklet_p tasklet)
{
    tasklet->next = NULL;
    
    spin_block(tasklet_list_lock)
    {
        tasklet->next = tasklet_list;
        tasklet_list = tasklet;
    }
}


void remove_tasklet(tasklet_p tasklet)
{
    tasklet_p prev = NULL;
    tasklet_p cur = NULL;

    spin_block(tasklet_list_lock)
    {
        cur = tasklet_list;
        
        while(cur && cur != tasklet) {
            prev = cur;
            cur = cur->next;
        }
        
        /* match? */
        if(cur == tasklet) {
            if(!prev) {
                /* first in the list */
                tasklet_list = cur->next;
            } else {
                prev->next = cur->next;
            }
        }
    }
}



/* find a tasklet that we can lock. */

tasklet_p get_next_tasklet(tasklet_p tasklet)
{
    tasklet_p tmp = tasklet;
    
    //pdebug(1, "starting with tasklet=%p",tasklet);
    
    spin_block(tasklet_list_lock) {
        if(!tmp) {
            tmp = tasklet_list;
        } else {
            tmp = tmp->next;
        }
        
        /* find the next one we can lock */
        while(tmp && !lock_acquire(&tmp->lock)) {
            tmp = tmp->next;
        }
    }
    
    //pdebug(1, "ending with tasklet=%p",tmp);
    
    return tmp;
}


#if  defined(WIN32) || defined(_WIN32)
DWORD __stdcall tasklet_runner(LPVOID not_used)
#else
void *tasklet_runner(void *not_used)
#endif
{
    tasklet_p tasklet = NULL;
        
    while (1) {
        /* Run to the end of the list, then pause */
        while((tasklet = get_next_tasklet(tasklet))) {            
            pdebug(1, "Running tasklet %p",tasklet);                
            
            if (tasklet->run_func) {
                /* run the run func and remove the tasklet if done. */
                if (tasklet->run_func(tasklet) == TASKLET_DONE) {
                    tasklet_p tmp = tasklet->next;
                    
                    remove_tasklet(tasklet);
                    mem_free(tasklet);
                    
                    tasklet = tmp;
                } else {
                    /* we will run it again at some point */
                    lock_release(&tasklet->lock);
                }
            } else {
                pdebug(1, "We have a tasklet without a run function!");
                /* something wrong */
                remove_tasklet(tasklet);
                mem_free(tasklet);
                /* 
                 * FIXME - we might leak the context memory here,
                 * but something is already quite wrong. 
                 */
            }
        }

        sleep_ms(500); /* MAGIC - FIXME */
    }

/* FIXME -- this should be factored out as a platform dependency.*/
#ifdef WIN32
    return (DWORD)0;
#else
    return NULL;
#endif
}
