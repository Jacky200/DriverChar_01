#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
/*先写出基本要使用的头文件，根据我们的设备模块，分析其特性和要实现的功能*/
#include <linux/cdev.h> //cdev结构体头文件
#include <linux/mutex.h> //互斥体头函数
#include <linux/slab.h> //kzalloc函数头文件
#include <linux/uaccess.h> //copy_from_user函数头文件
#include <uapi/asm-generic/errno-base.h>//EINVL等出错宏的声明
#include <asm-generic/ioctl.h> //_IO()宏的头文件
#include <linux/string.h> //内核中的memset函数
#include <linux/list.h>

#define MEM_CLR _IO('w',2) //关于此宏详情见globalmem_ioctl函数
/*第一步，确定主设备号，描述该该设备*/

//定义主设备号
static int major=0;
/*此宏定义的意思就是major变量的值，可以是用户在挂载驱动的时候进行指定主设备号，
 * 最后一个参数表示不传的默认值，处理方式就是用户传入就以用户传入值为主设备号，
 * 否则就是向内核自动申请分配一个主设备号，会在后面注册设备时详细介绍*/
module_param(major,int,0);

//描述设备
/*看似这个设备结构体差不多只有1k-2k的大小，但是实际上我们申请的是4k，因为操作系统
 * 默认分配的最小空间就是4K，也就是页的大小就是4K，只是我们用了4K中的1-2K*/
struct globalmem_chardev{
    /*cdev结构体的其实就是我们这个设备的基类，表达成这种方式的目的就是为了方便内核的管理和查找
     * 其实现的原理是一个双向链表，一个cdev即指向下一个cdev结构体又指向我们的设备地址*/
    struct cdev dev_cdev;
    /*设备描述,申请1K空间大小的内存*/
    unsigned char mem_data[1024];
    /*定义互斥体，保证内核共享内存区域的互斥访问，意思就是同一时间只能有一个进程或线程访问该设备*/
    struct mutex mem_mutex;
};
/*定义一个设备结构体变量*/
/*为什么要定义一个设备指针变量呢？因为后面我们需要卸载掉该设备的时候需要传入设备的地址进行释放空间*/
/*那为什么不直接定义一个结构体变量呢？为了节省空间，用的时候才向内核申请空间，不用就不申请*/
struct globalmem_chardev* global_chardev;

/*第二步：实现系统调用函数open，read，write，iotcl,lseek函数*/
/*open函数实现逻辑：确定打开的设备，将内核对该设备分配的索引节点对象和文件描述符进行绑定
 * 以获得文件描述符（struct file* fp），文件描述符提供了private_data属性，为设备数据提供指向能力
 * 获得了文件描述符，自然我们的read,write函数就只需要将struct file结构体传入即可操作该设备*/
static int globalmem_open(struct inode* node,struct file* fp){
    struct globalmem_chardev* dev;
    /*该宏的作用是通过传入基类索引节点中设备的地址（该地址就是此设备cdev结构体在内核中的地址）
     * 并传入第二个参数，表示派生类的模型，和第三个参数cedv结构体在该派生类中的名字进而根据偏移量
     * 转化得到派生类的地址（dev）*/
    dev=container_of(node->i_cdev,struct globalmem_chardev,dev_cdev);

    //绑定文件描述符和索引节点对象
    fp->private_data=dev;
    return 0;
}
/*release函数是实现逻辑，release函数对应应用程序中的close函数，功能就是释放该文件描述符
 * 因为struct file结构体是在由VFS（虚拟文件系统）向内核申请的空间，且fd的值也是由VFS来维护
 * 所以驱动层就不需要管了*/
static int globalmem_release(struct inode* node,struct file* fp){
    return 0;
}

/*write函数的逻辑：
 * 第一个参数就是文件描述符，第二个参数就是用户空间中，想要驱动发送给硬件的内容，因为该内容是在
 * 用户空间，所以我们还需要将该空间映射到内核空间才能操作，第三个参数就是内容的长度，
 * 第四个参数表示此设备的文件指针位置*/
