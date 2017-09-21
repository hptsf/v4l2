#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <linux/fb.h>
#include <signal.h>
#include "sock.h"

#define CLEAR(x) memset (&(x), 0, sizeof (x))

#define IMG_WIDTH       640
#define IMG_HEIGHT      480
#define BYTES_PER_PIXEL 3

#pragma pack(2)
typedef struct tagBITMAPFILEHEADER { /* bmfh */
    unsigned short bfType;        // 4D42
    unsigned int bfSize;       // 
    unsigned short bfReserved1; 
    unsigned short bfReserved2; 
    unsigned int bfOffBits;    // 14 + 40
} BITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER { /* bmih */
    unsigned int biSize; 
    unsigned int biWidth; 
    unsigned int biHeight; 
    unsigned short biPlanes; 
    unsigned short biBitCount; 
    unsigned int biCompression; 
    unsigned int biSizeImage; 
    unsigned int biXPelsPerMeter; 
    unsigned int biYPelsPerMeter; 
    unsigned int biClrUsed; 
    unsigned int biClrImportant;
} BITMAPINFOHEADER;
#pragma pack()

BITMAPFILEHEADER bfh;
BITMAPINFOHEADER bih;

struct buffer {
    void * start;
    size_t length;
};

static char * dev_name = NULL;
static int fd = -1;
struct buffer * buffers = NULL;
static unsigned int n_buffers = 0;
static int time_in_sec_capture=5;
static int fbfd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static char *fbp = NULL;
static long screensize=0;

static bool run_flag = false;
static int sockfd = -1;

static void save_bmp_image(const unsigned char *pbuf)
{
    int fd = -1;
    int ret = -1;

    int img_size = IMG_WIDTH * IMG_HEIGHT * BYTES_PER_PIXEL;

    memset(&bfh, 0x00, sizeof(bfh));
    memset(&bih, 0x00, sizeof(bih));

    bfh.bfType = 0x4D42;
    bfh.bfSize = 14 + 40 + img_size;
    bfh.bfOffBits = 14 + 40;

    bih.biSize = 40;
    bih.biWidth = IMG_WIDTH;
    bih.biHeight = IMG_HEIGHT;
    bih.biPlanes = 1;
    bih.biBitCount = BYTES_PER_PIXEL << 3;
    bih.biCompression = 0;
    bih.biSizeImage = 0;

    fd = open("./test.bmp", O_RDWR | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR);
    if(fd < 0)
        perror("open error");

    ret = write(fd, &bfh, sizeof(bfh));
    ret = write(fd, &bih, sizeof(bih));
    ret = write(fd, pbuf, img_size);

    close(fd);
}

static void sig_handler(int key)
{
    fprintf(stdout, "\nGet a signal: %d\n", key);
    run_flag = false;
}

static void errno_exit (const char * s)
{
    fprintf (stderr, "%s error %d, %s\n",s, errno, strerror (errno));
    exit (EXIT_FAILURE);
}

static int xioctl (int fd,int request,void * arg)
{
    int r;
    do r = ioctl (fd, request, arg);
    while (-1 == r && EINTR == errno);
    return r;
}

int clip(int value, int min, int max) {
    return (value > max ? max : value < min ? min : value);
}

static void process_image (const void * p)
{
    static int flag = 0;
#if 0
    if(0 == flag){
        flag = 1;
        save_bmp_image(p);
    }
#endif

#if 1
    sock_send(sockfd, "test msg", 8);
#else
    int x;
    int y;
    int j;
    int istride = IMG_WIDTH * BYTES_PER_PIXEL;
    unsigned int index;
    unsigned char r, g, b;
    unsigned char *in = (unsigned char *)p;

    for(y = 100; y < IMG_HEIGHT + 100; y++){
        for(j = 0, x = 100; x < IMG_WIDTH + 100; j += 3, x++){
            index = (y + vinfo.yoffset)*finfo.line_length + (x + vinfo.xoffset)*(vinfo.bits_per_pixel >> 3);
            r = in[j + 0];
            g = in[j + 1];
            b = in[j + 2];
            fbp[index + 0] = clip(r, 0, 255);
            fbp[index + 1] = clip(g, 0, 255);
            fbp[index + 2] = clip(b, 0, 255);
            fbp[index + 3] = 0x00;              // unused
        }

        in += istride;
    }
#endif
}

