#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/input.h>

#define EVENT_DEVICE    "/dev/input/event1"
#define EVENT_TYPE      EV_ABS
#define EVENT_CODE_X    53
#define EVENT_CODE_Y    54
#define HALF_BUF        16
#define MAX_BUF         32

#define PREV_BTN        0
#define SELECT_BTN      1
#define NEXT_BTN        2
#define NEWFILE_BTN     3
#define LOOP_BTN        7
#define EXIT_BTN        8
#define BACK_BTN        9

#define Menu_Phase      1
#define Record_Phase    2
#define Record_Select   3

#define AUDIO_DIR "/root/Sound/Track/"
#define AUDIO_NAME "_Audio"
#define AUDIO_FORMAT ".wav"

#define PIANO_DIR "/root/Sound/Piano/"
#define DRUM_DIR "/root/Sound/Drum/"

#define LCD_WIDTH 1024
#define LCD_HEIGHT 600

int dotFd, clcdFd, keyFd, tsFd;

char audioPath[30][64];
char pianoPath[9][64];
char drumPath[4][64];
char tempFile[64];

int nowPhase = Menu_Phase;
int soundChoice;
char audioList[30][16];

void controlFile(int curMusic);

void deleteAudioFile(int idx);
void playMusic(int idx);
void mergeAudioFiles(int idx);
void makeAudioFile();
void getAudioResources();
void makeBlankFile(int num, int time);
int getAudioList();

void initLcdDisplay(unsigned short *fbdata);
void coloringLcd(unsigned short *fbdata, int x, int y);
unsigned short makePixel(unsigned short red, unsigned short green, unsigned short blue);
void *t_touchReader(void *args);