static ssize_t globalmem_wirte(struct file* fp,const char __user *data,size_t len,loff_t *pos){
    //通过file结构体获得设备地址
    struct globalmem_chardev* dev=fp->private_data;
    /*获取写偏移量,为什么是long，因为loff_t是long long其实我觉得这里应该写成long long的
     * 至于为什么用long,这似乎与以前的inode中i_size的大小设置有关，因此就会出现文件超过2GB就会
     * 读写失败，文件指针指不过去了.*/
   unsigned long p=*pos;
   size_t ret;//接收返回值
   if(p>=1024){//越界了
       return 0;
   }
   if(p+len>1024){
       len=1024-p;
   }
   //上锁
   mutex_lock(&dev->mem_mutex);
   //将用户空间的内容映射到内核空间
   if(copy_from_user(dev->mem_data+p,data,len)){
       ret=-EFAULT;
   }else{
       *pos+=len;//改变文件指针的位置
       ret=len;
   }
   //开锁
   mutex_unlock(&dev->mem_mutex);
   return ret;
}

/*read函数编写逻辑：
 * read函数与write函数逻辑相似，注意点就是read函数就是将内容从内核空间中映射到用户空间的
 * 缓冲区就行*/
static ssize_t globalmem_read(struct file* fp,char __user *data,size_t len,loff_t *pos){
    struct globalmem_chardev* dev=fp->private_data;
    unsigned long p=*pos;
    ssize_t ret;

    if(p>1024){//越界
        return 0;
    }
    if(p+len>1024){
        len=1024-p;
    }
    //上锁
    mutex_lock(&dev->mem_mutex);
    if(copy_to_user(data,dev->mem_data+p,len)){
        ret=-EFAULT;
    }else{
        *pos+=len;
        ret=len;
    }
    //开锁
    mutex_unlock(&dev->mem_mutex);
    return ret;
}
/*lseek函数实现逻辑：
 * 联系上面的read函数的第四个参数*pos，表示的就是当前文件指针的位置
 * lssek就是来改变该指针的指向，唯一的就是要获取当前地址然后改变就行*/
static loff_t globalmem_lseek(struct file* fp,loff_t offset,int whence){
    int ret;//接收返回值
    switch(whence){
        case SEEK_SET://表示开头位置
            if((offset<0) || (offset>1024)){
                ret=-EINVAL;//无效参数的意思
                break;
            }
            /*fp->f_pos表示文件指针当前位置*/
            fp->f_pos=offset;
            ret=fp->f_pos;
            break;
        case SEEK_CUR://表示文件指针当前位置
            if((fp->f_pos+offset<0) || (fp->f_pos+offset>1024)){
                ret=-EINVAL;
                break;
            }
            fp->f_pos+=offset;
            ret=fp->f_pos;
            break;
        case SEEK_END://表示文件末尾
            if(offset>=0){
                ret=-EINVAL;
                break;
            }
            fp->f_pos-=offset;
            ret=fp->f_pos;
            break;
        default:
            ret=-EINVAL;
            break;
    }
    return ret;
}

/*编写iotcl函数思路：
 * iotcl函数的功能是实现对设备模式的控制，因此第一个参数表示文件描述符
 * 第二个参数表示控制命令，第三个参数表示参入的参数*/