static int read_frame (void)
{
    struct v4l2_buffer buf;
    unsigned int i;
    static int cnt = 0;

    CLEAR (buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
        switch (errno) {
            case EAGAIN:
                return 0;
            case EIO:
            default:
                errno_exit ("VIDIOC_DQBUF");
        }
    }

    assert (buf.index < n_buffers);
    fprintf(stdout, "  Get %06d packages[index:%d length:%u]", ++cnt, buf.index, buf.length);
    fprintf(stdout, "\r");
    fflush(stdout);
//    assert (buf.field ==V4L2_FIELD_NONE);
    process_image (buffers[buf.index].start);
    if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
        errno_exit ("VIDIOC_QBUF");

    return 1;
}

static void run (void)
{
    unsigned int count;

    while (run_flag) {
        for (;;) {
            fd_set fds;
            struct timeval tv;
            int r;
            FD_ZERO (&fds);
            FD_SET (fd, &fds);

            tv.tv_sec = 2;
            tv.tv_usec = 0;
            r = select (fd + 1, &fds, NULL, NULL, &tv);
            if (-1 == r) {
                if (EINTR == errno)
                    continue;
                errno_exit ("select");
            }

            if (0 == r) {
                fprintf (stderr, "select timeout\n");
                exit (EXIT_FAILURE);
            }

            if (read_frame())
                break;
        }
    }

    fprintf(stdout, "Capture done\n");
}

static void stop_capturing (void)
{
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl (fd, VIDIOC_STREAMOFF, &type))
        errno_exit ("VIDIOC_STREAMOFF");
}

