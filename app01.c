#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <string.h>
#include <sys/ioctl.h>
#define MEM_CLR _IO('w',2)

#define device_globalmem "/dev/globalmem"
int main(void){
    //打开文件获取文件描述符，进行测试wirte，read
    int fd;
    int rc;//出错检验
    fd=open(device_globalmem,O_RDWR);
    char buff[20]="hello my_drive_01";//写入字符串
    printf("fd:%d\n",fd);
    printf("test_01 write、read and lseek\n");
    printf("write:%s\n",buff);
    rc=write(fd,buff,sizeof(buff)-1);
    if(rc==-1) //向驱动中写入字符串
    {
        perror("[wirte error]");
        exit(-1);
    }
    printf("write_length:%d\n",rc);
    //读取出来
    bzero(buff,sizeof(buff));

    rc=lseek(fd,0,SEEK_SET);
    printf("lseek_offset:%d\n",rc);
    if((rc=read(fd,buff,sizeof(buff)-1))==-1){
        perror("[read error]");
        exit(-1);
    }

    printf("read_length:%d\n",rc);
    printf("read:%s\n",buff);
    printf("\n");

    printf("test_02 ioctl\n");
    if(ioctl(fd,MEM_CLR,0)){
        perror("[ioctl]");
        exit(-1);
    }
    bzero(buff,sizeof(buff)-1);
    lseek(fd,0,SEEK_SET);
    if(read(fd,buff,sizeof(buff)-1)==-1){
        perror("[ioctl read error]");
        exit(-1);
    }
    if(strlen(buff)==0){
        printf("ioctl is true\n");
    }
    printf("test success\n");
    close(fd);
    return 0;
}
