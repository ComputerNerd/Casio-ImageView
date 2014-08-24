#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <png.h>
#include <zlib.h>
#include <fxcg/keyboard.h>
#include <fxcg/misc.h>
#include "filegui.h"
#define VRAM_ADDRESS 0xA8000000
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
			memset((unsigned short*)VRAM_ADDRESS,0,384*216*2);
			char buf[128];
			FBL_Filelist_getFilename(list,buf,127);
			if(!strncmp(buf,"\\\\fls0\\",7)){
				//load png file
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
					puts("Make sure what you are loading is a valid png");
					break;
				}
				png_structp png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
				if (!png_ptr){
					fclose(fp);
					puts("Error creating png read struct");
					break;
				}
				png_infop info_ptr = png_create_info_struct(png_ptr);
				if (!info_ptr){
					fclose(fp);
					puts("Error creating png info struct");
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
				png_set_background_fixed(png_ptr,&background_color,PNG_BACKGROUND_GAMMA_SCREEN, 0, PNG_FP_1);
				png_read_update_info(png_ptr, info_ptr);
				//Read the image two rows at a time for bilinear scaling
				
				uint16_t * vram=(uint16_t*)VRAM_ADDRESS;
				unsigned h;
				if((width==384)&&(height<=216)){
					png_bytep row_pointers[216];
					//Simply center image and copy data
					vram+=((216-height)/2)*384;
					uint8_t*imgTmp=alloca(width*height*3);
					uint8_t*d=imgTmp;
					for(h=0;h<height;++h){
						row_pointers[h]=d;
						d+=384*3;
					}
					png_read_image(png_ptr, row_pointers);
					d=imgTmp;
					for(h=0;h<width*height;++h){
						*vram++=((d[0] & 0xF8) << 8) | ((d[1] & 0xFC) << 3) | (d[2] >> 3);
						d+=3;
					}
				}else{
					unsigned w2,h2,centerx,centery;
					unsigned xpick=((384<<16)/width)+1,ypick=((216<<16)/height)+1;
					if(xpick==ypick){
						w2=384;
						h2=216;
						centerx=centery=0;
					}else if(xpick<ypick){
						w2=384;
						h2=height*384/width;
						centerx=0;
						centery=(216-h2)/2;
					}else{
						w2=width*216/height;
						h2=216;
						centerx=(384-w2)/2;
						centery=0;
					}
					// EDIT: added +1 to account for an early rounding problem
					unsigned x_ratio = ((width<<12)/w2)+1;
					unsigned y_ratio = ((height<<12)/h2)+1;
					uint8_t * decodeBuf=alloca(width*3*2);//Enough memory to hold two rows of data
					vram+=(centery*384)+centerx;
					unsigned i,j,yo=0;
					png_bytep row_pointers[2];
					row_pointers[0]=decodeBuf;
					row_pointers[1]=decodeBuf+(width*3);
					png_read_rows(png_ptr, row_pointers, NULL,2);
					unsigned left=height-2;
					for (i=0;i<h2;++i){
						//Deterimin how many lines to read
						unsigned read=((y_ratio * i)>>12)-yo;
						if(read){
							while(read>=3){
								png_read_row(png_ptr,decodeBuf,NULL);//Apperntly this is the right way to skip a row http://osdir.com/ml/graphics.png.devel/2008-05/msg00038.html
								--read;
								--left;
							}
							if(read==1){
								memcpy(decodeBuf,decodeBuf+(width*3),width*3);
								png_read_row(png_ptr,decodeBuf+(width*3),NULL);
								--left;
							}else{
								png_read_rows(png_ptr, row_pointers, NULL,2);
								left-=2;
							}
						}
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
						
							*vram++=(((A[0]+B[0]+C[0]+D[0]) & 0xF8) << 8) | (((A[1]+B[1]+C[1]+D[1]) & 0xFC) << 3) | ((A[2]+B[2]+C[2]+D[2]) >> 3);
							//*out++=A+B+C+D;
						}
						vram+=384-w2;
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
				Bdisp_PutDisp_DD();
				int col=0,row=0;
				unsigned short keycode=0;
				GetKeyWait_OS(&col,&row,0,0,0,&keycode);//Better solution to avoid border
			}
		}
	}
}
