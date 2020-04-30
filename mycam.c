#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>           
#include <fcntl.h>            
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <asm/types.h>        
#include <linux/videodev2.h>
#include <linux/fb.h>

  

#define CLEAR(x)	memset(&(x), 0, sizeof(x))

static char *MY_CAMERA = "/dev/video0";  //摄像头设备路径和名字 


typedef struct VideoBuffer {
    void   *start; 	//视频缓冲区的起始地址
    size_t  length;	//缓冲区的长度
    size_t  offset;	//缓冲区的长度
} VideoBuffer;


//rgb结构
typedef struct {
 unsigned char r; // 红色分量
 unsigned char g; // 绿色分量
 unsigned char b; // 蓝色分量
 unsigned char rgbReserved; // 保留字节（用作Alpha通道或忽略）
} rgb32;

//帧缓冲中的rgb结构
typedef struct {
 unsigned char b; // 蓝色分量
 unsigned char g; // 绿色分量
 unsigned char r; // 红色分量
 unsigned char rgbReserved; // 保留字节（用作Alpha通道或忽略）
} rgb32_frame;

//yuyv转rgb32的算法实现  
static int sign3 = 1;
/*
YUV到RGB的转换有如下公式： 
R = 1.164*(Y-16) + 1.159*(V-128); 
G = 1.164*(Y-16) - 0.380*(U-128)+ 0.813*(V-128); 
B = 1.164*(Y-16) + 2.018*(U-128)); 
*/
int yuvtorgb(int y, int u, int v)
{
     unsigned int pixel32 = 0;
     unsigned char *pixel = (unsigned char *)&pixel32;
     int r, g, b;
     static long int ruv, guv, buv;

     if(1 == sign3)
     {
         sign3 = 0;
         ruv = 1159*(v-128);
         guv = -380*(u-128) + 813*(v-128);
         buv = 2018*(u-128);
     }

     r = (1164*(y-16) + ruv) / 1000;
     g = (1164*(y-16) - guv) / 1000;
     b = (1164*(y-16) + buv) / 1000;

     if(r > 255) r = 255;
     if(g > 255) g = 255;
     if(b > 255) b = 255;
     if(r < 0) r = 0;
     if(g < 0) g = 0;
     if(b < 0) b = 0;

     pixel[0] = r;
     pixel[1] = g;
     pixel[2] = b;

     return pixel32;
}

int yuyv2rgb32(unsigned char *yuv, unsigned char *rgb, unsigned int
width, unsigned int height)
{
     unsigned int in, out;
     int y0, u, y1, v;
     unsigned int pixel32;
     unsigned char *pixel = (unsigned char *)&pixel32;
     //分辨率描述像素点个数，而yuv2个像素点占有4个字符，所以这里计算总的字符个数，需要乘2
     unsigned int size = width*height*2; 

     for(in = 0, out = 0; in < size; in += 4, out += 8)
     {
          y0 = yuv[in+0];
          u  = yuv[in+1];
          y1 = yuv[in+2];
          v  = yuv[in+3];

          sign3 = 1;
          pixel32 = yuvtorgb(y0, u, v);
          rgb[out+0] = pixel[0];   
          rgb[out+1] = pixel[1];
          rgb[out+2] = pixel[2];
          rgb[out+3] = 0;  //32位rgb多了一个保留位

          pixel32 = yuvtorgb(y1, u, v);
          rgb[out+4] = pixel[0];
          rgb[out+5] = pixel[1];
          rgb[out+6] = pixel[2];
          rgb[out+7] = 0;

     }
     return 0;
}




int open_cameral(char* path);
int init_FrameBuffer(void)  ;
void get_camInfo(void);
int set_format();
int get_buf(void);
void map_buf(void);
void startcon();
int get_picture(char *buffer);
int write_data_to_fb(void *fbp, int fbfd, void *img_buf, unsigned int img_width, unsigned int img_height, unsigned int img_bits);
int stopcon(void);  
int bufunmap(void); 
int exit_Framebuffer(void); 
void close_cameral(void);


//#define  CBUF_NUM  21
#define  CBUF_NUM  20

static int fd = -1;         //打开的摄像头句柄  
static int Frame_fd = -1;          //打开的帧缓冲句柄  
static char *FrameBuffer = NULL;
static int screensize;
unsigned int cam_width = 640, cam_hight = 480, Framebpp = 32;

