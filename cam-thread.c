/*
 * Copyright (C) 2015-2016 Thomas <tomas123 @ EEVblog Electronics Community Forum>
 * Copyright (C) 2021 Nicole Faerber <nicole.faerber@dpin.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <gtk/gtk.h>
#include <cJSON.h>

// from main thread
void update_fb(void);


#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>
#include <time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <assert.h>
 
#include <fcntl.h>
#include <math.h>

#include "cam-thread.h"
#include "planck.h"


// -----------------START-ORG-CODE------------------------------------------

#define VENDOR_ID 0x09cb
#define PRODUCT_ID 0x1996

static struct libusb_device_handle *devh = NULL;

static int FFC =   0; // detect FFC

// -- buffer for EP 0x85 chunks ---------------
#define BUF85SIZE 1048576  // size got from android app
static int buf85pointer = 0;
static unsigned char buf85[BUF85SIZE];
static unsigned char fb_proc[160*120]; //, fb_proc2[160*120*3];



static struct t_data_t *tdata;

// EP81 device status message (JSON)
// {
//   "type":"batteryChargingCurrentUpdate",
//   "data":
//   {
//     "chargingCurrent":0
//   }
// }
// {
//   "type":"batteryChargingStateUpdate",
//   "data":
//   {
//     "chargingState":"stateNoCharging"
//   }
// }
// {
//    "type":"batteryVoltageUpdate",
//    "data":
//    {
//      "voltage":3.76999998092651,
//      "percentage":77
//    }
// }

void
parse_status(unsigned char *buf)
{
	cJSON *status_json = cJSON_Parse((char *)buf);
	const cJSON *res = NULL;

	if (status_json == NULL)
		return;

	res = cJSON_GetObjectItemCaseSensitive(status_json, "shutterState");
	if (cJSON_IsString(res) && (res->valuestring != NULL)) {
		if (strncmp(res->valuestring,"FFC",3)==0) {
			tdata->shutter_state.shutterState=sFFC;
		} else if (strncmp(res->valuestring,"ON",2)==0) {
			tdata->shutter_state.shutterState=sON;
		} else {
			tdata->shutter_state.shutterState=sUNKNOWN;
		}
	}
	res = cJSON_GetObjectItemCaseSensitive(status_json, "shutterTemperature");
	if (cJSON_IsNumber(res)) {
		tdata->shutter_state.shutterTemperature=res->valuedouble;
	}
	res = cJSON_GetObjectItemCaseSensitive(status_json, "usbNotifiedTimestamp");
	if (cJSON_IsNumber(res)) {
		tdata->shutter_state.usbNotifiedTimestamp=res->valuedouble;
	}
	res = cJSON_GetObjectItemCaseSensitive(status_json, "usbEnqueuedTimestamp");
	if (cJSON_IsNumber(res)) {
		tdata->shutter_state.usbEnqueuedTimestamp=res->valuedouble;
	}
	res = cJSON_GetObjectItemCaseSensitive(status_json, "ffcState");
	if (cJSON_IsString(res) && (res->valuestring != NULL)) {
		if (strncmp(res->valuestring,"FFC_VALID_RAD",13)==0) {
			tdata->shutter_state.ffcState=FFC_VALID_RAD;
		} else if (strncmp(res->valuestring,"FFC_PROGRESS",12)==0) {
			tdata->shutter_state.ffcState=FFC_PROGRESS;
		} else {
			tdata->shutter_state.ffcState=FFC_UNKNOWN;
		}
	}

	cJSON_Delete(status_json);
}

void
parse_config_in(unsigned char *buf)
{
	cJSON *config_json = cJSON_Parse((char *)buf);
	const cJSON *res = NULL;
	const cJSON *res2 = NULL;
	const cJSON *res3 = NULL;

	if (config_json == NULL) {
		fprintf(stderr, "config msg parse json failed\n");
		return;
	}// else
	//	fprintf(stderr, "config msg parse json\n%s\n", buf);

	res = cJSON_GetObjectItemCaseSensitive(config_json, "type");
	res2 = cJSON_GetObjectItemCaseSensitive(config_json, "data");
	if (cJSON_IsString(res) && (res->valuestring != NULL)) {
		if (strncmp(res->valuestring,"batteryVoltageUpdate",20)==0) {
			res3 = cJSON_GetObjectItemCaseSensitive(res2, "voltage");
			if (cJSON_IsNumber(res3)) {
				tdata->battery_state.voltage = res3->valuedouble;
				// printf("bat %.2fV\n", res3->valuedouble);
			}
			res3 = cJSON_GetObjectItemCaseSensitive(res2, "percentage");
			if (cJSON_IsNumber(res3)) {
				tdata->battery_state.percentage = res3->valueint;
				// printf("bat %f%%\n", res3->valuedouble);
			}
		}
		if (strncmp(res->valuestring,"batteryChargingCurrentUpdate",28)==0) {
			res3 = cJSON_GetObjectItemCaseSensitive(res2, "chargingCurrent");
			if (cJSON_IsNumber(res3)) {
				tdata->battery_state.chargingCurrent = res3->valuedouble;
				// printf("bat %.2fV\n", res3->valuedouble);
			}
		}
		if (strncmp(res->valuestring,"batteryChargingStateUpdate",26)==0) {
			res3 = cJSON_GetObjectItemCaseSensitive(res2, "chargingCurrent");
			if (cJSON_IsString(res3) && (res3->valuestring != NULL)) {
				printf("bat chg state '%s'\n", res3->valuestring);
			}
		}
	}

	cJSON_Delete(config_json);
}


double
raw2temperature(unsigned short RAW)
{
	// mystery correction factor
	RAW *=4;
	// calc amount of radiance of reflected objects ( Emissivity < 1 )
	double RAWrefl=PlanckR1/(PlanckR2*(exp(PlanckB/(tdata->tempreflected/*TempReflected*/+273.15))-PlanckF))-PlanckO;
	// get displayed object temp max/min
	double RAWobj=(RAW-(1-tdata->emissivity/*Emissivity*/)*RAWrefl)/tdata->emissivity/*Emissivity*/;
	// calc object temperature

	return PlanckB/log(PlanckR1/(PlanckR2*(RAWobj+PlanckO))+PlanckF)-273.15;  
}


