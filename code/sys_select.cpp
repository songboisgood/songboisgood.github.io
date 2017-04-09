asmlinkage long sys_select(int n, fd_set __user *inp, fd_set __user *outp,  
                           fd_set __user *exp, struct timeval __user *tvp)  
{  
    s64 timeout = -1;  
    struct timeval tv;  
    int ret;  
  
    if (tvp) {/*如果有超时值*/  
        if (copy_from_user(&tv, tvp, sizeof(tv)))  
            return -EFAULT;  
  
        if (tv.tv_sec < 0 || tv.tv_usec < 0)/*时间无效*/  
            return -EINVAL;  
  
        /* Cast to u64 to make GCC stop complaining */  
        if ((u64)tv.tv_sec >= (u64)MAX_INT64_SECONDS)  
            timeout = -1;   /* 无限等待*/  
        else {  
            timeout = DIV_ROUND_UP(tv.tv_usec, USEC_PER_SEC/HZ);  
            timeout += tv.tv_sec * HZ;/*计算出超时的相对时间,单位为时钟周期数*/  
        }  
    }  
  
    /*主要工作都在core_sys_select中做了*/  
    ret = core_sys_select(n, inp, outp, exp, &timeout);  
  
    if (tvp) {/*如果有超时值*/  
        struct timeval rtv;  
  
        if (current->personality & STICKY_TIMEOUTS)/*模拟bug的一个机制,不详细描述*/  
            goto sticky;  
        /*rtv中是剩余的时间*/  
        rtv.tv_usec = jiffies_to_usecs(do_div((*(u64*)&timeout), HZ));  
        rtv.tv_sec = timeout;  
        if (timeval_compare(&rtv, &tv) >= 0)/*如果core_sys_select超时返回,更新时间*/  
            rtv = tv;  
        /*拷贝更新后的时间到用户空间*/  
        if (copy_to_user(tvp, &rtv, sizeof(rtv))) {  
sticky:  
            /* 
            * If an application puts its timeval in read-only 
            * memory, we don't want the Linux-specific update to 
            * the timeval to cause a fault after the select has 
            * completed successfully. However, because we're not 
            * updating the timeval, we can't restart the system 
            * call. 
            */  
            if (ret == -ERESTARTNOHAND)/*ERESTARTNOHAND表明,被中断的系统调用*/  
                ret = -EINTR;  
        }  
    }  
  
    return ret;  
} 