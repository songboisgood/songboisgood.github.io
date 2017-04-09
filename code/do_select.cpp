int do_select(int n, fd_set_bits *fds, s64 *timeout)  
{  
    struct poll_wqueues table;  
    poll_table *wait;  
    int retval, i;  
  
    rcu_read_lock();  
    /*根据已经打开fd的位图检查用户打开的fd, 要求对应fd必须打开, 并且返回最大的fd*/  
    retval = max_select_fd(n, fds);  
    rcu_read_unlock();  
  
    if (retval < 0)  
        return retval;  
    n = retval;  
  
  
    /*将当前进程放入自已的等待队列table, 并将该等待队列加入到该测试表wait*/  
    poll_initwait(&table);  
    wait = &table.pt;  
  
    if (!*timeout)  
        wait = NULL;  
    retval = 0;  
  
    for (;;) {/*死循环*/  
        unsigned long *rinp, *routp, *rexp, *inp, *outp, *exp;  
        long __timeout;  
  
        /*注意:可中断的睡眠状态*/  
        set_current_state(TASK_INTERRUPTIBLE);  
  
        inp = fds->in; outp = fds->out; exp = fds->ex;  
        rinp = fds->res_in; routp = fds->res_out; rexp = fds->res_ex;  
  
  
        for (i = 0; i < n; ++rinp, ++routp, ++rexp) {/*遍历所有fd*/  
            unsigned long in, out, ex, all_bits, bit = 1, mask, j;  
            unsigned long res_in = 0, res_out = 0, res_ex = 0;  
            const struct file_operations *f_op = NULL;  
            struct file *file = NULL;  
  
            in = *inp++; out = *outp++; ex = *exp++;  
            all_bits = in | out | ex;  
            if (all_bits == 0) {  
                /* 
                __NFDBITS定义为(8 * sizeof(unsigned long)),即long的位数。 
                因为一个long代表了__NFDBITS位，所以跳到下一个位图i要增加__NFDBITS 
                */  
                i += __NFDBITS;  
                continue;  
            }  
  
            for (j = 0; j < __NFDBITS; ++j, ++i, bit <<= 1) {  
                int fput_needed;  
                if (i >= n)  
                    break;  
  
                /*测试每一位*/  
                if (!(bit & all_bits))  
                    continue;  
  
                /*得到file结构指针，并增加引用计数字段f_count*/  
                file = fget_light(i, &fput_needed);  
                if (file) {  
                    f_op = file->f_op;  
                    mask = DEFAULT_POLLMASK;  
  
                    /*对于socket描述符,f_op->poll对应的函数是sock_poll 
                    注意第三个参数是等待队列，在poll成功后会将本进程唤醒执行*/  
                    if (f_op && f_op->poll)  
                        mask = (*f_op->poll)(file, retval ? NULL : wait);  
  
                    /*释放file结构指针，实际就是减小他的一个引用计数字段f_count*/  
                    fput_light(file, fput_needed);  
  
                    /*根据poll的结果设置状态,要返回select出来的fd数目，所以retval++。 
                    注意：retval是in out ex三个集合的总和*/  
                    if ((mask & POLLIN_SET) && (in & bit)) {  
                        res_in |= bit;  
                        retval++;  
                    }  
                    if ((mask & POLLOUT_SET) && (out & bit)) {  
                        res_out |= bit;  
                        retval++;  
                    }  
                    if ((mask & POLLEX_SET) && (ex & bit)) {  
                        res_ex |= bit;  
                        retval++;  
                    }  
                }  
  
                /* 
                注意前面的set_current_state(TASK_INTERRUPTIBLE); 
                因为已经进入TASK_INTERRUPTIBLE状态,所以cond_resched回调度其他进程来运行， 
                这里的目的纯粹是为了增加一个抢占点。被抢占后，由等待队列机制唤醒。 
 
                在支持抢占式调度的内核中（定义了CONFIG_PREEMPT），cond_resched是空操作 
                */   
                cond_resched();  
            }  
            /*根据poll的结果写回到输出位图里*/  
            if (res_in)  
                *rinp = res_in;  
            if (res_out)  
                *routp = res_out;  
            if (res_ex)  
                *rexp = res_ex;  
        }  
        wait = NULL;  
        if (retval || !*timeout || signal_pending(current))/*signal_pending前面说过了*/  
            break;  
        if(table.error) {  
            retval = table.error;  
            break;  
        }  
  
        if (*timeout < 0) {  
            /*无限等待*/  
            __timeout = MAX_SCHEDULE_TIMEOUT;  
        } else if (unlikely(*timeout >= (s64)MAX_SCHEDULE_TIMEOUT - 1)) {  
            /* 时间超过MAX_SCHEDULE_TIMEOUT,即schedule_timeout允许的最大值，用一个循环来不断减少超时值*/  
            __timeout = MAX_SCHEDULE_TIMEOUT - 1;  
            *timeout -= __timeout;  
        } else {  
            /*等待一段时间*/  
            __timeout = *timeout;  
            *timeout = 0;  
        }  
  
        /*TASK_INTERRUPTIBLE状态下，调用schedule_timeout的进程会在收到信号后重新得到调度的机会， 
        即schedule_timeout返回,并返回剩余的时钟周期数 
        */  
        __timeout = schedule_timeout(__timeout);  
        if (*timeout >= 0)  
            *timeout += __timeout;  
    }  
  
    /*设置为运行状态*/  
    __set_current_state(TASK_RUNNING);  
    /*清理等待队列*/  
    poll_freewait(&table);  
  
    return retval;  
}  