void vframe(char ep[],char EP_error[], int r, int actual_length, unsigned char buf[], unsigned char *colormap) 
{
	//static int fcnt=0;
	// error handler
	time_t now1;
	now1 = time(NULL); 
	if (r < 0) {
		if (strcmp (EP_error, libusb_error_name(r))!=0) {       
			strcpy(EP_error, libusb_error_name(r));
			fprintf(stderr, "\n: %s >>>>>>>>>>>>>>>>>bulk transfer (in) %s:%i %s\n", ctime(&now1), ep , r, libusb_error_name(r));
			sleep(1);
		}
		return;
	}
  
	// reset buffer if the new chunk begins with magic bytes or the buffer size limit is exceeded
	unsigned char magicbyte[4]={0xEF,0xBE,0x00,0x00};

	if  ((strncmp ((char *)buf, (char *)magicbyte,4)==0 ) || ((buf85pointer + actual_length) >= BUF85SIZE)) {
		//printf(">>>>>>>>>>>begin of new frame<<<<<<<<<<<<<\n");
		buf85pointer=0;
	}

	// printf("actual_length %d !!!!!\n", actual_length);

	memmove(buf85+buf85pointer, buf, actual_length);
	buf85pointer=buf85pointer+actual_length;
  
	if  ((strncmp ((char *)buf85, (char *)magicbyte,4)!=0 )) {
		//reset buff pointer
		buf85pointer=0;
		printf("Reset buffer because of bad Magic Byte!\n");
		return;
	}
      
	// a quick and dirty job for gcc
	uint32_t FrameSize   = buf85[ 8] + (buf85[ 9] << 8) + (buf85[10] << 16) + (buf85[11] << 24);
	uint32_t ThermalSize = buf85[12] + (buf85[13] << 8) + (buf85[14] << 16) + (buf85[15] << 24);
	uint32_t JpgSize     = buf85[16] + (buf85[17] << 8) + (buf85[18] << 16) + (buf85[19] << 24);
	uint32_t StatusSize  = buf85[20] + (buf85[21] << 8) + (buf85[22] << 16) + (buf85[23] << 24);

	if ( (FrameSize+28) > (buf85pointer) )  {
		// wait for next chunk
		return;
	}

	if (StatusSize > 10) {
		parse_status(&buf85[28+ThermalSize+JpgSize]);
	}

	int v;
	// get a full frame, first print the status
	buf85pointer=0;
  
	unsigned short *pix=tdata->raw_ir_buffer;   // original Flir 16 Bit RAW
	int x, y;

	// fb_proc = malloc(160 * 128); // 8 Bit gray buffer really needs only 160 x 120
	memset(fb_proc, 128, 160*120);       // sizeof(fb_proc) doesn't work, value depends from LUT
  
	//fb_proc2 = malloc(160 * 128 * 3); // 8x8x8  Bit RGB buffer 

	int min = 0x10000, max = 0;
	float rms = 0;

	// Make a unsigned short array from what comes from the thermal frame
	// find the max, min and RMS (not used yet) values of the array
	//int maxx, maxy;
	for (y = 0; y < 120; ++y) {
		for (x = 0; x < 160; ++x) {
			if (x<80) 
				v = buf85[2*(y * 164 + x) +32]+256*buf85[2*(y * 164 + x) +33];
			else
				v = buf85[2*(y * 164 + x) +32+4]+256*buf85[2*(y * 164 + x) +33+4];   
			pix[y * 160 + x] = v;   // unsigned char!!
      
			if (v < min)
				min = v;
			if (v > max) {
				max = v; //maxx = x; maxy = y;
			}
			rms += v * v;      
		}
	}

	// RMS used later
	//  rms /= 160 * 120;
	//  rms = sqrtf(rms);
  
	// scale the data in the array
	int delta = max - min;
	if (!delta)
		delta = 1;   // if max = min we have divide by zero
	int scale = 0x10000 / delta;

	for (y = 0; y < 120; ++y) {   //120
		for (x = 0; x < 160; ++x) {   //160
			int v = (pix[y * 160 + x] - min) * scale >> 8;

			// fb_proc is the gray scale frame buffer
			fb_proc[y * 160 + x] = v;   // unsigned char!!
		}
	}
  
	// calc medium of 2x2 center pixels
	int med = (pix[59 * 160 + 79]+pix[59 * 160 + 80]+pix[60 * 160 + 79]+pix[60 * 160 + 80])/4;

	tdata->t_min = raw2temperature(min);
	tdata->t_max = raw2temperature(max);
	tdata->t_center = raw2temperature(med);

	if (tdata->ir_buffer == NULL) {
		tdata->ir_buffer = (unsigned char *)malloc(640*480*4);
		fprintf(stderr, "nb\n");
	}
	for (y = 0; y < 120; ++y) {
		for (x = 0; x < 160; ++x) {
			// fb_proc is the gray scale frame buffer
			v=fb_proc[y * 160 + x] ;   // unsigned char!!
			tdata->ir_buffer[4*y * 160 + x*4] = 
				tdata->color_palette[3 * v + 2];  // B
			tdata->ir_buffer[(4*y * 160 + x*4)+1] = 
				tdata->color_palette[3 * v + 1]; // G
			tdata->ir_buffer[(4*y * 160 + x*4)+2] = 
				tdata->color_palette[3 * v]; // R
//			ir_buffer[(4*y * 160 + x*4)+3] = 
//				0x00; // A, empty
		}
	}
    
	if (tdata->jpeg_size == 0 && JpgSize > 0) {
		tdata->jpeg_size=JpgSize;
		tdata->jpeg_buffer=(unsigned char *)malloc(tdata->jpeg_size);
		memcpy(tdata->jpeg_buffer, &buf85[28+ThermalSize], tdata->jpeg_size);
	}
 
	if (strncmp ((char *)&buf85[28+ThermalSize+JpgSize+17],"FFC",3)==0) {
		FFC=1;  // drop all FFC frames
	} else {
		if (FFC==1) {
			FFC=0; // drop first frame after FFC
		} else {
			// write(fdwr2, fb_proc2, framesize2);  // colorized RGB Thermal Image
			update_fb();
		}
	}
	// free memory
	//free(fb_proc);                    // thermal RAW
	//free(fb_proc2);                   // visible jpg
}

