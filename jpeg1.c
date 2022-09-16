#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bmp.h"
#define JPEG_QUALITY 100
#pragma warning(disable : 4996)

unsigned int BuffIndex;	 // JPEG数据的位置
unsigned int BuffSize;	 // JPEG数据的大小
unsigned int BuffX;		 // 图像的横向尺寸
unsigned int BuffY;		 // 图像的垂直尺寸
unsigned int BuffBlockX; // 横向MCU个数
unsigned int BuffBlockY; // 纵向MCU个数
unsigned char *Buff;	 // 装入解压数据的缓冲器

unsigned char TableDQT[4][64];	 // 量化表
unsigned short TableDHT[4][162]; // 霍夫曼表格

unsigned short TableHT[4][16]; // 霍夫曼起始表
unsigned char TableHN[4][16];  // 霍夫曼起始号

unsigned char BitCount = 0; // 压缩数据的读取位置
unsigned int LineData;		// 解压缩时使用的数据
unsigned int NextData;		// 解压缩时使用的数据

unsigned int PreData[3]; // 用于DC分量的储存缓冲器

unsigned char CompCount;  // 组件数
unsigned char CompNum[4]; // 组件号(未使用)
unsigned char CompSample[4];
unsigned char CompDQT[4]; // 组件的DQT表号
unsigned char CompDHT[4]; // 组件的DHT表号
unsigned char CompSampleX, CompSampleY;

// 反Zig-Zag表
int zigzag_table[] = {
	0, 1, 8, 16, 9, 2, 3, 10,
	17, 24, 32, 25, 18, 11, 4, 5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63, 0};

typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef int LONG;

