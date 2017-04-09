static int core_sys_select(int n, fd_set __user *inp, fd_set __user *outp,  
                           fd_set __user *exp, s64 *timeout)  
{  
    fd_set_bits fds;  
    void *bits;  
    int ret, max_fds;  
    unsigned int size;  
    struct fdtable *fdt;  
    /* Allocate small arguments on the stack to save memory and be faster */  
  
    /*SELECT_STACK_ALLOC 定义为256*/  
    long stack_fds[SELECT_STACK_ALLOC/sizeof(long)];  
  
    ret = -EINVAL;  
    if (n < 0)  
        goto out_nofds;  
  
    /* max_fds can increase, so grab it once to avoid race */  
    rcu_read_lock();  
    fdt = files_fdtable(current->files);/*获取当前进程的文件描述符表*/  
    max_fds = fdt->max_fds;  
    rcu_read_unlock();  
    if (n > max_fds)/*修正用户传入的第一个参数：fd_set中文件描述符的最大值*/  
        n = max_fds;  
  
    /* 
    * We need 6 bitmaps (in/out/ex for both incoming and outgoing), 
    * since we used fdset we need to allocate memory in units of 
    * long-words.  
    */  
  
    /* 
    如果stack_fds数组的大小不能容纳下所有的fd_set,就用kmalloc重新分配一个大数组。 
    然后将位图平均分成份,并初始化fds结构 
    */  
    size = FDS_BYTES(n);  
    bits = stack_fds;  
    if (size > sizeof(stack_fds) / 6) {  
        /* Not enough space in on-stack array; must use kmalloc */  
        ret = -ENOMEM;  
        bits = kmalloc(6 * size, GFP_KERNEL);  
        if (!bits)  
            goto out_nofds;  
    }  
    fds.in      = bits;  
    fds.out     = bits +   size;  
    fds.ex      = bits + 2*size;  
    fds.res_in  = bits + 3*size;  
    fds.res_out = bits + 4*size;  
    fds.res_ex  = bits + 5*size;  
  
    /*get_fd_set仅仅调用copy_from_user从用户空间拷贝了fd_set*/  
    if ((ret = get_fd_set(n, inp, fds.in)) ||  
        (ret = get_fd_set(n, outp, fds.out)) ||  
        (ret = get_fd_set(n, exp, fds.ex)))  
        goto out;  
  
    zero_fd_set(n, fds.res_in);  
    zero_fd_set(n, fds.res_out);  
    zero_fd_set(n, fds.res_ex);  
  
  
    /* 
    接力棒传给了do_select 
    */  
    ret = do_select(n, &fds, timeout);  
  
    if (ret < 0)  
        goto out;  
  
    /*do_select返回,是一种异常状态*/  
    if (!ret) {  
        /*记得上面的sys_select不？将ERESTARTNOHAND转换成了EINTR并返回。EINTR表明系统调用被中断*/  
        ret = -ERESTARTNOHAND;  
        if (signal_pending(current))/*当当前进程有信号要处理时,signal_pending返回真,这符合了EINTR的语义*/  
            goto out;  
        ret = 0;  
    }  
  
    /*把结果集,拷贝回用户空间*/  
    if (set_fd_set(n, inp, fds.res_in) ||  
        set_fd_set(n, outp, fds.res_out) ||  
        set_fd_set(n, exp, fds.res_ex))  
        ret = -EFAULT;  
  
out:  
    if (bits != stack_fds)  
        kfree(bits);/*对应上面的kmalloc*/  
out_nofds:  
    return ret;  
} 