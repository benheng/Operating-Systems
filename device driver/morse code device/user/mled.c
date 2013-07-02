/*
**  mled.c: User program used to communicate with device file /dev/morsedev.
**
**	::NOTE::
**		Only numbers, letters, and spaces will be properly encoded. Other characters will
**		automatically be set as spaces.
**
**	::USES::
**	  -g		Gets the string stored within the device file and flashes
**				an LED connected to a specified GPIO pin, if possible.
**	  -s [msg]	Sets the string stored within the device file and prepares
**				the pulse buffer that controls the flashing LED.
*/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#define CAP1X 32
#define DEVICE "/dev/morsedev"

char buff[CAP1X];

int main(int argc, char* argv[])
{
	int fd = -1;

	// check for proper use of program
	if (argc < 2 || argc > 3) { goto fail; }
	fd = open(DEVICE, O_RDWR | O_APPEND);
	if (fd < 0) { printf("Open %s failed!\n\n", DEVICE); exit(1); }
	else { printf("Opened %s\n", DEVICE); }

	// get message from device file buffer
	if (argc == 2 && strcmp(argv[1], "-g") == 0) {
		printf("flashing... \n");
		read(fd, buff, strlen(buff));
		if (buff[0] != '\0') { printf("\"%s\" flashed\n", buff); }
		else { printf("buffer empty - nothing flashed\n"); }
		goto exit;
	}
	// set message to device file buffer
	else if (argc == 3 && strcmp(argv[1], "-s") == 0) {
		write(fd, argv[2], strlen(argv[2]));
		goto exit;
	}
	else { goto fail; }

fail:
	printf("\nFunction uses: only numbers, letters, and spaces will be properly encoded.\n"
		"\t./mled -g       : Get the current message in the device file buffer and pulse\n"
		"\t                  morse code equivalent through an LED connected to the GPIO.\n"
		"\t./mled -s [msg] : Set the message in the device file and blink the morse code\n"
		"\t                  equivalent to an LED via a GPIO. (Max %d char including\n"
		"\t                  string terminator.\n\n", CAP1X);
exit:
	if (fd > 0) { close(fd); }
	return 0;
}