VideoBuffer pic_buffers[CBUF_NUM];

struct v4l2_buffer enqueue;
struct v4l2_buffer dequeue;

void main()
{
 
    unsigned char *img_data = NULL;
    img_data = (unsigned char *)malloc(cam_width * cam_hight * 2);
	if(!img_data) {
		printf("malloc failed!\n");
	}
    //打开摄像头设备
    open_cameral(MY_CAMERA);
    //初始化帧缓冲
    init_FrameBuffer();  
    //获取当前摄像头的格式信息    
    get_camInfo();
    //设置用户需要的摄像头格式信息(分辨率和图形格式)
    set_format();
    //获取摄像头采集图片buf
    get_buf();
    //映射buf到用户空间
    map_buf();
    //开始采集
    startcon();
    while(1) //这里可以优化成select，就不会阻塞了
    {
        //获取采集到的数据
        get_picture(img_data);
        //把采集数据写入帧缓冲
//	YUYV_to_Y(img_data, FrameBuffer, 480, 640, 800*32);
	YUYV_to_RGB888(img_data, FrameBuffer, cam_hight, cam_width, 800*32);
    //    write_data_to_fb(FrameBuffer, Frame_fd, img_data, cam_width, cam_hight, Framebpp);    
    }
    
    //停止采集
    stopcon();  
    //解除映射
    bufunmap();
    //关闭帧缓冲
    exit_Framebuffer();  
    //关闭摄像头设备
    close_cameral();      
}


//打开摄像头设备
int open_cameral(char* path)
{
	fd=open(path,O_RDWR);
	if (fd < 0) {
		printf("Open /dev/video0 failed\n");
		return -1;
	}
}


//初始化framebuffer
int init_FrameBuffer(void)  
{  

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    Frame_fd = open("/dev/fb0" , O_RDWR);  
    if(-1 == Frame_fd)  
    {  
        perror("open frame buffer fail");  
        return -1 ;   
    }  
    
    // Get fixed screen information
    if (ioctl(Frame_fd, FBIOGET_FSCREENINFO, &finfo))
    {
        printf("Error reading fixed information.\n");
        exit(0);
    }

    // Get variable screen information
    if (ioctl(Frame_fd, FBIOGET_VSCREENINFO, &vinfo)) 
    {            
        printf("Error reading variable information.\n");
        exit(0);
    }
    //这里把整个显存一起初始化（xres_virtual 表示显存的x，比实际的xres大,bits_per_pixel位深）
    screensize = vinfo.xres_virtual * vinfo.yres_virtual * vinfo.bits_per_pixel / 8;
    //获取实际的位色，这里很关键，后面转换和填写的时候需要
    Framebpp = vinfo.bits_per_pixel;
    printf("%dx%d, %dbpp  screensize is %ld\n", vinfo.xres_virtual, vinfo.yres_virtual, vinfo.bits_per_pixel,screensize);
    
    //映射出来，用户直接操作
    FrameBuffer = mmap(0, screensize, PROT_READ | PROT_WRITE , MAP_SHARED , Frame_fd ,0 );  
    if(FrameBuffer == (void *)-1)  
    {  
        perror("memory map fail");  
        return -2 ;  
    }  
    return 0 ;   
}


//获取摄像头信息
void get_camInfo(void)
{ 
    int ret = 0;
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = cam_width;
    fmt.fmt.pix.height      = cam_hight;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        printf("VIDIOC_S_FMT failed (%d)\n", ret);
    }
	 
 
    //获取当前摄像头的宽高
    ioctl(fd, VIDIOC_G_FMT, &fmt);
    printf("Current data format information:\n\twidth:%d\n\theight:%d\n",fmt.fmt.pix.width,fmt.fmt.pix.height);

    struct v4l2_fmtdesc fmtdesc; 
    fmtdesc.index=0; 
    fmtdesc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    //获取当前摄像头支持的格式
    while(ioctl(fd,VIDIOC_ENUM_FMT,&fmtdesc)!=-1)
    {
        if(fmtdesc.pixelformat & fmt.fmt.pix.pixelformat)
        {
            printf("\tformat:%s  index=%d \n",fmtdesc.description,fmtdesc.index);
            break;
        }
        fmtdesc.index++;
    }
}