static int
find_lvr_flirusb(void)
{
	devh = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, PRODUCT_ID);

	return devh ? 0 : -EIO;
}
 
void
print_bulk_result(char ep[],char EP_error[], int r, int actual_length, unsigned char buf[])
{
time_t now1;
int i;

	now1 = time(NULL);
	if (r < 0) {
		if (strcmp (EP_error, libusb_error_name(r))!=0) {
			strcpy(EP_error, libusb_error_name(r));
			fprintf(stderr, "\n: %s >>>>>>>>>>>>>>>>>bulk transfer (in) %s:%i %s\n", ctime(&now1), ep , r, libusb_error_name(r));
			sleep(1);
		}
		//return 1;
	} else {
		printf("\n: %s bulk read EP %s, actual length %d\nHEX:\n",ctime(&now1), ep ,actual_length);
		// write frame to file          
		/*
		char filename[100];
		sprintf(filename, "EP%s#%05i.bin",ep,filecount);
		//filecount++;
		FILE *file = fopen(filename, "wb");
		fwrite(buf, 1, actual_length, file);
		fclose(file);
		*/         
		// hex print of first byte
#if 1
		for (i = 0; i <  (((200)<(actual_length))?(200):(actual_length)); i++) {
			printf(" %02x", buf[i]);
		}
#else
		printf("\nSTRING:\n");	
		for (i = 0; i <  (((200)<(actual_length))?(200):(actual_length)); i++) {
			if (isascii(buf[i])) {
				printf("%c", buf[i]);
			}
		}
#endif
		printf("\n");
	} 
}       
 