static long globalmem_ioctl(struct file* fp,unsigned int cmd,unsigned long arg){
    struct globalmem_chardev* dev=fp->private_data;
    switch(cmd){
        //这个宏在文件开头定义，这里进行详细的分析，之前在面试的时候，有问到过魔数区的问题
        case MEM_CLR:
        /*在驱动程序中，ioctl函数上传送的变量cmd是用于区别设备驱动程序请求处理内容的值
         * cmd除了可区别数字外，还包含有助于处理的几种相应的信息。
         * cmd一共32位，其中bit31-bit30：表示区别读写区，作用是区分是读取命令还是写入命令
         * bit29-bit15：14位表示“数据大小区”，表示ioctl中的arg变量传送的内存大小。
         * bit20-bit08 8位数据，表示魔数（幻数）区，用以与其他设备驱动程序的ioctl命令进行区别
         * bit07-bit00 8位为“区别序号区”，用于区分命令的顺序序号
         * 因此，_IO('w',2)中'w'表示传入的魔数，魔数一般用英文字母来表示，大小写均可。2表示基数
         * 魔数的功能就是用于与传入的命令进行比较，相同就处理，不同就不处理。奇数的功能就是区别不同的
         * 命令。因此_IO('w',2)表示含义就是为此设备设置一个命令为不做数据传输、魔数为w、基数为2的
         * 命令。当给此设备发出此命令时就执行相应的处理程序*/
            //进行上锁
            mutex_lock(&dev->mem_mutex);
            memset(dev->mem_data,0,1024);
            mutex_unlock(&dev->mem_mutex);
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

/*第三步，将驱动函数与文件操作结构体进行绑定*/
static struct file_operations fops={
        .owner =THIS_MODULE,
        .open =globalmem_open,
        .write =globalmem_wirte,
        .read =globalmem_read,
        .release =globalmem_release,
        .llseek =globalmem_lseek,
        .unlocked_ioctl =globalmem_ioctl,
};
/*第四步，向内核注册此设备的驱动*/
//__init的作用就是在注册成功该驱动以后，就释放此函数占用的内存空间
// （因为已经加载到内核中了，就不要占用内存了）
static int __init globalmem_init(void){
    dev_t dev_id ;//接收主设备号
    int rc;//出错检验
    int i;//循环的时候用，为什么这么写，为了兼容以前的老编译器，不支持for(int i=0;i<5;i++)的写法
    //申请主设备号
    if(major){//上面我们调用了module_param函数，如果用户传参数，就以用户的major为主
        dev_id=MKDEV(major,0);//0表示此设备号开始，意思就是我要占用major这个教室，从0位置开始的设备号
        //向内核占用设备号
        rc=register_chrdev_region(dev_id,2,"globalmem");//2表示占用的次设备号数量
    }else{//表示用户不指定主设备号，我们就向内核自动申请一个主设备号
        rc=alloc_chrdev_region(&dev_id,0,2,"globalmem");//0表示起始位置，2表示个数
        major=MAJOR(dev_id); //从设备号获取主设备号，目的就是向内核绑定设备与主设备号
    }
    if(rc<0){ //出错处理
        printk("driver major get failed\n");
        goto failed;
    }
    //占用了设备号的空间，现在就需要往空间中放入我们的设备，以及处理函数
    //第一步申请设备结构体空间
    global_chardev=kzalloc(2*sizeof(struct globalmem_chardev), GFP_KERNEL);//GFP_KERNEL表示内核内存正常分配

    if(!global_chardev){
        printk("get memery for globalmem_chardev failed\n");
        goto failed_chardev;
    }
    //注册设备（cdev）信息,并初始化对应设备的互斥体
    for(i=0;i<2;i++){
        cdev_init(&global_chardev[i].dev_cdev,&fops);//绑定设备与处理程序
        rc=cdev_add(&global_chardev[i].dev_cdev,MKDEV(major,i),1);//向内核注册
        if(rc<0){
            printk("cdev_add register dev_num failed\n,%d",i);
            goto failed_num;
        }
        //初始化互斥体
        mutex_init(&global_chardev[i].mem_mutex);
    }
    printk("register success\n");
    return 0;
    failed_num: //释放掉申请的内核空间
        kfree(global_chardev);
    failed_chardev: //释放掉占用的设备号
        unregister_chrdev_region(MKDEV(major,0),2);
    failed:
        return rc;
}

/*第五步：注销驱动*/
static void __exit gloabelmem_exit(void){
    int i;
    for(i=0;i<2;i++){
        cdev_del(&global_chardev[i].dev_cdev);
    }
    kfree(global_chardev);
    unregister_chrdev_region(MKDEV(major,0),2);
}

//入口函数
module_init(globalmem_init);
//出口函数
module_exit( gloabelmem_exit);
//遵循开源协议
MODULE_LICENSE("GPL");
//作者
MODULE_AUTHOR("jacky");