//设置摄像头具体格式
int set_format()
{
    struct v4l2_format fmt; 
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE; //这里必须填这个
    fmt.fmt.pix.width       = cam_width;   //用户希望设置的宽
    fmt.fmt.pix.height      = cam_hight;   //用户希望设置的高
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;//选择格式：V4L2_PIX_FMT_YUYV或V4L2_PIX_FMT_MJPEG
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    int ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        printf("VIDIOC_S_FMT failed (%d)\n", ret);
        return -1;
    }
    //如果用户传入超过了实际摄像头支持大小，摄像头会自动缩小成最大支持。这里把摄像头当前支持的宽高情况反馈给用户。
    cam_width = fmt.fmt.pix.width; 
    cam_hight = fmt.fmt.pix.height;
    
    printf("------------VIDIOC_S_FMT---------------\n");
    printf("Stream Format Informations:\n");
    printf(" type: %d\n", fmt.type);
    printf(" width: %d\n", fmt.fmt.pix.width);
    printf(" height: %d\n", fmt.fmt.pix.height);
    return 0;
}


//获取摄像头图片采集的缓存buf
int get_buf(void)
{
    struct v4l2_requestbuffers req;    
    memset(&req, 0, sizeof (req));
    req.count = CBUF_NUM;  //摄像头图片缓存buf个数，这里一般设置4个
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd,VIDIOC_REQBUFS,&req) <0)
    {
        perror("VIDIOC_REQBUFS error \n");
        return -1;
    }
    printf(" ***** count = %d **** \n",req.count );
    return 0;
}


//映射buf到用户空间
void map_buf(void)
{
    int numBufs = 0;
    struct v4l2_buffer tmp_buf ;   //摄像头缓冲buf临时保存buf
    for (numBufs = 0; numBufs < CBUF_NUM; numBufs++)
    {
        memset( &tmp_buf, 0, sizeof(tmp_buf) );
        tmp_buf.type =  V4L2_BUF_TYPE_VIDEO_CAPTURE ;   
        tmp_buf.memory = V4L2_MEMORY_MMAP ;   
        tmp_buf.index = numBufs;
        //获取内部buf信息到tmp_buf
        if (ioctl(fd, VIDIOC_QUERYBUF, &tmp_buf) < 0)
        {
            printf("VIDIOC_QUERYBUF (%d) error\n",numBufs);
            return;
        }
        pic_buffers[numBufs].length = tmp_buf.length;
        pic_buffers[numBufs].offset = (size_t) tmp_buf.m.offset;
        //开始映射
        pic_buffers[numBufs].start = mmap (NULL, tmp_buf.length,PROT_READ | PROT_WRITE, MAP_SHARED, fd, tmp_buf.m.offset);
        if (pic_buffers[numBufs].start == MAP_FAILED)
        {
            perror("pic_buffers error\n");
            //return -1;
        }
        //把设置好的buf入队列
        if (ioctl (fd, VIDIOC_QBUF, &tmp_buf) < 0)
        {
            printf("VIDIOC_QBUF error\n");
            //return -1;
        }
    }
    //初始化入队出队  
    enqueue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE ;   
    dequeue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE ;   
    enqueue.memory = V4L2_MEMORY_MMAP ;   
    dequeue.memory = V4L2_MEMORY_MMAP ;   
}

//开始采集
void startcon()
{
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl (fd, VIDIOC_STREAMON, &type) < 0)
    {
        printf("VIDIOC_STREAMON error\n");
        // return -1;
    }
}


//获取采集到的数据
int get_picture(char *buffer)  
{  
    int ret ;   
    //把采集到图片的缓冲出队 
  	
    ret = WaitCamerReady(1);
    if (ret != 0) {
	printf("wait time out\n");
	return ret;
    }    
 
 
    //printf("VIDIOC_DQBUF aaa\n" );
    ret = ioctl(fd , VIDIOC_DQBUF , &dequeue);  
 
//    printf("VIDIOC_DQBUF bbb\n" );
  
    if(ret != 0)  
    {  
        perror("dequeue fail");  
        return -1 ;   
    }  
  
    //把图片数据放到buffer中
    memcpy(buffer , pic_buffers[dequeue.index].start , pic_buffers[dequeue.index].length);  

  //  printf("get_pictures length = %d\n", pic_buffers[dequeue.index].length );
    //由于当前出队的缓冲数据已经拷贝到用户buffer中，这里可以重新入队用于后面的数据保存，构造起循环队列。
    enqueue.index = dequeue.index ;   
    printf("get_pictures index = %d\n", dequeue.index );
      
   
    ret = ioctl(fd , VIDIOC_QBUF , &enqueue);  
    if(ret != 0)  
    {  
        perror("enqueue fail");  
        return -2 ;   
    }  
    return 0 ;   
}