int main() {
    int end = 0;
    int curlist = 0;
    int listCount = 0;
    char clcd_top[17];
    char clcd_buf[33];
    char keyValue[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    char matrixBuf[10] = { 0x0e, 0x0a, 0x0a, 0x0a, 0x08, 0x08, 0x38, 0x38, 0x38, 0x38 };

    int thr_id, status;
    pthread_t p_thread;

    if ((clcdFd = open("/dev/fpga_text_lcd", O_WRONLY)) < 0) {
		printf("Device open error : /dev/fpga_text_lcd\n");
		exit(-1);
	}
    if ((dotFd = open("/dev/fpga_dot", O_WRONLY)) < 0) {
		printf("Device open error : /dev/fpga_dot\n");
		exit(-1);
	}
    if ((keyFd = open("/dev/fpga_push_switch", O_RDONLY)) < 0) {
		printf("Device open error : /dev/fpga_push_switch\n");
		exit(-1);
	}
    printf("Open device\n");

    write(dotFd, matrixBuf, 10);

    printf("Dot-Matrix\n");

    thr_id = pthread_create(&p_thread, NULL, t_touchReader, NULL);
    if(thr_id < 0) {
        perror("thread create error : ");
        exit(0);
    }
    printf("Thread-0 : TouchScreen\n");

    getAudioResources();
    listCount = getAudioList();
    printf("Make list %d\n", listCount);

    while(end != EXIT_BTN) {
        sprintf(clcd_top, "   [MAINMENU]   ");
        if(listCount > 0)
            sprintf(clcd_buf, "%s%s", clcd_top, audioList[curlist]);
        else
            sprintf(clcd_buf, "%s", clcd_top);
        memset(clcd_buf + strlen(clcd_buf), ' ', MAX_BUF - strlen(clcd_buf));
        write(clcdFd, clcd_buf, MAX_BUF);

        read(keyFd, &keyValue, sizeof(keyValue));
        if(keyValue[PREV_BTN] == 1) {
            if(listCount > 0)
                curlist = (curlist + listCount - 1) % listCount;
            printf("Prev\n");
			usleep(300000);
        } else if(keyValue[SELECT_BTN] == 1) {
            usleep(300000);
            if(listCount > 0)
                controlFile(curlist);
            listCount = getAudioList();
            curlist = 0;
        } else if(keyValue[NEXT_BTN] == 1) {
            if(listCount > 0)
                curlist = (curlist + 1) % listCount;
            printf("Next\n");
			usleep(300000);
        } else if(keyValue[NEWFILE_BTN] == 1) {
            makeBlankFile(listCount, 4);
            listCount = getAudioList();
            curlist = 0;
			usleep(300000);
        } else if(keyValue[EXIT_BTN] == 1) {
            end = EXIT_BTN;
            return 0;
        }
    }

    pthread_join(p_thread, (void**)&status);

    close(clcdFd);
    close(keyFd);
    close(dotFd);
    return 0;
}



void controlFile(int curMusic) {
    char controlMenu[4][16] = {"Play", "Record", "Remove", "Back"};
    int curlist = 0;
    int end = 0;
    int ledFd = 0;
    unsigned char ledOn = 0b11111111;
    unsigned char ledOff = 0b00000000;
    char keyValue[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    char clcd_top[17];
    char clcd_buf[33];

    if ( (ledFd = open("/dev/fpga_led", O_WRONLY)) < 0) {
        printf("Device Open Error : /dev/fpga_led\n");
        exit(0);
    }

    while(end != BACK_BTN) {
        sprintf(clcd_top, "%s", audioList[curMusic]);
        memset(clcd_top + strlen(clcd_top), ' ', HALF_BUF - strlen(clcd_top));
        sprintf(clcd_buf, "%s%s", clcd_top, controlMenu[curlist]);
        memset(clcd_buf + strlen(clcd_buf), ' ', MAX_BUF - strlen(clcd_buf));
        write(clcdFd, clcd_buf, MAX_BUF);

        read(keyFd, &keyValue, sizeof(keyValue));
        if(keyValue[PREV_BTN] == 1) {
            curlist = (curlist + 4 - 1) % 4;
            printf("Prev\n");
			usleep(300000);
        } else if(keyValue[SELECT_BTN] == 1) {
            if(curlist == 0)
                playMusic(curMusic);
            else if(curlist == 1) {
                write(ledFd, &ledOn, sizeof(ledOn));
                makeAudioFile();
                write(ledFd, &ledOff, sizeof(ledOff));
                mergeAudioFiles(curMusic);
            } else if(curlist == 2) {
                deleteAudioFile(curMusic);
                end = BACK_BTN;
            } else if(curlist == 3)
                end = BACK_BTN;
			usleep(300000);
        } else if(keyValue[NEXT_BTN] == 1) {
            curlist = (curlist + 1) % 4;
            printf("Next\n");
			usleep(300000);
        }
    }
    close(ledFd);
    printf("Back\n");
}



void deleteAudioFile(int idx) {

	char* argv[] = { "rm", audioPath[idx], (char *)0 };

	if(fork() == 0) {
		if(execv("/bin/rm", argv) < 0) {
			perror("exevc");
			exit(1);
		}
	}
	wait(0);
}

void playMusic(int idx) {

	char* argv[] = { "mplayer", audioPath[idx], "-ao", "alsa:device=hw=1.0", "-volume", "100", (char *)0 };

	if(fork() == 0) {
		if(execv("/usr/bin/mplayer", argv) < 0) {
			perror("exevc");
			exit(1);
		}
	}
	wait(0);
}

void makeAudioFile() {

	int i;
	char subAudio[8][64];

	printf("Make audio file for mix\n");
	for(i=0; i<8; i++) {
		printf("0~7 piano, 8.rest, 9-12 drum");
		printf("%d-th choose : ", i+1);

        nowPhase = Record_Phase;
        while(nowPhase == Record_Phase) {
            usleep(10000);
        }

		switch(soundChoice) {
			case 1: case 2: case 3: case 4: case 5:
			case 6: case 7: case 8: case 0:
				strcpy(subAudio[i], pianoPath[soundChoice]);
				break;
			case 9: case 10: case 11: case 12:
				strcpy(subAudio[i], drumPath[soundChoice-9]);
				break;
		}
	}
    nowPhase = Menu_Phase;

	printf("--Selected audio source \n");
	for(i=0; i<8; i++) {
		printf("%s\n", subAudio[i]);
	}

	strcpy(tempFile, AUDIO_DIR);
	strcat(tempFile, "temp.wav");

	char* argv[] = { "ffmpeg", "-i", subAudio[0], "-i", subAudio[1],
							   "-i", subAudio[2], "-i", subAudio[3],
							   "-i", subAudio[4], "-i", subAudio[5],
							   "-i", subAudio[6], "-i", subAudio[7],
							   "-filter_complex",
							   "[0:0][1:0][2:0][3:0]concat=n=8:v=0:a=1[out]",
							   "-map", "[out]", tempFile, (char *)0 };

	if(fork() == 0) {
		if(execv("/usr/bin/ffmpeg", argv) < 0) {
			perror("exevc");
			exit(1);
		}
	}
	wait(0);
}

void mergeAudioFiles(int idx) {

	char tempFile2[64];
	strcpy(tempFile2, AUDIO_DIR);
	strcat(tempFile2, "temp2.wav");

	char* argv1[] = { "ffmpeg", "-i", audioPath[idx], "-i", tempFile,
				   	  "-filter_complex", "amerge", tempFile2, (char *)0 };
	if(fork() == 0) {
		if(execv("/usr/bin/ffmpeg", argv1) < 0) {
			perror("exevc");
			exit(1);
		}
	}
	wait(0);

	char* argv2[] = { "rm", tempFile, audioPath[idx], (char *)0 };
	if(fork() == 0) {
		if(execv("/bin/rm", argv2) < 0) {
			perror("exevc");
			exit(1);
		}
	}
	wait(0);

	char* argv3[] = { "mv", tempFile2, audioPath[idx], (char *)0 };
	if(fork() == 0) {
		if(execv("/bin/mv", argv3) < 0) {
			perror("exevc");
			exit(1);
		}
	}
	wait(0);
	memset(tempFile, '\0', sizeof(tempFile));
}

void getAudioResources() {

	DIR *piano_dp, *drum_dp;
	struct dirent *piano_dep, *drum_dep;
	int i;

	char pianoList[9][16];
	char drumList[4][16];

	if((piano_dp = opendir(PIANO_DIR)) == NULL) {
		perror("PIANO_DIR_OPEN_ERR");
		exit(1);
	}
	if((drum_dp = opendir(DRUM_DIR)) == NULL) {
		perror("DRUM_DIR_OPEN_ERR");
		exit(1);
	}

	while(piano_dep = readdir(piano_dp)) {
		if (strcmp(piano_dep->d_name, ".") == 0) {}
		else if (strcmp(piano_dep->d_name, "..") == 0) {}
		else {
			i = piano_dep->d_name[0] - 49;
			strcpy(pianoList[i], piano_dep->d_name);
		}
	}

	while(drum_dep = readdir(drum_dp)) {
		if (strcmp(drum_dep->d_name, ".") == 0) {}
		else if (strcmp(drum_dep->d_name, "..") == 0) {}
		else {
			i = drum_dep->d_name[0] - 49;
			strcpy(drumList[i], drum_dep->d_name);
		}
	}

	for(i=0; i<9; i++) {
		strcpy(pianoPath[i], PIANO_DIR);
		strcat(pianoPath[i], pianoList[i]);
		printf("Piano[%d] = %s\n", i, pianoPath[i]);
	}
	for(i=0; i<4; i++) {
		strcpy(drumPath[i], DRUM_DIR);
		strcat(drumPath[i], drumList[i]);
		printf("Drum[%d] = %s\n", i, drumPath[i]);
	}


	closedir(piano_dp);
	closedir(drum_dp);
}

void makeBlankFile(int num, int time) {

	char timeBuffer[4];
	char filePath[64] = AUDIO_DIR;
	char fileName[32] = AUDIO_NAME;
	char fileNum[4];
	char fileFormat[5] = AUDIO_FORMAT;

	sprintf(timeBuffer, "%d", time);
	sprintf(fileNum, "%d", num);
	strcat(filePath, fileNum);
	strcat(filePath, fileName);
	strcat(filePath, fileFormat);

	printf("\'%s\' is created\n", filePath);

	char* argv[] = { "ffmpeg", "-ar", "44100", "-t", timeBuffer, "-f", "s16le", "-ac", "2", "-i"
					, "/dev/zero", "-aq", "4", filePath, (char *)0 };

	if(fork() == 0) {
		if(execv("/usr/bin/ffmpeg", argv) < 0) {
			perror("exevc");
			exit(1);
		}
	}
	wait(0);
}

int getAudioList() { // 영상 리스트를 가져오는 함수

	DIR *dp;
	struct dirent *dep;
	int i, count;

	if((dp = opendir(AUDIO_DIR)) == NULL) {
		perror("Directory Open Fail.");
		exit(1);
	}

	count = 0;
	while(dep = readdir(dp)) {
		if (strcmp(dep->d_name, ".") == 0) {}
		else if (strcmp(dep->d_name, "..") == 0) {}
		else {
			i = dep->d_name[0] - 48;
			strcpy(audioList[i], dep->d_name);
			strcpy(audioPath[i], AUDIO_DIR);
			strcat(audioPath[i], audioList[i]);
			count++;
		}
	}

	for(i=0; i<count; i++) {
		printf("%s\n", audioPath[i]);
	}

	closedir(dp);
	return count;
}




unsigned short makePixel(unsigned short red, unsigned short green, unsigned short blue) {
    return ( (red << 11) | (green << 5) | (blue) );
}

void coloringLcd(unsigned short *fbdata, int x, int y) {
    int i, j, offset;
    unsigned short pixel = makePixel(30, 60, 30);

    for (i = (y - 200); i < y; i++) {
        offset = LCD_WIDTH * i + (x - 200);
        for (j = 0; j < 200; j++) {
            *(fbdata + (offset++)) = pixel;
        }
    }
}

void initLcdDisplay(unsigned short *fbdata) {
    unsigned short pixel;
    int offset, i, j;
    int flag = 0;

    pixel = makePixel(5, 10, 5);
    for (i = 0; i < 200; i++) {
        offset = LCD_WIDTH * i + 0;
        for (j = 0; j < 800; j++) {

            if ((j % 200) == 0) {
                if (flag == 0) {
                    pixel = makePixel(10, 15, 10);
                    flag = 1;
                }
                else {
                    pixel = makePixel(5, 10, 5);
                    flag = 0;
                }
            }

            *(fbdata + (offset++)) = pixel;
        }
    }

	flag = 0;

    pixel = makePixel(10, 15, 10);
    for (i = 200; i < 600; i++) {
        offset = LCD_WIDTH * i + 0;
		if (i == 400) { flag = 1; }
        for (j = 0; j < 800; j++) {

            if ((j % 200) == 0) {
                if (flag == 0) {
                    pixel = makePixel(5, 10, 5);
                    flag = 1;
                }
                else {
                    pixel = makePixel(10, 15, 10);
                    flag = 0;
                }
            }
            *(fbdata + (offset++)) = pixel;
        }
    }

    pixel = makePixel(0, 0, 0);
    for (i = 0; i < 600; i++) {
        offset = LCD_WIDTH * i + 800;
        for (j = 0; j < 224; j++) {
            *(fbdata + (offset++)) = pixel;
        }
    }
}

void closeDevice(void *fd) {
    close(*(int *)fd);
}

void *t_touchReader(void *args) {
    int touchFd, fbFd;
    int xPos, yPos, i, j, offset, pixel;
    int flag = 0;
    int isPressed = 0;
    char* argv[] = { "mplayer", "", "-ao", "alsa:device=hw=1.0", "-volume", "40",(char *)0 };

    struct input_event ev;

    if ( (fbFd = open("/dev/fb0", O_RDWR)) < 0) {
        perror("FRAME BUFFER\n");
        exit(1);
    }
    if ( (touchFd = open("/dev/input/event1", O_RDONLY)) < 0) {
        perror("TOUCH DEVICE\n");
        exit(1);
    }

    unsigned short *fbdata = (unsigned short *)mmap(0, LCD_WIDTH * LCD_HEIGHT * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd, 0);

    initLcdDisplay(fbdata);

    pthread_cleanup_push(closeDevice, (void *)&touchFd);
    pthread_cleanup_push(closeDevice, (void *)&fbFd);

    while (1) {
        read(touchFd, &ev, sizeof(ev));

        if (ev.type == 1 && isPressed == 1) {
            // 뗄 때
            isPressed = 0;
            if(nowPhase == Record_Phase) {
            	if(yPos >= 0 && yPos < 200) { // Drum sound
    				if(xPos >= 0 && xPos < 200) {
    					printf("Drum1 \n");
                        argv[1] = "/root/Sound/Drum/1_bass.wav";
                        soundChoice = 9;
    				} else if(xPos >= 200 && xPos < 400) {
    					printf("Drum2 \n");
                        argv[1] = "/root/Sound/Drum/2_cymbal.wav";
                        soundChoice = 10;
    				} else if(xPos >= 400 && xPos < 600) {
    					printf("Drum3 \n");
                        argv[1] = "/root/Sound/Drum/3_hihat.wav";
                        soundChoice = 11;
    				} else if(xPos >= 600 && xPos < 800) {
    					printf("Drum4 \n");
                        printf("Rest \n");
                        argv[1] = "/root/Sound/Drum/4_snare.wav";
                        soundChoice = 12;
    				} else {
                        argv[1] = "/root/Sound/Piano/9_rest.wav";
                        soundChoice = 8;
    				}
    			} else if(yPos >= 200 && yPos < 400) { // Piano sound 1
    				if(xPos >= 0 && xPos < 200) {
    					printf("Piano1 \n");
                        argv[1] = "/root/Sound/Piano/1_c1.wav";
                        soundChoice = 0;
    				} else if(xPos >= 200 && xPos < 400) {
    					printf("Piano2 \n");
                        argv[1] = "/root/Sound/Piano/2_d1.wav";
                        soundChoice = 1;
    				} else if(xPos >= 400 && xPos < 600) {
    					printf("Piano3 \n");
                        argv[1] = "/root/Sound/Piano/3_e1.wav";
                        soundChoice = 2;
    				} else if(xPos >= 600 && xPos < 800) {
    					printf("Piano4 \n");
                        argv[1] = "/root/Sound/Piano/4_f1.wav";
                        soundChoice = 3;
    				} else {
    					printf("Rest \n");
                        argv[1] = "/root/Sound/Piano/9_rest.wav";
                        soundChoice = 8;
    				}
    			} else { // Piano sound 2
    				if(xPos >= 0 && xPos < 200) {
    					printf("Piano5 \n");
                        argv[1] = "/root/Sound/Piano/5_g1.wav";
                        soundChoice = 4;
    				} else if(xPos >= 200 && xPos < 400) {
    					printf("Piano6 \n");
                        argv[1] = "/root/Sound/Piano/6_a1.wav";
                        soundChoice = 5;
    				} else if(xPos >= 400 && xPos < 600) {
    					printf("Piano7 \n");
                        argv[1] = "/root/Sound/Piano/7_b1.wav";
                        soundChoice = 6;
    				} else if(xPos >= 600 && xPos < 800) {
    					printf("Piano8 \n");
                        argv[1] = "/root/Sound/Piano/8_c2.wav";
                        soundChoice = 7;
    				} else {
    					printf("Rest \n");
                        argv[1] = "/root/Sound/Piano/9_rest.wav";
                        soundChoice = 8;
    				}
    			}
            	if(fork() == 0) {
            		if(execv("/usr/bin/mplayer", argv) < 0) {
            			perror("exevc");
            			exit(1);
            		}
            	}
            	wait(0);
                nowPhase = Record_Select;
            }
            initLcdDisplay(fbdata);
		} else if (ev.type == 1 && isPressed == 0) {
            // 누를 때
            isPressed = 1;

        } else if (ev.type == 3) {
            if (ev.code == 53) { xPos = ev.value; }
            if (ev.code == 54) { yPos = ev.value; flag = 1; }

            if (flag == 1) { // y좌표까지 갱신한 뒤에 작동
                pixel = makePixel(30, 60, 30);

                if(yPos >= 0 && yPos < 200) { // Drum sound
                    if(xPos >= 0 && xPos < 200) {
                            coloringLcd(fbdata, 200, 200);
                    }
                    else if(xPos >= 200 && xPos < 400) {
                            coloringLcd(fbdata, 400, 200);
                    }
                    else if(xPos >= 400 && xPos < 600) {
                            coloringLcd(fbdata, 600, 200);
                    }
                    else if(xPos >= 600 && xPos < 800) {
                            coloringLcd(fbdata, 800, 200);
                    }
                    else { // 800 ~ 1024
                        for (i = 0; i < 600; i++) {
                            offset = LCD_WIDTH * i + 800;
                            for (j = 0; j < 224; j++) {
                                *(fbdata + (offset++)) = pixel;
                            }
                        }
						// 세로 길게 칠하기,  쉼표에 해당됨
                    }
                }
                else if(yPos >= 200 && yPos < 400) { // Piano sound 1
                    if(xPos >= 0 && xPos < 200) {
                        coloringLcd(fbdata, 200, 400);
                    }
                    else if(xPos >= 20 && xPos < 400) {
                        coloringLcd(fbdata, 400, 400);
                    }
                    else if(xPos >= 400 && xPos < 600) {
                        coloringLcd(fbdata, 600, 400);
                    }
                    else if(xPos >= 600 && xPos < 800) {
                        coloringLcd(fbdata, 800, 400);
                    }
					else {
						for (i = 0; i < 600; i++) {
                            offset = LCD_WIDTH * i + 800;
                            for (j = 0; j < 224; j++) {
                                *(fbdata + (offset++)) = pixel;
                            }
                        }
					}
                }
                else { // Piano sound 2
                    if(xPos >= 0 && xPos < 200) {
                        coloringLcd(fbdata, 200, 600);
                    }
                    else if(xPos >= 200 && xPos < 400) {
                        coloringLcd(fbdata, 400, 600);
                    }
                    else if(xPos >= 400 && xPos < 600) {
                        coloringLcd(fbdata, 600, 600);
                    }
					else if(xPos >= 600 && xPos < 800) {
						coloringLcd(fbdata, 800, 600);
					}
                    else {
                        for (i = 0; i < 600; i++) {
                            offset = LCD_WIDTH * i + 800;
                            for (j = 0; j < 224; j++) {
                                *(fbdata + (offset++)) = pixel;
                            }
                        }
                    }
                }
                flag = 0;
            }
        }
    }
    pthread_cleanup_pop(0);
    pthread_cleanup_pop(0);
}