int
EPloop(unsigned char *colormap)
{    
int r = 1;

	r = libusb_init(NULL);
	if (r < 0) {
		fprintf(stderr, "failed to initialise libusb\n");
		return(1);
	}

	r = find_lvr_flirusb();
	if (r < 0) {
		fprintf(stderr, "Could not find/open device\n");
		goto out;
	}
	printf("Successfully found the Flir One G2 device\n");

	r = libusb_set_configuration(devh, 3);
	if (r < 0) {
		fprintf(stderr, "libusb_set_configuration error %d\n", r);
		goto out;
	}
	printf("Successfully set usb configuration 3\n");
	
 
	// Claiming of interfaces is a purely logical operation; 
	// it does not cause any requests to be sent over the bus. 
	r = libusb_claim_interface(devh, 0);
	if (r <0) {
		fprintf(stderr, "libusb_claim_interface 0 error %d\n", r);
		goto out;
	}	
	r = libusb_claim_interface(devh, 1);
	if (r < 0) {
		fprintf(stderr, "libusb_claim_interface 1 error %d\n", r);
		goto out;
	}
	r = libusb_claim_interface(devh, 2);
	if (r < 0) {
		fprintf(stderr, "libusb_claim_interface 2 error %d\n", r);
		goto out;
	}
	printf("Successfully claimed interface 0,1,2\n");


	unsigned char buf[1048576]; 
	int actual_length;

	time_t now;
	// save last error status to avoid clutter the log
	//char EP81_error[50]="", EP83_error[50]="",
	char EP85_error[50]=""; 
	unsigned char data[2]={0,0}; // only a bad dummy

	// don't forget: $ sudo modprobe v4l2loopback video_nr=0,1
	// startv4l2();

	int state = 1; 
	// int ct=0;

	while (tdata->flir_run) {
		switch(state) {
		         case 1:
				/* Flir config
				01 0b 01 00 01 00 00 00 c4 d5
				0 bmRequestType = 01
				1 bRequest = 0b
				2 wValue 0001 type (H) index (L)    stop=0/start=1 (Alternate Setting)
				4 wIndex 01                         interface 1/2
				5 wLength 00
				6 Data 00 00

				libusb_control_transfer (*dev_handle, bmRequestType, bRequest, wValue,  wIndex, *data, wLength, timeout)
				*/
				printf("stop interface 2 FRAME\n");
				r = libusb_control_transfer(devh,1,0x0b,0,2,data,0,100);
				if (r < 0) {
					fprintf(stderr, "Control Out error %d\n", r);
					return r;
				}

				printf("stop interface 1 FILEIO\n");
				r = libusb_control_transfer(devh,1,0x0b,0,1,data,0,100);
				if (r < 0) {
					fprintf(stderr, "Control Out error %d\n", r);
					return r;
				} 
             	
				printf("\nstart interface 1 FILEIO\n");
				r = libusb_control_transfer(devh,1,0x0b,1,1,data,0,100);
				if (r < 0) {
					fprintf(stderr, "Control Out error %d\n", r);
					return r;
				}
				now = time(0); // Get the system time
				printf("\n:xx %s",ctime(&now));
				state = 3;  // jump over wait stait 2. Not really using any data from CameraFiles.zip
				break;
		        case 2:
				printf("\nask for CameraFiles.zip on EP 0x83:\n");     
				now = time(0); // Get the system time
				printf("\n: %s",ctime(&now));

				int transferred = 0;
				char my_string[128];

				//--------- write string: {"type":"openFile","data":{"mode":"r","path":"CameraFiles.zip"}}
				int length = 16;
				unsigned char my_string2[16]={0xcc,0x01,0x00,0x00,0x01,0x00,0x00,0x00,0x41,0x00,0x00,0x00,0xF8,0xB3,0xF7,0x00};
				printf("\nEP 0x02 to be sent Hexcode: %i Bytes[",length);
				int i;
				for (i = 0; i < length; i++) {
					printf(" %02x", my_string2[i]);
				}
				printf(" ]\n");

				r = libusb_bulk_transfer(devh, 2, my_string2, length, &transferred, 0);
				if (r == 0 && transferred == length) {
					printf("\nWrite successful!");
				} else
					printf("\nError in write! res = %d and transferred = %d\n", r, transferred);

				strcpy(  my_string,"{\"type\":\"openFile\",\"data\":{\"mode\":\"r\",\"path\":\"CameraFiles.zip\"}}");

				length = strlen(my_string)+1;
				printf("\nEP 0x02 to be sent: %s", my_string);

				// avoid error: invalid conversion from ‘char*’ to ‘unsigned char*’ [-fpermissive]
				unsigned char *my_string1 = (unsigned char*)my_string;
				//my_string1 = (unsigned char*)my_string;

				r = libusb_bulk_transfer(devh, 2, my_string1, length, &transferred, 0);
				if (r == 0 && transferred == length) {
					printf("\nWrite successful!");
					printf("\nSent %d bytes with string: %s\n", transferred, my_string);
				} else
					printf("\nError in write! res = %d and transferred = %d\n", r, transferred);
 
				//--------- write string: {"type":"readFile","data":{"streamIdentifier":10}}
				length = 16;
				unsigned char my_string3[16]={0xcc,0x01,0x00,0x00,0x01,0x00,0x00,0x00,0x33,0x00,0x00,0x00,0xef,0xdb,0xc1,0xc1};
				printf("\nEP 0x02 to be sent Hexcode: %i Bytes[",length);
				for (i = 0; i < length; i++) {
					printf(" %02x", my_string3[i]);
				}
				printf(" ]\n");

				r = libusb_bulk_transfer(devh, 2, my_string3, length, &transferred, 0);
				if(r == 0 && transferred == length) {
					printf("\nWrite successful!");
				} else
					printf("\nError in write! res = %d and transferred = %d\n", r, transferred);

				//strcpy(  my_string, "{\"type\":\"setOption\",\"data\":{\"option\":\"autoFFC\",\"value\":true}}");
				strcpy(  my_string,"{\"type\":\"readFile\",\"data\":{\"streamIdentifier\":10}}");
				length = strlen(my_string)+1;
				printf("\nEP 0x02 to be sent %i Bytes: %s", length, my_string);

				// avoid error: invalid conversion from ‘char*’ to ‘unsigned char*’ [-fpermissive]
				my_string1 = (unsigned char*)my_string;
            
				r = libusb_bulk_transfer(devh, 2, my_string1, length, &transferred, 0);
				if (r == 0 && transferred == length) {
					printf("\nWrite successful!");
					printf("\nSent %d bytes with string: %s\n", transferred, my_string);
				} else
					printf("\nError in write! res = %d and transferred = %d\n", r, transferred);

				// go to next state
				now = time(0); // Get the system time
				printf("\n: %s",ctime(&now));
				//sleep(1);
				state = 3;           
				break;
			case 3:
				printf("\nAsk for video stream, start EP 0x85:\n");        

				r = libusb_control_transfer(devh,1,0x0b,1,2,data, 2,200);
				if (r < 0) {
					fprintf(stderr, "Control Out error %d\n", r);
					return r;
				};
				state = 4;
				break;
			case 4:
				// endless loop 
				// poll Frame Endpoints 0x85 
				// don't change timeout=100ms !!
				r = libusb_bulk_transfer(devh, 0x85, buf, sizeof(buf), &actual_length, 100); 
				if (actual_length > 0) {
					// print_bulk_result("0x85", "none", r, actual_length, buf);
					vframe("0x85",EP85_error, r, actual_length, buf, colormap);
				}
				break;      
		}

		// poll Endpoints 0x81, 0x83
		r = libusb_bulk_transfer(devh, 0x81, buf, sizeof(buf), &actual_length, 10);
		if (actual_length > 16) {
			unsigned int magic, second, len;

			magic=*(unsigned int *)&buf[0];
			second=*(unsigned int *)&buf[4];
			len=*(unsigned int *)&buf[8];

			fprintf(stderr, "EP81=%d in %d, magic=0x%08x sec=%08x len=%08x\n", r, actual_length,magic,second,len);
			if (magic == 0x000001cc) {
				parse_config_in(&buf[16]);
			}
#if 0
			int i;
			for (i=0; i<actual_length; i++) {
				if (buf[i] > 31 && buf[i]<128)
					fprintf(stderr, "%c", buf[i]);
				else
					//fprintf(stderr, ".");
					fprintf(stderr, "<%02x>", buf[i]);
			}
			fprintf(stderr, "\n");
#endif
		}
/*		
		if (actual_length > 0 && actual_length <= 101) {
			char k[5];
			int i;
			if (strncmp (&buf[32],"VoltageUpdate",13)==0) {
				printf("xx %d\n",actual_length);
				char *token, *string, *tofree, *string2;
				// char l;
				strcpy(string,buf);
				// string = buf;
				// assert(string != NULL);
				printf("yy\n");

				for (i = 32; i <  (((200)<(actual_length))?(200):(actual_length)); i++) {
					if (string[i]>31) {
						printf("%c", string[i]);
						// printf("%d ", i);
						// string2[i-32] = string[i];
					}
				}

				while ((token = strsep(&string, ":")) != NULL) {
					printf("zz\n");
					printf("%s\n", token);
				}
				// free(tofree);
				// for (i = 32; i <  (((200)<(actual_length))?(200):(actual_length)); i++) {
					// if(buf[i]>31) {printf("%c", buf[i]);}
				// }
			}
		}
*/

		r = libusb_bulk_transfer(devh, 0x83, buf, sizeof(buf), &actual_length, 10); 
		if (strcmp(libusb_error_name(r), "LIBUSB_ERROR_NO_DEVICE")==0) {
			fprintf(stderr, "EP 0x83 LIBUSB_ERROR_NO_DEVICE -> reset USB\n");
			goto out;
		}
		if (actual_length > 0) {
			//int i;
			fprintf(stderr, "EP83 in %d\n", actual_length);
		}
		// print_bulk_result("0x83",EP83_error, r, actual_length, buf); 
	}

	// never reached ;-)
	libusb_release_interface(devh, 0);

	out:
	//close the device
	if (devh != NULL) {
		libusb_reset_device(devh);
		libusb_close(devh);
		libusb_exit(NULL);
	}

	return r >= 0 ? r : -r;
}


// -----------------END-ORG-CODE--------------------------------------------


gpointer cam_thread_main(gpointer user_data)
{
	tdata=(struct t_data_t *)user_data;
	EPloop(NULL);

	return NULL;
}