//写入framebuffer   fbp：帧缓冲首地址   fbfd：帧缓冲fd   img_buf:采集到的图片首地址  width：用户的宽 height：用户的高  bits：帧缓冲的位深 
int write_data_to_fb(void *fbp, int fbfd, void *img_buf, unsigned int img_width, unsigned int img_height, unsigned int img_bits)
{   
    int row, column;
    int num = 0;        //img_buf 中的某个像素点元素的下标
    rgb32_frame *rgb32_fbp = (rgb32_frame *)fbp;
    rgb32 *rgb32_img_buf = (rgb32 *)img_buf;    
    
    //防止摄像头采集宽高比显存大
    if(screensize < img_width * img_height * img_bits / 8)
    {
        printf("the imgsize is too large\n");
        return -1;
    }
        
    /*不同的位深度图片使用不同的显示方案*/
    switch (img_bits)
    {
        case 32:
            for(row = 0; row < img_height; row++)
                {
                    for(column = 0; column < img_width; column++)
                    {
                        //由于摄像头分辨率没有帧缓冲大，完成显示后，需要强制换行，帧缓冲是线性的，使用row * vinfo.xres_virtual换行
                        rgb32_fbp[row * img_bits + column].r = rgb32_img_buf[num].r;
                        rgb32_fbp[row * img_bits + column].g = rgb32_img_buf[num].g;
                        rgb32_fbp[row * img_bits + column].b = rgb32_img_buf[num].b;

                        num++;
                    }        
                }    
            break;
        default:
            break;
    }
    
    return 0;
}

int stopcon(void)  
{  
    //停止摄像头  
    int ret ;   
    int off= 1 ;   
    ret = ioctl(fd , VIDIOC_STREAMOFF, &off);  
    if(ret != 0)  
    {  
        perror("stop Cameral fail");  
        return -1 ;   
    }  
    return 0 ;  
}

int bufunmap(void)  
{  
    int i ;   
    for(i = 0 ; i < CBUF_NUM ; i++)
    {
        munmap(pic_buffers[i].start , pic_buffers[i].length);
    }
    return 0 ;   
}

//退出framebuffer  
int exit_Framebuffer(void)  
{  
    munmap(FrameBuffer , screensize);  
    close(Frame_fd);  
    return 0 ;   
}

void close_cameral(void)
{
    close(fd);
}


struct yuv422_sample {
	__u8 b1;
	__u8 b2;
	__u8 b3;
	__u8 b4;
};


struct bpp32_pixel {
	__u8 red;
	__u8 green;
	__u8 blue;
	__u8 alpha;
};

int YUYV_to_RGB888 (void *src_buf, void *dst_buf, size_t src_height ,size_t src_width, size_t dstStride)
{
    __u8 y0 = 0;
    __u8 u = 0;
    __u8 y1 = 0;
    __u8 v = 0;
    int colorB = 0;
    int colorG = 0;
    int colorR = 0;
    int line =0;
    int col =0;
    int dst_col =0;
    int dst_width = dstStride / 32 ;
    struct yuv422_sample *yuv422_samp = (struct yuv422_sample*)src_buf;
    struct bpp32_pixel *pixel = (struct bpp32_pixel *)dst_buf;



    for(line = 0; line < src_height; line++)
    {
       col = 0 ;
       for( dst_col = 0; dst_col < dst_width; dst_col = dst_col + 2)
       {
	  if(dst_col >= src_width)
		break;
	
          y0 = yuv422_samp[line * src_width/2 + col].b1;
          u  = yuv422_samp[line * src_width/2 + col].b2;
          y1 = yuv422_samp[line * src_width/2 + col].b3;
          v  = yuv422_samp[line * src_width/2 + col].b4;

          colorB = y0 + ((443 * (u - 128)) >> 8);
          colorB = (colorB < 0) ? 0 : ((colorB > 255 ) ? 255 : colorB);
	  colorG = y0 - ((179 * (v - 128) + 86 * (u - 128)) >> 8);
	  colorG = (colorG < 0) ? 0 : ((colorG > 255 ) ? 255 : colorG);
	  colorR = y0 + ((351 * (v - 128)) >> 8);
	  colorR = (colorR < 0) ? 0 : ((colorR > 255 ) ? 255 : colorR);


          pixel[line * dst_width + dst_col].red = colorR;
	  pixel[line * dst_width + dst_col].green = colorG;
	  pixel[line * dst_width + dst_col].blue = colorB;
	  pixel[line * dst_width + dst_col].alpha = 0;

	  colorB = y1 + ((443 * (u - 128)) >> 8);
          colorB = (colorB < 0) ? 0 : ((colorB > 255 ) ? 255 : colorB);
	  colorG = y1 - ((179 * (v - 128) + 86 * (u - 128)) >> 8);
	  colorG = (colorG < 0) ? 0 : ((colorG > 255 ) ? 255 : colorG);
	  colorR = y1 + ((351 * (v - 128)) >> 8);
	  colorR = (colorR < 0) ? 0 : ((colorR > 255 ) ? 255 : colorR);



          pixel[line * dst_width + dst_col + 1].red = colorR;
	  pixel[line * dst_width + dst_col + 1].green = colorG;
	  pixel[line * dst_width + dst_col + 1].blue = colorB;
	  pixel[line * dst_width + dst_col + 1].alpha = 0;
	   col = col + 1;

       }
    }
   return 0;
}