typedef struct tagBITMAPFILEHEADER
{
	WORD bfType;
	DWORD bfSize;
	WORD bfReserved1;
	WORD bfReserved2;
	DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER
{
	DWORD biSize;
	LONG biWidth;
	LONG biHeight;
	WORD biPlanes;
	WORD biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG biXPelsPerMeter;
	LONG biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;


void BmpSave(char *file, unsigned char *buff, unsigned int x, unsigned int y, unsigned int b)
{
	BITMAPFILEHEADER lpBf;
	BITMAPINFOHEADER lpBi;
	unsigned char tbuff[4];
	FILE *fp;
	unsigned char str;
	int i, k;

	if ((fp = fopen(file, "wb")) == NULL)
	{
		perror(0);
		exit(0);
	}

	// 文件现在的设定
	tbuff[0] = 'B';
	tbuff[1] = 'M';
	fwrite(tbuff, 2, 1, fp);
	tbuff[3] = ((14 + 40 + x * y * b) >> 24) & 0xff; //右移24位，取低8位
	tbuff[2] = ((14 + 40 + x * y * b) >> 16) & 0xff; //右移16位，取低8位
	tbuff[1] = ((14 + 40 + x * y * b) >> 8) & 0xff;	 //右移8位，取低8位
	tbuff[0] = ((14 + 40 + x * y * b) >> 0) & 0xff;	 //右移0位，取低8位
	fwrite(tbuff, 4, 1, fp);
	tbuff[1] = 0;
	tbuff[0] = 0;
	fwrite(tbuff, 2, 1, fp);
	fwrite(tbuff, 2, 1, fp);
	tbuff[3] = 0;
	tbuff[2] = 0;
	tbuff[1] = 0;
	tbuff[0] = 54;
	fwrite(tbuff, 4, 1, fp);

	// 信息的设定
	lpBi.biSize = 40;
	lpBi.biWidth = x;
	lpBi.biHeight = y;
	lpBi.biPlanes = 1;
	lpBi.biBitCount = b * 8;
	lpBi.biCompression = 0;
	lpBi.biSizeImage = x * y * b;
	lpBi.biXPelsPerMeter = 300;
	lpBi.biYPelsPerMeter = 300;
	lpBi.biClrUsed = 0;
	lpBi.biClrImportant = 0;
	fwrite(&lpBi, 1, 40, fp);

	// 上下反转
	for (k = 0; k < y / 2; k++)
	{
		for (i = 0; i < x * 3; i++)
		{
			str = buff[k * x * 3 + i];
			buff[k * x * 3 + i] = buff[((y - 1) * x * 3 - k * x * 3) + i];
			buff[((y - 1) * x * 3 - k * x * 3) + i] = str;
		}
	}
	// delete height
	for (k = 0; k < y; k++)
	{
		for (i = 0; i < x / 4; i++)
		{
			buff[k * x * 3 + i] = 255;
		}
	}
	// delete widht
	for (k = 0; k < y / 4; k++)
	{
		for (i = 0; i < x * 3; i++)
		{

			buff[((y - 1) * x * 3 - k * x * 3) + i] = 255;
		}
	}
	fwrite(buff, 1, x * y * b, fp);

	fclose(fp);
}

// 1Byte取得
unsigned char get_byte(unsigned char *buff)
{
	if (BuffIndex >= BuffSize)
		return 0;
	return buff[BuffIndex++];
}

// 2Byte取得
unsigned short get_word(unsigned char *buff)
{
	unsigned char h, l;
	h = get_byte(buff);
	l = get_byte(buff);
	return (h << 8) | l; //左移8位，与l做或运算
}

// 32位数据取得(仅在解压缩时使用)
unsigned int get_data(unsigned char *buff)
{
	unsigned char str = 0;
	unsigned int data = 0;
	str = get_byte(buff);

	// JPEG中以0xFF来作为特殊标记字符，如果某个像素的值为0xFF那么实际在存放的时候是以0xFF00来保存的，从而避免了其跟特殊字符0xFF之间产生混淆
	if (str == 0xff)
		if (get_byte(buff) == 0x00)
			str = 0xFF;
		else
			str = 0x00;
	data = str;
	str = get_byte(buff);
	if (str == 0xff)
		if (get_byte(buff) == 0x00)
			str = 0xFF;
		else
			str = 0x00;
	data = (data << 8) | str; // data（原数据为str）左移8位，与str做或运算
	str = get_byte(buff);
	if (str == 0xff)
		if (get_byte(buff) == 0x00)
			str = 0xFF;
		else
			str = 0x00;
	data = (data << 8) | str;
	str = get_byte(buff);
	if (str == 0xff)
		if (get_byte(buff) == 0x00)
			str = 0xFF;
		else
			str = 0x00;
	data = (data << 8) | str;
	return data;
}

// APP0处理
void GetAPP0(unsigned char *buff)
{
	unsigned short data;
	unsigned char str;
	unsigned int i;

	data = get_word(buff); // APP0字段总长度
						   // APP0因为不读也可以，所以不能互相交换就跳过副本
	for (i = 0; i < data - 2; i++)
	{
		str = get_byte(buff);
	}
}

// DQT处理
void GetDQT(unsigned char *buff)
{
	unsigned short data;
	unsigned char str;
	unsigned int i;
	unsigned int tablenum;

	data = get_word(buff); // 字段长度
	str = get_byte(buff);  // 表号

	printf("*** DQT Table %d\n", str);
	for (i = 0; i < 64; i++)
	{
		TableDQT[str][i] = get_byte(buff);
		printf(" %2d: %2x\n", i, TableDQT[str][i]);
	}
}

// DHT处理
void GetDHT(unsigned char *buff)
{
	unsigned short data;
	unsigned char str;
	unsigned int i;
	unsigned char max, count;
	unsigned short ShiftData = 0x8000, HuffmanData = 0x0000;
	unsigned int tablenum;

	data = get_word(buff); // 字段长度
	str = get_byte(buff);  // 表ID和表类型

	switch (str)
	{
	case 0x00:
		// Y直流成分
		tablenum = 0x00;
		break;
	case 0x10:
		// Y交流成分
		tablenum = 0x01;
		break;
	case 0x01:
		// CbCr直流成分
		tablenum = 0x02;
		break;
	case 0x11:
		// CbCr交流成分
		tablenum = 0x03;
		break;
	}

	printf("*** DHT Table/Number %d\n", tablenum);

	max = 0;
	for (i = 0; i < 16; i++) // 16个字节表示不同编码长度的码字个数
	{
		count = get_byte(buff); 
		TableHT[tablenum][i] = HuffmanData;
		TableHN[tablenum][i] = max;
		printf(" %2d: %4x,%2x\n", i, TableHT[tablenum][i], TableHN[tablenum][i]);
		max = max + count;
		while (!(count == 0))
		{
			HuffmanData += ShiftData;
			count--;
		}
		ShiftData = ShiftData >> 1; // 向右移位1位
	}

	printf("*** DHT Table %d\n", tablenum);
	for (i = 0; i < max; i++)
	{
		TableDHT[tablenum][i] = get_byte(buff);
		printf(" %2d: %2x\n", i, TableDHT[tablenum][i]);
	}
}

// SOF处理
void GetSOF(unsigned char *buff)
{
	unsigned short data;
	unsigned char str;
	unsigned int i;
	unsigned char count;
	unsigned char num;

	data = get_word(buff);		// 该字段数据长度
	str = get_byte(buff);		// 精度（通常是8位）
	BuffY = get_word(buff);		// 图像的高度
	BuffX = get_word(buff);		// 图像的宽度
	CompCount = get_byte(buff); // 数据的组件数
	printf(" CompCount: %d\n", CompCount);
	for (i = 0; i < CompCount; i++)
	{
		str = get_byte(buff); // 组件号
		num = str;
		printf(" Comp[%d]: %02X\n", i, str);
		str = get_byte(buff); // 采样比率
		CompSample[num] = str;
		printf(" Sample[%d]: %02X\n", i, str);
		str = get_byte(buff); // DQT表号
		CompDQT[num] = str;
		printf(" DQT[%d]: %02X\n", i, str);
	}

	if (CompCount == 1)
	{
		CompSampleX = 1;
		CompSampleY = 1;
	}
	else
	{
		CompSampleX = CompSample[1] & 0x0F; // 高四位水平采样因子，低四位垂直采样因子
		CompSampleY = (CompSample[1] >> 4) & 0x0F;
	}

	// 计算MCU的大小(一块)
	BuffBlockX = (int)(BuffX / (8 * CompSampleX));
	if (BuffX % (8 * CompSampleX) > 0)
		BuffBlockX++;
	BuffBlockY = (int)(BuffY / (8 * CompSampleY));
	if (BuffY % (8 * CompSampleY) > 0)
		BuffBlockY++;
	Buff = (unsigned char *)malloc(BuffBlockY * (8 * CompSampleY) * BuffBlockX * (8 * CompSampleX) * 3);

	printf(" size : %d x %d,(%d x %d)\n", BuffX, BuffY, BuffBlockX, BuffBlockY);
}

// SOS处理
void GetSOS(unsigned char *buff)
{
	unsigned short data;
	unsigned char str;
	unsigned int i;
	unsigned char count;
	unsigned char num;

	data = get_word(buff);	// 字段总长度
	count = get_byte(buff); // 颜色分量数
	for (i = 0; i < count; i++)
	{
		str = get_byte(buff); // 获得颜色分量ID
		num = str;
		printf(" CompNum[%d]: %02X\n", i, str);
		str = get_byte(buff); // 直流、交流系数表号
		CompDHT[num] = str;
		printf(" CompDHT[%d]: %02X\n", i, str);
	}
	str = get_byte(buff); // 谱选择开始
	str = get_byte(buff); // 谱选择结束
	str = get_byte(buff); // 谱选择
}

// huffman解码+逆量化+逆z字形
void HuffmanDecode(unsigned char *buff, unsigned char table, int *BlockData)
{
	unsigned int data;
	unsigned char zero;
	unsigned short code, huffman;
	unsigned char count = 0;
	unsigned int BitData;
	unsigned int i;
	unsigned char tabledqt, tabledc, tableac, tablen;
	unsigned char ZeroCount, DataCount;
	int DataCode;

	for (i = 0; i < 64; i++)
		BlockData[i] = 0x0; // 数据重置

	// 设定表格号码
	if (table == 0x00)
	{
		tabledqt = 0x00;
		tabledc = 0x00;
		tableac = 0x01;
	}
	else if (table == 0x01)
	{
		tabledqt = 0x01;
		tabledc = 0x02;
		tableac = 0x03;
	}
	else
	{
		tabledqt = 0x01;
		tabledc = 0x02;
		tableac = 0x03;
	}

	count = 0; // 以防万一
	while (count < 64)
	{
		// 当位计数的位置超过32时，获得新的数据。
		if (BitCount >= 32)
		{
			LineData = NextData;
			NextData = get_data(buff);
			BitCount -= 32;
		}
		// Huffman用解码的数据替换
		if (BitCount > 0)
		{
			BitData = (LineData << BitCount) | (NextData >> (32 - BitCount));
		}
		else
		{
			BitData = LineData;
		}

		// 使用的表的选择
		if (count == 0)
			tablen = tabledc;
		else
			tablen = tableac;
		code = (unsigned short)(BitData >> 16); // 代码使用16位
		// 找出哈夫曼代码在哪个位数
		for (i = 0; i < 16; i++)
		{
			if (TableHT[tablen][i] > code)
				break;
		}
		i--;

		code = (unsigned short)(code >> (15 - i)); // 把代码的下位对齐
		huffman = (unsigned short)(TableHT[tablen][i] >> (15 - i));


		// 计算huffman表的位置
		code = code - huffman + TableHN[tablen][i];
		ZeroCount = (TableDHT[tablen][code] >> 4) & 0x0F; // 零保留的个数
		DataCount = (TableDHT[tablen][code]) & 0x0F;	  // 接着的数据的位长

		// 跳过哈夫曼编码码字，获取数据
		DataCode = (BitData << (i + 1)) >> (16 + (16 - DataCount));

		// 头位为“0”则为负数据，对应的正数按位取反加1
		if (!(DataCode & (1 << (DataCount - 1))) && DataCount != 0)
		{
			DataCode |= (~0) << DataCount;
			DataCode += 1;
		}
		BitCount += (i + DataCount + 1); // 加上所使用的位数

		if (count == 0)
		{
			// C分量的情况下，成为数据。
			if (DataCount == 0)
				DataCode = 0x0;			// 如果DataCount为0，则数据为0。
			PreData[table] += DataCode; // DC分量要加起来
										// 逆量化 + z
			BlockData[zigzag_table[count]] = PreData[table] * TableDQT[tabledqt][count];
			count++;
		}
		else
		{
			if (ZeroCount == 0x0 && DataCount == 0x0)
			{
				break; // 在AC分量中出现EOB符号的情况下结束
			}
			else if (ZeroCount == 0xF && DataCount == 0x0)
			{
				count += 15; // 如果ZRL码到来，则视为15个零数据
			}
			else
			{
				count += ZeroCount;
				// 逆量化+ z
				BlockData[zigzag_table[count]] = DataCode * TableDQT[tabledqt][count];
			}
			count++;
		}
	}
}

const int C1_16 = 4017; // cos( pi/16) x4096
const int C2_16 = 3784; // cos(2pi/16) x4096
const int C3_16 = 3406; // cos(3pi/16) x4096
const int C4_16 = 2896; // cos(4pi/16) x4096
const int C5_16 = 2276; // cos(5pi/16) x4096
const int C6_16 = 1567; // cos(6pi/16) x4096
const int C7_16 = 799;	// cos(7pi/16) x4096

// 逆DCT
void DctDecode(int *BlockIn, int *BlockOut)
{
	int i;
	int s0, s1, s2, s3, s4, s5, s6, s7;
	int t0, t1, t2, t3, t4, t5, t6, t7;
	for (i = 0; i < 8; i++)
	{
		s0 = (BlockIn[0] + BlockIn[4]) * C4_16;
		s1 = (BlockIn[0] - BlockIn[4]) * C4_16;
		s3 = (BlockIn[2] * C2_16) + (BlockIn[6] * C6_16);
		s2 = (BlockIn[2] * C6_16) - (BlockIn[6] * C2_16);

		s7 = (BlockIn[1] * C1_16) + (BlockIn[7] * C7_16);
		s4 = (BlockIn[1] * C7_16) - (BlockIn[7] * C1_16);
		s6 = (BlockIn[5] * C5_16) + (BlockIn[3] * C3_16);
		s5 = (BlockIn[5] * C3_16) - (BlockIn[3] * C5_16);
		t0 = s0 + s3;
		t1 = s1 + s2;
		t3 = s0 - s3;
		t2 = s1 - s2;

		t4 = s4 + s5;
		t5 = s4 - s5;
		t7 = s7 + s6;
		t6 = s7 - s6;
		s6 = (t5 + t6) * 181 / 256; 
		s5 = (t6 - t5) * 181 / 256; 
		*BlockIn++ = (t0 + t7) >> 11;
		*BlockIn++ = (t1 + s6) >> 11;
		*BlockIn++ = (t2 + s5) >> 11;
		*BlockIn++ = (t3 + t4) >> 11;
		*BlockIn++ = (t3 - t4) >> 11;
		*BlockIn++ = (t2 - s5) >> 11;
		*BlockIn++ = (t1 - s6) >> 11;
		*BlockIn++ = (t0 - t7) >> 11;
	}

	BlockIn -= 64;
	for (i = 0; i < 8; i++)
	{
		s0 = (BlockIn[0] + BlockIn[32]) * C4_16;
		s1 = (BlockIn[0] - BlockIn[32]) * C4_16;
		s3 = BlockIn[16] * C2_16 + BlockIn[48] * C6_16;
		s2 = BlockIn[16] * C6_16 - BlockIn[48] * C2_16;
		s7 = BlockIn[8] * C1_16 + BlockIn[56] * C7_16;
		s4 = BlockIn[8] * C7_16 - BlockIn[56] * C1_16;
		s6 = BlockIn[40] * C5_16 + BlockIn[24] * C3_16;
		s5 = BlockIn[40] * C3_16 - BlockIn[24] * C5_16;

		t0 = s0 + s3;
		t1 = s1 + s2;
		t2 = s1 - s2;
		t3 = s0 - s3;
		t4 = s4 + s5;
		t5 = s4 - s5;
		t6 = s7 - s6;
		t7 = s6 + s7;

		s5 = (t6 - t5) * 181 / 256; // 1/sqrt(2)
		s6 = (t5 + t6) * 181 / 256; // 1/sqrt(2)

		BlockOut[0] = ((t0 + t7) >> 15);
		BlockOut[56] = ((t0 - t7) >> 15);
		BlockOut[8] = ((t1 + s6) >> 15);
		BlockOut[48] = ((t1 - s6) >> 15);
		BlockOut[16] = ((t2 + s5) >> 15);
		BlockOut[40] = ((t2 - s5) >> 15);
		BlockOut[24] = ((t3 + t4) >> 15);
		BlockOut[32] = ((t3 - t4) >> 15);

		BlockIn++;
		BlockOut++;
	}
	BlockOut -= 8;
}

// 4:1:1的解码处理
void Decode411(unsigned char *buff, int *BlockY, int *BlockCb, int *BlockCr)
{
	int BlockHuffman[64];
	int BlockYLT[64];
	int BlockYRT[64];
	int BlockYLB[64];
	int BlockYRB[64];
	int Block[64];
	unsigned int i;
	unsigned int m, n;

	for (n = 0; n < CompSampleY; n++)
	{
		for (m = 0; m < CompSampleX; m++)
		{
			HuffmanDecode(buff, 0x00, BlockHuffman);
			DctDecode(BlockHuffman, Block);
			for (i = 0; i < 64; i++)
			{
				BlockY[(int)(i / 8) * 8 * CompSampleX + (i % 8) + (m * 8) + (n * 64 * CompSampleY)] = Block[i];
			}
		}
	}

	if (CompCount > 1)
	{
		HuffmanDecode(buff, 0x01, BlockHuffman);
		DctDecode(BlockHuffman, BlockCb);

		HuffmanDecode(buff, 0x02, BlockHuffman);
		DctDecode(BlockHuffman, BlockCr);
	}
}

// YUV→RGB变换
void DecodeYUV(int *y, int *cb, int *cr, unsigned char *rgb)
{
	int r, g, b;
	int p, i;


	for (i = 0; i < ((CompCount > 1) ? 256 : 64); i++)
	{
		p = ((int)(i / 32) * 8) + ((int)((i % 16) / 2));

		r = 128 + y[i] + ((CompCount > 1) ? cr[p] * 1.402 : 0);

		if (r >= 255)
			r = 255;
		else if (r <= 0)
			r = 0;

		g = 128 + y[i] - ((CompCount > 1) ? cb[p] * 0.34414 : 0) - ((CompCount > 1) ? cr[p] * 0.71414 : 0);

		if (g >= 255)
			g = 255;
		else if (g <= 0)
			g = 0;

		b = 128 + y[i] + ((CompCount > 1) ? cb[p] * 1.772 : 0);

		if (b >= 255)
			b = 255;
		else if (b <= 0)
			b = 0;

		rgb[i * 3 + 0] = b;
		rgb[i * 3 + 1] = g;
		rgb[i * 3 + 2] = r;

	}
}

// 图像解码
void Decode(unsigned char *buff, unsigned char *rgb)
{
	int BlockY[256];
	int BlockCb[256];
	int BlockCr[256];
	int x, y, i, p;

	for (y = 0; y < BuffBlockY; y++)
	{
		for (x = 0; x < BuffBlockX; x++)
		{
			Decode411(buff, BlockY, BlockCb, BlockCr); // 4:1:1的解码
			DecodeYUV(BlockY, BlockCb, BlockCr, rgb);  // YUV-RGB変换
			for (i = 0; i < ((CompCount > 1) ? 256 : 64); i++)
			{
				if ((x * (8 * CompSampleX) + (i % (8 * CompSampleX)) < BuffX) && (y * (8 * CompSampleY) + i / (8 * CompSampleY) < BuffY))
				{
					p = y * (8 * CompSampleY) * BuffX * 3 + x * (8 * CompSampleX) * 3 + (int)(i / (8 * CompSampleY)) * BuffX * 3 + (i % (8 * CompSampleX)) * 3;
					Buff[p + 0] = rgb[i * 3 + 0];
					Buff[p + 1] = rgb[i * 3 + 1];
					Buff[p + 2] = rgb[i * 3 + 2];
				}
			}
		}
	}
}

void JpegDecode(unsigned char *buff)
{
	unsigned short data;
	unsigned int i;
	unsigned int Image = 0;
	unsigned char RGB[256 * 3];
	while (!(BuffIndex >= BuffSize))
	{
		if (Image == 0)
		{
			data = get_word(buff);
			switch (data)
			{
			case 0xFFD8: // SOI
				printf("Header: SOI\n");
				break;
			case 0xFFE0: // APP0
				printf("Header: APP0\n");
				GetAPP0(buff);
				break;
			case 0xFFDB: // DQT
				printf("Header: DQT\n");
				GetDQT(buff);
				break;
			case 0xFFC4: // DHT
				printf("Header: DHT\n");
				GetDHT(buff);
				break;
			case 0xFFC0: // SOF
				printf("Header: SOF\n");
				GetSOF(buff);
				break;
			case 0xFFDA: // SOS
				printf("Header: SOS\n");
				GetSOS(buff);
				Image = 1;
				// 数据的准备
				PreData[0] = 0x00;
				PreData[1] = 0x00;
				PreData[2] = 0x00;
				LineData = get_data(buff); // 获得32位的图像数据
				NextData = get_data(buff);
				BitCount = 0;
				break;
			case 0xFFD9: // EOI
				printf("Header: EOI\n");
				break;
			default:
				// 判別读不完的报头
				printf("Header: other(%X)\n", data);
				if ((data & 0xFF00) == 0xFF00 && !(data == 0xFF00))
				{
					data = get_word(buff);
					for (i = 0; i < data - 2; i++)
					{
						get_byte(buff);
					}
				}
				break;
			}
		}
		else
		{
			// 解(SOS来了)
			printf("/****Image****/\n");
			Decode(buff, RGB);
		}
	}
}
// 主函数
int main(int argc, char *argv[])
{
	unsigned char *buff;
	unsigned char *desData;
	FILE *fp;

	// 输出各种类型数据所占的字节数
	printf(" sizeof(char):           %02lu\n", sizeof(char));
	printf(" sizeof(unsigned char):  %02lu\n", sizeof(unsigned char));
	printf(" sizeof(short):          %02lu\n", sizeof(short));
	printf(" sizeof(unsigned short): %02lu\n", sizeof(unsigned short));
	printf(" sizeof(int):            %02lu\n", sizeof(int));
	printf(" sizeof(unsigned int):   %02lu\n", sizeof(unsigned int));
	printf(" sizeof(long):           %02lu\n", sizeof(long));
	printf(" sizeof(unsigned long):  %02lu\n", sizeof(unsigned long));

	printf(" sizeof:                 %02lu\n", sizeof(BITMAPFILEHEADER));
	printf(" sizeof:                 %02lu\n", sizeof(BITMAPINFOHEADER));
	fp = fopen("jpeg.jpg", "rb");
	if ((fp = fopen("jpeg.jpg", "rb")) == NULL) //打开待解码的JPG图片
	{
		printf("hello");
		perror(0);
		exit(0);
	}

	BuffSize = 0;	  // 获取文件大小
	while (!feof(fp)) // feof(fp)文件结束检测函数
	{
		fgetc(fp); // 从文件中读入一个字符
		BuffSize++;
	}
	BuffSize--;
	rewind(fp); // 使文件指针返回最初
	buff = (unsigned char *)malloc(BuffSize); // 开辟相同大小的内存空间用于存放图像数据
	fread(buff, 1, BuffSize, fp);			  // 将图片数据读入开辟的内存中
	BuffIndex = 0;

	JpegDecode(buff); // JPEG解码

	printf("Finished decode\n");
	printf("%hhn", buff);
    BmpSave(argv[1], Buff, BuffX, BuffY, 3); // Bitmap
	printf("Saved BMP\n");
	//jpeg save
	char BMP_filename[64];									//保存文件名（bmp文件名）
	char JPG_filename[64];									//保存Jpeg压缩后的文件名
	unsigned short int Ximage_original, Yimage_original; 	//the original image dimensions,
															//before we made them divisible by 8
	unsigned char len_filename;
	bitstring fillbits; 									//filling bitstring for the bit alignment of the EOI marker
	if (argc > 1) 											//大于1说明有其他参数输入
	{
		strcpy(BMP_filename, argv[1]);
		if (argc > 2)
			strcpy(JPG_filename, argv[2]);
		else
		{
			strcpy(JPG_filename, BMP_filename);
			len_filename = strlen(BMP_filename);
			strcpy(JPG_filename + (len_filename - 3), "jpg");
		}
	}
	load_bitmap(BMP_filename, &Ximage_original, &Yimage_original);		//将图像获取到buffer所指向的内存空间中
	fp_jpeg_stream = fopen(JPG_filename, "wb");							//新建一个二进制文件，只允许写，用于存放压缩后的Jpeg数据
	init_all();												//初始化一些标记头文件，以及开辟一个存储空间用于存放正负数的编码（正数使用源码，负数使用反码）
	SOF0info.width = Ximage_original;
	SOF0info.height = Yimage_original;

	writeword(0xFFD8);	//写入SOI图像开始标志

	write_APP0info();	//写入JFIF应用数据块
	write_DQTinfo();	//写入量化表
	write_SOF0info();	//帧图像分开始
	write_DHTinfo();	//写入哈夫曼表
	write_SOSinfo();	//扫描开始

	bytenew = 0;														//The byte that will be written in the JPG file
	bytepos = 7;														//bit position in the byte we write (bytenew)
																		//should be <= 7 and >=0	
	main_encoder();		//编码子函数

	//Do the bit alignment of the EOI marker
	if (bytepos >= 0)
	{
		fillbits.length = bytepos + 1;
		fillbits.value = (1 << (bytepos + 1)) - 1;
		writebits(fillbits);
	}
	writeword(0xFFD9); 			//EOI图像结束标志
    printf("jpeg save\n");
	free(buffer);				//释放内存
	free(category_alloc);
	free(bitcode_alloc);
	fclose(fp_jpeg_stream);
	fclose(fp);
	free(buff);

	return 0;
}