static void start_capturing (void)
{
    unsigned int i;
    enum v4l2_buf_type type;

    for (i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;

        CLEAR (buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
            errno_exit ("VIDIOC_QBUF");
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
        errno_exit ("VIDIOC_STREAMON");
}

static void uninit_device (void)
{
    unsigned int i;

    for (i = 0; i < n_buffers; ++i)
        if (-1 == munmap (buffers[i].start, buffers[i].length))
            errno_exit ("munmap");

    if (-1 == munmap(fbp, screensize)) {
        printf(" Error: framebuffer device munmap() failed.\n");
        exit (EXIT_FAILURE) ;
    }
    free (buffers);

    sock_uninit(&sockfd);
}


static void init_mmap (void)
{
    struct v4l2_requestbuffers req;

    //mmap framebuffer
    fbp = (char *)mmap(NULL,screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == (void *)-1) {
        printf("Error: failed to map framebuffer device to memory.\n");
        exit (EXIT_FAILURE) ;
    }
 //   memset(fbp, 0, screensize);

    CLEAR (req);
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
 //   req.memory = V4L2_MEMORY_DMABUF;
    if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            fprintf (stderr, "%s does not support memory mapping\n", dev_name);
            exit (EXIT_FAILURE);
        } else {
            errno_exit ("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 4) {    //if (req.count < 2)
        fprintf (stderr, "Insufficient buffer memory on %s\n",dev_name);
        exit (EXIT_FAILURE);
    }

    buffers = calloc (req.count, sizeof (*buffers));
    if (!buffers) {
        fprintf (stderr, "Out of memory\n");
        exit (EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;

        CLEAR (buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;

        if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))
            errno_exit ("VIDIOC_QUERYBUF");

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start =mmap (NULL,buf.length,PROT_READ | PROT_WRITE ,MAP_SHARED,fd, buf.m.offset);

        if (MAP_FAILED == buffers[n_buffers].start)
            errno_exit ("mmap");
    }
}

static bool init_device (void)
{
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    unsigned int min;


    // Get fixed screen information
    if (-1==xioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        printf("Error reading fixed information.\n");
        return false;
    }
    fprintf(stdout, "Screen fix info:\n");
    fprintf(stdout, "\ttype: %d\n", finfo.type);
    fprintf(stdout, "\tlength of a line: %d\n", finfo.line_length);

    // Get variable screen information
    if (-1==xioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        printf("Error reading variable information.\n");
        return false;
    }
    fprintf(stdout, "Screen var info:\n");
    fprintf(stdout, "\tvisible resolution: %dx%d\n", vinfo.xres, vinfo.yres);
    fprintf(stdout, "\tvirtual resolution: %dx%d\n", vinfo.xres_virtual, vinfo.yres_virtual);
    fprintf(stdout, "\toffset x: %d, y: %d\n", vinfo.xoffset, vinfo.yoffset);
    fprintf(stdout, "\tbits_per_pixel: %d\n", vinfo.bits_per_pixel);
    screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

    if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            fprintf (stderr, "%s is no V4L2 device\n",dev_name);
            return false;
        } else {
            perror("VIDIOC_QUERYCAP");
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf (stderr, "%s is no video capture device\n",dev_name);
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf (stderr, "%s does not support streaming i/o\n",dev_name);
        return false;
    }

    CLEAR (cropcap);
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (0 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect;

        if (-1 == xioctl (fd, VIDIOC_S_CROP, &crop)) {
            switch (errno) {
                case EINVAL:
                    break;
                default:
                    break;
            }
        }
    }else {     }

    CLEAR (fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
#if 0
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
#else
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
#endif
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
        perror("VIDIOC_S_FMT");

    init_mmap ();
    sockfd = sock_init();
	return true;
}

static void close_device (void)
{
    if (-1 == close (fd))
        errno_exit ("close");
    fd = -1;
    close(fbfd);
}

static bool open_device (void)
{
    struct stat st;

    if (-1 == stat (dev_name, &st)) {
        fprintf (stderr, "Cannot identify '%s': %d, %s\n",dev_name, errno, strerror (errno));
        return false;
    }

    if (!S_ISCHR (st.st_mode)) {
        fprintf (stderr, "%s is no device\n", dev_name);
        return false;
    }

    //open framebuffer
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd==-1) {
        printf("Error: cannot open framebuffer device.\n");
        return false;
    }

    //open camera
    fd = open (dev_name, O_RDWR| O_NONBLOCK, 0);
    if (-1 == fd) {
        fprintf (stderr, "Cannot open '%s': %d, %s\n",dev_name, errno, strerror (errno));
		close(fbfd);
        return false;
    }

	return true;
}

static void usage (FILE * fp,int argc,char ** argv)
{
    fprintf (fp,
            "Usage: %s [options]\n\n"
            "Options:\n"
            "-d | --device name Video device name [/dev/video]\n"
            "-h | --help Print this message\n"
            "-t | --how long will display in seconds\n"
            "",
            argv[0]);
}

static const char short_options [] = "d:ht:";
static const struct option long_options [] = {
    { "device", required_argument, NULL, 'd' },
    { "help", no_argument, NULL, 'h' },
    { "time", no_argument, NULL, 't' },
    { 0, 0, 0, 0 }
};

int main (int argc,char ** argv)
{
    int index;
    int c;

    dev_name = "/dev/video0";
    for (;;){
        c = getopt_long (argc, argv,short_options, long_options,&index);
        if (-1 == c)
            break;

        switch (c) {
        case 0:
            break;

        case 'd':
            dev_name = optarg;
            break;

        case 'h':
            usage (stdout, argc, argv);
            exit (EXIT_SUCCESS);
        case 't':
            time_in_sec_capture = atoi(optarg);
            break;

        default:
            usage (stderr, argc, argv);
            exit (EXIT_FAILURE);
        }
    }
    if(!open_device ())
		return -1;

    init_device ();
    start_capturing ();

    run_flag = true;
    signal(SIGINT, sig_handler);
    run();
    stop_capturing ();
    uninit_device ();
    close_device ();

    return 0;
}