int WaitCamerReady(unsigned int second)
{
	fd_set fds;
	struct timeval tv;
	int r;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	/* Timeout */
	tv.tv_sec  = second;
	tv.tv_usec = 0;

	r = select(fd + 1, &fds, NULL, NULL, &tv);
	if (r == -1)
	{
		printf("select err\n");
		return -1;
	}
	else if (r == 0)
	{
		printf("select timeout\n");
		return -1;
	}

	return 0;
}

int YUYV_to_Y (void *src_buf, void *dst_buf, size_t src_height ,size_t src_width, size_t dstStride)
{
    __u8 y0 = 0;
    __u8 u = 0;
    __u8 y1 = 0;
    __u8 v = 0;
    int colorB = 0;
    int colorG = 0;
    int colorR = 0;
    int line =0;
    int col =0;
    int dst_col =0;
    int dst_width = dstStride / 32 ;
    struct yuv422_sample *yuv422_samp = (struct yuv422_sample*)src_buf;
    struct bpp32_pixel *pixel = (struct bpp32_pixel *)dst_buf;



    for(line = 0; line < src_height; line++)
    {
       col = 0 ;
       for( dst_col = 0; dst_col < dst_width; dst_col = dst_col + 2)
       {
          y0 = yuv422_samp[line * src_width/2 + col].b1;
          u  = yuv422_samp[line * src_width/2 + col].b2;
          y1 = yuv422_samp[line * src_width/2 + col].b3;
          v  = yuv422_samp[line * src_width/2 + col].b4;

/*          colorB = y0 + ((443 * (u - 128)) >> 8);
          colorB = (colorB < 0) ? 0 : ((colorB > 255 ) ? 255 : colorB);
	  colorG = y0 - ((179 * (v - 128) + 86 * (u - 128)) >> 8);
	  colorG = (colorG < 0) ? 0 : ((colorG > 255 ) ? 255 : colorG);
	  colorR = y0 + ((351 * (v - 128)) >> 8);
	  colorR = (colorR < 0) ? 0 : ((colorR > 255 ) ? 255 : colorR);  */


          pixel[line * dst_width + dst_col].red = y0;
	  pixel[line * dst_width + dst_col].green = 0;
	  pixel[line * dst_width + dst_col].blue = 0;

	/*  colorB = y1 + ((443 * (u - 128)) >> 8);
          colorB = (colorB < 0) ? 0 : ((colorB > 255 ) ? 255 : colorB);
	  colorG = y1 - ((179 * (v - 128) + 86 * (u - 128)) >> 8);
	  colorG = (colorG < 0) ? 0 : ((colorG > 255 ) ? 255 : colorG);
	  colorR = y1 + ((351 * (v - 128)) >> 8);
	  colorR = (colorR < 0) ? 0 : ((colorR > 255 ) ? 255 : colorR); */



          pixel[line * dst_width + dst_col + 1].red = y1;
	  pixel[line * dst_width + dst_col + 1].green = 0;
	  pixel[line * dst_width + dst_col + 1].blue = 0;
	 
	   col = col + 1;

       }
    }
   return 0;
}
