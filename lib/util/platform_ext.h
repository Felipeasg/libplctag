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

#include <platform.h>

/*
 * spin locks
 */

extern int lock_acquire_spin(lock_t *lock);

/* spin lock block macro.  Similar to critical block */
#define spin_block(lock) \
    for (int __spin_flag_nargle_##__LINE__ = 1; __spin_flag_nargle_##__LINE__; __spin_flag_nargle_##__LINE__ = 0, lock_release(&(lock))) \
        for (int __spin_rc_nargle_##__LINE__ = lock_acquire_spin(&(lock)); __spin_rc_nargle_##__LINE__ != 0 && __spin_flag_nargle_##__LINE__; __spin_flag_nargle_##__LINE__ = 0)

/*
 * Tasklet functions.
 */

#define TASKLET_RESCHED (1)
#define TASKLET_DONE (0)

typedef struct tasklet_t *tasklet_p;
typedef int (*tasklet_func)(tasklet_p context);

struct tasklet_t
{
    tasklet_p next;
    lock_t lock;
    tasklet_func run_func;
    void *data;
};

extern tasklet_p tasklet_create(tasklet_func run_func, void *data);
// extern int tasklet_schedule(tasklet_p tasklet);
