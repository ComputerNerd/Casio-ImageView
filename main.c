#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <png.h>
#include <zlib.h>
#include <fxcg/keyboard.h>
#include <fxcg/misc.h>
#include "filegui.h"
#define VRAM_WIDTH 396
#define VRAM_HEIGHT 224
static FILE*fp;
void abort(void){
	int x=0,y=160;
	PrintMini(&x,&y,"Abort called",0,0xFFFFFFFF,0,0,0xFFFF,0,1,0);
	int key;
	if(fp)
		fclose(fp);
	while(1)
		GetKey(&key);
}
#define LCD_GRAM	0x202
#define LCD_BASE	0xB4000000
#define SYNCO() __asm__ volatile("SYNCO\n\t":::"memory");
// Module Stop Register 0
#define MSTPCR0		(volatile unsigned*)0xA4150030
// DMA0 operation register
#define DMA0_DMAOR	(volatile unsigned short*)0xFE008060
#define DMA0_SAR_0	(volatile unsigned*)0xFE008020
#define DMA0_DAR_0	(volatile unsigned*)0xFE008024
#define DMA0_TCR_0	(volatile unsigned*)0xFE008028
#define DMA0_CHCR_0	(volatile unsigned*)0xFE00802C
void DmaWaitNext(void){
	while(1){
		if((*DMA0_DMAOR)&4)//Address error has occurred stop looping
			break;
		if((*DMA0_CHCR_0)&2)//Transfer is done
			break;
	}
	SYNCO();
	*DMA0_CHCR_0&=~1;
	*DMA0_DMAOR=0;
}
static void DoDMAlcdNonblockStrip(unsigned y1,unsigned y2,unsigned addr){
	Bdisp_WriteDDRegister3_bit7(1);
	Bdisp_DefineDMARange(0,VRAM_WIDTH-1,y1,y2);
	Bdisp_DDRegisterSelect(LCD_GRAM);

	*MSTPCR0&=~(1<<21);//Clear bit 21
	*DMA0_CHCR_0&=~1;//Disable DMA on channel 0
	*DMA0_DMAOR=0;//Disable all DMA
	*DMA0_SAR_0=addr; //Source address is VRAM
	*DMA0_DAR_0=LCD_BASE&0x1FFFFFFF;//Destination is LCD
	*DMA0_TCR_0=((y2-y1+1)*VRAM_WIDTH)/16;//Transfer count bytes/32
	*DMA0_CHCR_0=0x00101400;
	*DMA0_DMAOR|=1;//Enable DMA on all channels
	*DMA0_DMAOR&=~6;//Clear flags
	*DMA0_CHCR_0|=1;//Enable channel0 DMA
}
int main(void){
	Bdisp_EnableColor(1);
	while(1){
		struct FBL_Filelist_Data *list = FBL_Filelist_cons("\\\\fls0\\", "*.png", "Open file (*.png)");
		// Actual GUI happens here
		FBL_Filelist_go(list);

		// Optional
		DrawFrame(COLOR_BLACK);
		
		// Check if a file was returned
		if(list->result == 1){
			char buf[128];
			FBL_Filelist_getFilename(list,buf,127);
			if(!strncmp(buf,"\\\\fls0\\",7)){
				// Load PNG file
				fp = fopen(buf, "rb");
				if(!fp){
					perror("Error while reading file:");
					break;
				}
				uint8_t header[8];
				if(fread(header, 1, 8, fp)!=8){
					fclose(fp);
					puts("Read error");
					break;
				}
				int is_png = !png_sig_cmp(header, 0, 8);
				if(!is_png){
					fclose(fp);
					puts("Make sure what you are loading is a valid PNG");
					break;
				}
				png_structp png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
				if (!png_ptr){
					fclose(fp);
					puts("Error creating PNG read struct");
					break;
				}
				png_infop info_ptr = png_create_info_struct(png_ptr);
				if (!info_ptr){
					fclose(fp);
					puts("Error creating PNG info struct");
					png_destroy_read_struct(&png_ptr,(png_infopp)NULL, (png_infopp)NULL);
					break;
				}
				png_init_io(png_ptr, fp);
				png_set_sig_bytes(png_ptr, 8);
				png_read_info(png_ptr, info_ptr);
				//get information about the image
				unsigned width,height;
				int color_type,bit_depth;
				png_get_IHDR(png_ptr, info_ptr,&width,&height,&bit_depth,&color_type,0,0,0);
				if (color_type == PNG_COLOR_TYPE_PALETTE)
					png_set_palette_to_rgb(png_ptr);
				if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
					png_set_gray_to_rgb(png_ptr);
				if (bit_depth == 16){
					#if PNG_LIBPNG_VER >= 10504
						   png_set_scale_16(png_ptr);
					#else
						   png_set_strip_16(png_ptr);
					#endif
				}
				png_color_16 background_color;
				memset(&background_color,0,sizeof(png_color_16));//Black

				png_set_background_fixed(png_ptr,&background_color,PNG_BACKGROUND_GAMMA_SCREEN, 0, PNG_FP_1);
				png_read_update_info(png_ptr, info_ptr);
				//Read the image two rows at a time for bilinear scaling
				
				uint16_t * lineBuffer = (uint16_t*)0xE5017000; // Holds four lines. We need four lines because if we use less it's not a multiple of 32 bytes.
				unsigned h;
				memset(lineBuffer, 0, VRAM_WIDTH * 2 * 4); // The border is black. In both cases we need this cleared.
				unsigned firstRowOnBottomWithoutPixels, onLine, nextDmaRow;
				if((width==VRAM_WIDTH)&&(height<=VRAM_HEIGHT)){
					// Decode the PNG file without scaling.
					uint8_t trueColorLine[VRAM_WIDTH * 3];
					unsigned i;

					unsigned firstRowWithPixels = (VRAM_HEIGHT - height) / 2;
					nextDmaRow = 0;

					for (i = 0; i < firstRowWithPixels; i += 4) {
						DmaWaitNext(); // This is before the DMA call because when we are done with the loop assuming that there are no border lines on the bottom we want to get started right away decoding the PNG.
						DoDMAlcdNonblockStrip(i, i + 3, lineBuffer);
						nextDmaRow = i + 4;
					}
					onLine = (firstRowWithPixels & 3);

					uint16_t * lineBufferPixel = lineBuffer;
					lineBufferPixel += onLine * VRAM_WIDTH;
					for(h=0;h<height;++h){
						uint8_t * srcTrueColorPixel = trueColorLine;

						png_read_row(png_ptr, trueColorLine, NULL);
						for (i = 0; i < VRAM_WIDTH; ++i) {
							*lineBufferPixel++ = ((srcTrueColorPixel[0] & 0xF8) << 8) | ((srcTrueColorPixel[1] & 0xFC) << 3) | (srcTrueColorPixel[2] >> 3);
							srcTrueColorPixel += 3;
						}

						if (onLine == 3) {
							DmaWaitNext();
							DoDMAlcdNonblockStrip(nextDmaRow, nextDmaRow + 3, lineBuffer);

							onLine = 0;
							nextDmaRow += 4;
							lineBufferPixel = lineBuffer;
						} else {
							++onLine;
						}
					}

					firstRowOnBottomWithoutPixels = firstRowWithPixels + height;
				}else{
					unsigned w2,h2,centerx,centery;
					unsigned xpick=(VRAM_WIDTH<<16)/width,ypick=(VRAM_HEIGHT<<16)/height;
					if(xpick==ypick){
						w2=VRAM_WIDTH;
						h2=VRAM_HEIGHT;
						centerx=centery=0;
					}else if(xpick<ypick){
						w2=VRAM_WIDTH;
						h2=height*VRAM_WIDTH/width;
						centerx=0;
						centery=(VRAM_HEIGHT-h2)/2;
					}else{ // ypick > xpick
						w2=width*VRAM_HEIGHT/height;
						h2=VRAM_HEIGHT;
						centerx=(VRAM_WIDTH-w2)/2;
						centery=0;
					}
					unsigned x_ratio = (width<<12)/w2;
					unsigned y_ratio = (height<<12)/h2;
					uint8_t * decodeBuf=alloca(width*3*2);//Enough memory to hold two rows of data
					unsigned i,j,yo=0;
					png_read_row(png_ptr,decodeBuf,NULL);
					unsigned left=height-1;
					if(y_ratio>=4096){
						unsigned skip=y_ratio>>12;
						while(skip--){
							png_read_row(png_ptr,decodeBuf+(width*3),NULL);
							--left;
						}
					}else{
						png_read_row(png_ptr,decodeBuf+(width*3),NULL);
						--left;
					}

					// First draw the border. centery is the first row number that will have pixels. centery + h2 is the first row where the bottom border starts.
					firstRowOnBottomWithoutPixels = centery + h2;
					nextDmaRow = 0;
					for (i = 0; i < centery; i += 4) {
						DmaWaitNext();
						DoDMAlcdNonblockStrip(i, i + 3, lineBuffer);
						nextDmaRow = i + 4;
					}
					onLine = (centery & 3);
					// Now that we are done drawing some of the rows that are 100% border we can start drawing the image centered in the middle. The key will be ensuring that we never overwrite the border pixels on the left and the right.

					for(i=0;i<h2;++i){
						//Determine how many lines to read
						unsigned read=((y_ratio * i)>>12)-yo;
						if(read){
							if(read==1){
								memcpy(decodeBuf,decodeBuf+(width*3),width*3);
								png_read_row(png_ptr,decodeBuf+(width*3),NULL);
								--left;
							}else{
								png_read_row(png_ptr,decodeBuf,NULL);
								--left;
								while(--read){
									png_read_row(png_ptr,decodeBuf+(width*3),NULL);//Apparently this is the right way to skip a row according to: http://osdir.com/ml/graphics.png.devel/2008-05/msg00038.html
									--left;
								}
							}
						}
						uint16_t * lineBufPtr = lineBuffer;
						lineBufPtr += onLine * VRAM_WIDTH;
						lineBufPtr += centerx;
						for(j=0;j<w2;++j){
							unsigned A[3],B[3],C[3],D[3];
							unsigned x = x_ratio * j;
							unsigned y = y_ratio * i;
							unsigned x_diff = x&4095;
							unsigned y_diff = y&4095;
							unsigned ix_diff=4095-x_diff;
							unsigned iy_diff=4095-y_diff;
							x>>=12;
							y>>=12;
							yo=y;
							unsigned rgb;
							for(rgb=0;rgb<3;++rgb){
								A[rgb] = (decodeBuf[x*3+rgb]*ix_diff*iy_diff)>>24;
								B[rgb] = (decodeBuf[(x+1)*3+rgb]*(x_diff)*iy_diff)>>24;
								C[rgb] = (decodeBuf[(x+width)*3+rgb]*(y_diff)*ix_diff)>>24;
								D[rgb] = (decodeBuf[(x+width+1)*3+rgb]*(x_diff*y_diff))>>24;
							}
							// Y = A(1-w)(1-h) + B(w)(1-h) + C(h)(1-w) + Dwh
						
							*lineBufPtr++=(((A[0]+B[0]+C[0]+D[0]) & 0xF8) << 8) | (((A[1]+B[1]+C[1]+D[1]) & 0xFC) << 3) | ((A[2]+B[2]+C[2]+D[2]) >> 3);
							//*out++=A+B+C+D;
						}
						if (onLine == 3) {
							DmaWaitNext();
							DoDMAlcdNonblockStrip(nextDmaRow, nextDmaRow + 3, lineBuffer);

							onLine = 0;
							nextDmaRow += 4;
						} else {
							++onLine;
						}
					}
					while(left--){
						png_read_row(png_ptr,decodeBuf,NULL);//Avoid a too much data warning
					}
				}
				//cleanup
				png_read_end(png_ptr,(png_infop)NULL);
				png_destroy_read_struct(&png_ptr, &info_ptr,(png_infopp)NULL);
				fclose(fp);
				fp=0;

				unsigned isCleared[4];
				memset(isCleared, 0, sizeof(isCleared));
				for (unsigned i = firstRowOnBottomWithoutPixels; i < VRAM_HEIGHT; ++i) {
					if (!isCleared[onLine]) {
						memset(lineBuffer + (VRAM_WIDTH * onLine), 0, VRAM_WIDTH * 2);
						isCleared[onLine] = 1;
					}
					if (onLine == 3) {
						DmaWaitNext();
						DoDMAlcdNonblockStrip(nextDmaRow, nextDmaRow + 3, lineBuffer);

						onLine = 0;
						nextDmaRow += 4;
					} else {
						++onLine;
					}
				}

				DmaWaitNext(); // Ensure that we are done with DMA.

				int col=0,row=0;
				unsigned short keycode=0;
				GetKeyWait_OS(&col,&row,0,0,0,&keycode);//Better solution to avoid border
			}
		}
	}
}
