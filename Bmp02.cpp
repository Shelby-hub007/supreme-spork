#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// 1. 定义BMP文件头与信息头（参考摘要2、5结构）
#pragma pack(push, 1) // 禁用内存对齐，确保结构占14/40字节,，push的意思是把原先的对齐规则存入栈中，强制对齐规则为1 
typedef struct {
    uint16_t bfType;      // 必须为0x4D42（BM）
    uint32_t bfSize;      // 文件总大小
    uint16_t bfReserved1; // 保留，0,预留用于未来拓展 
    uint16_t bfReserved2; // 保留，0,预留用于未来拓展 
    uint32_t bfOffBits;   // 像素数据偏移量（24/16位BMP均为54）
} BITMAPFILEHEADER;

typedef struct {//向程序提供具体的属性参数，程序只有读取了这些数据，才能正确读取属性参数 
    uint32_t biSize;      // 信息头大小（40字节）
    int32_t  biWidth;     // 图像宽度（像素）
    int32_t  biHeight;    // 图像高度（像素，正数=从下到上存储）
    uint16_t biPlanes;    // 必须为1
    uint16_t biBitCount;  // 位深（24→16）
    uint32_t biCompression;// 压缩方式（0=不压缩）
    uint32_t biSizeImage; // 像素数据大小（含行对齐）
    int32_t  biXPelsPerMeter;// 水平分辨率（默认0）
    int32_t  biYPelsPerMeter;// 垂直分辨率（默认0）
    uint32_t biClrUsed;   // 使用颜色数（0=全用）
    uint32_t biClrImportant;// 重要颜色数（0=全重要）
} BITMAPINFOHEADER;
//这两个结构体占54字节，是24/16的BMP像素数据偏移量
/*其中int32_t 是有符号32位整数，占4字节，可表示负数*/

#pragma pack(pop)//把之前存的对齐规则弹出来使用 

// 2. BGR888转RGB565核心函数
uint16_t bgr888_to_rgb565(uint8_t b, uint8_t g, uint8_t r) {
    uint8_t r5 = r >> 3; // 8位R→5位    >>这个符号是二进制位右移符号 ，丢弃低三位，保留剩余高位 
    uint8_t g6 = g >> 2; // 8位G→6位
    uint8_t b5 = b >> 3; // 8位B→5位
    return (r5 << 11) | (g6 << 5) | b5; // 重组为16位     <<这个符号是二进制位左移符号， 
	// 														通过左移运算将压缩后的通道值放到16位中的对应位置 
}



// 3. 转换主函数
int bmp24_to_bmp16(const char* in_path, const char* out_path) {
    // 打开输入BMP24文件
    FILE* in_fp = fopen(in_path, "rb");
    
	/*FILE类型来自库函数<stdio.h> ，这段代码的意思是把函数调用的返回值储存在刚刚创建的指针in_fo中
	借助这个指针可以使用FILE中的方法，如管理文件操作（打开，读取，写入，关闭），而这些方法都已经在库函数中写好 
	其次，c语言不能直接操控系统上的文件，只能通过FILE类型间接操控 
	
	
	*/ 
    if (!in_fp) { perror("打开输入文件失败"); return -1; }//返回-1默认意思是打开输入文件失败

    // 读取文件头与信息头
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    fread(&fh, sizeof(BITMAPFILEHEADER), 1, in_fp);//&fh获取该结构体的地址 
    fread(&ih, sizeof(BITMAPINFOHEADER), 1, in_fp);

    // 校验BMP24格式（参考摘要2）
    if (fh.bfType != 0x4D42 || ih.biBitCount != 24) {
        fclose(in_fp);
        fprintf(stderr, "非24位BMP文件（文件标识：0x%04X，位深：%d）\n", fh.bfType, ih.biBitCount);
		//stderr是标准错误输出流的指针，上面这行表示无缓存立刻输出错误信息
		 
        return -2;//返回-2默认意思是检测该文件是非24位BMP文件 
    }

    // 4. 计算BMP16参数（行对齐处理）
    int bmp16_line_bytes = ((ih.biWidth * 16 + 31) / 32) * 4; // 16位每行字节数（4字节对齐）
    
    ih.biBitCount = 16;                                      // 位深改为16
    ih.biSizeImage = bmp16_line_bytes * abs(ih.biHeight);    // 像素数据大小
    fh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + ih.biSizeImage; // 总大小
    fh.bfOffBits = 54; // 无调色板，偏移量固定54字节

    // 打开 输出BMP16文件 并 写入头信息
    FILE* out_fp = fopen(out_path, "wb");
    if (!out_fp) { perror("打开输出文件失败"); fclose(in_fp); return -3; }
    fwrite(&fh, sizeof(BITMAPFILEHEADER), 1, out_fp);
    fwrite(&ih, sizeof(BITMAPINFOHEADER), 1, out_fp);

    // 5. 读取BMP24像素并转换（处理BGR顺序与行对齐）
    uint8_t bgr24[3]; // 存储BMP24的B、G、R字节
    uint16_t rgb16;   // 存储转换后的RGB565
    uint8_t pad[4] = {0}; // 行对齐补齐用空字节
    int bmp24_line_bytes = ((ih.biWidth * 24 + 31) / 32) * 4; // 24位每行字节数

    // 定位到BMP24像素数据（偏移54字节）
    fseek(in_fp, fh.bfOffBits, SEEK_SET);
    for (int y = 0; y < abs(ih.biHeight); y++) {
        // 读取当前行所有像素
        for (int x = 0; x < ih.biWidth; x++) {
            fread(bgr24, 1, 3, in_fp); // 读取B、G、R
            rgb16 = bgr888_to_rgb565(bgr24[0], bgr24[1], bgr24[2]); // 转换
            fwrite(&rgb16, 2, 1, out_fp); // 写入RGB565
        }
        // 处理BMP24行尾补齐（跳过无用字节）
        fseek(in_fp, bmp24_line_bytes - ih.biWidth * 3, SEEK_CUR);
        // 处理BMP16行尾补齐
        int pad_bytes = bmp16_line_bytes - ih.biWidth * 2;
        if (pad_bytes > 0) fwrite(pad, 1, pad_bytes, out_fp);
    }

    // 关闭文件
    fclose(in_fp);
    fclose(out_fp);
    printf("转换完成：%s → %s\n", in_path, out_path);
    return 0;//返回0表示转换成功 
}





int main() {
    // 示例：将input24.bmp转为output16.bmp
    return bmp24_to_bmp16("test.bmp", "output16.bmp");
}
