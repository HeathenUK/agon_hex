#include <agon/vdp_vdu.h>
#include <agon/vdp_key.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <mos_api.h>

// void write16bit(uint16_t w)
// {
// 	putch(w & 0xFF); // write LSB
// 	putch(w >> 8);	 // write MSB	
// }

// void write24bit(uint24_t w)
// {
// 	putch(w & 0xFF); // write LSB
// 	putch(w >> 8);	 // write middle	
//     putch(w >> 16);	 // write MSB	
// }

static FILE *hex_file;
unsigned long hex_file_size = 0;
unsigned long hex_file_bottom = 0;
uint8_t hex_file_rump = 0;
unsigned long current_offset = 0;
unsigned long top_offset = 0;
unsigned long bottom_offset = 0;

static FILE *hex_file_b;
unsigned long hex_file_size_b = -1;
unsigned long hex_file_bottom_b = -1;
uint8_t hex_file_rump_b = 0;
unsigned long current_offset_b = 0;
unsigned long top_offset_b = 0;
unsigned long bottom_offset_b = 0;

uint8_t lines_per_window = 59;

bool diff = false;

uint8_t selected_x = 0;
uint8_t selected_y = 0;

char* itoa(int value, char* result, int base) {
	// check that the base if valid
	if (base < 2 || base > 36) { *result = '\0'; return result; }

	char* ptr = result, *ptr1 = result, tmp_char;
	int tmp_value;

	do {
		tmp_value = value;
		value /= base;
		*ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
	} while ( value );

	// Apply negative sign
	if (tmp_value < 0) *ptr++ = '-';
	*ptr-- = '\0';
	while(ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr--= *ptr1;
		*ptr1++ = tmp_char;
	}
	return result;
}

char* to_bin(int number) {

    char* binaryString = (char*)malloc(9 * sizeof(char));
    
    binaryString[8] = '\0';

    for (int i = 7; i >= 0; i--) {
        binaryString[i] = (number & 1) + '0';
        number >>= 1;
    }

    return binaryString;
}

int min(int a, int b) {
    if (a < b) {
        return a;
    } else {
        return b;
    }
}

int max(int a, int b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

void cursor_tab(uint8_t x, uint8_t y) {

	putch(0x1F);
	putch(x);
	putch(y);

}

void cursor_set(bool state) {

	//VDU 23, 1, n

	putch(23);
	putch(1);
	putch(state);

}

void set_text_window(uint8_t left, uint8_t bottom, uint8_t right, uint8_t top) {

	putch(0x1C);

	putch(left);
	putch(bottom);
	putch(right);
	putch(top);

}

void print_to_debug(const char* format, ...) {

	va_list args;
	putch(2);
	putch(21);
	
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

	putch(6);
	putch(3);

}

bool is_ag_print(unsigned char test) {

	if (test >= 32 && test < 127) return true;
	else return false;

}

static volatile SYSVAR *sv;

void set_text_fg(uint8_t colour) {

	putch(17);
	putch(colour);

}

void set_text_bg(uint8_t colour) {

	putch(17);
	putch(colour + 128);

}

void scroll_text_up(uint8_t scroll_y) {

	//VDU 23, 7
	putch(23);
	putch(7);
	putch(0); //Scroll text viewport
	putch(3); //Scroll up
	putch(scroll_y * 8); //Scroll up by scroll_y

}

void scroll_text_down(uint8_t scroll_y) {

	//VDU 23, 7
	putch(23);
	putch(7);
	putch(0); //Scroll text viewport
	putch(2); //Scroll down
	putch(scroll_y * 8); //Scroll down by scroll_y

}

void fill_line(bool newline) {
    
	unsigned char x16[16];

	fseek(hex_file, current_offset, SEEK_SET);

    size_t bytes_read = fread(x16, sizeof(uint8_t), 16, hex_file);

    if (newline) printf("\r\n");

	printf("%06X ", (unsigned int)ftell(hex_file) - bytes_read);
    for (size_t i = 0; i < bytes_read; ++i) {
        printf("%02X ", x16[i]);
    }
    if (bytes_read < 16) {
        printf("%*s", (int)(16 - bytes_read) * 3, ""); // Pad
    }
    for (size_t i = 0; i < bytes_read; ++i) {
        printf("%c", is_ag_print(x16[i]) ? x16[i] : '.');
    }

	current_offset += bytes_read;

}

void fill_line_bottom(bool newline) {
    
	unsigned char x16_b[16];

	fseek(hex_file_b, current_offset_b, SEEK_SET);

    size_t bytes_read = fread(x16_b, sizeof(uint8_t), 16, hex_file_b);

    if (newline) printf("\r\n");

	printf("%06X ", (unsigned int)ftell(hex_file_b) - bytes_read);
    for (size_t i = 0; i < bytes_read; ++i) {
        printf("%02X ", x16_b[i]);
    }
    if (bytes_read < 16) {
        printf("%*s", (int)(16 - bytes_read) * 3, ""); // Pad
    }
    for (size_t i = 0; i < bytes_read; ++i) {
        printf("%c", is_ag_print(x16_b[i]) ? x16_b[i] : '.');
    }

	current_offset_b += bytes_read;

}

void header_bar() {

	set_text_bg(3);
	set_text_fg(0);
	printf("0x     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F                          ");
	set_text_bg(0);
	set_text_fg(3);

}

void redraw_current_line() {
    
	unsigned char x16[16];

	fseek(hex_file, top_offset + (16 * selected_y), SEEK_SET);

    size_t bytes_read = fread(x16, sizeof(uint8_t), 16, hex_file);

	cursor_tab(0, selected_y);

    printf("\r");

	printf("%06X ", (unsigned int)ftell(hex_file) - bytes_read);
    for (size_t i = 0; i < bytes_read; ++i) {
        printf("%02X ", x16[i]);
    }
    if (bytes_read < 16) {
        printf("%*s", (int)(16 - bytes_read) * 3, ""); // Pad
    }
    for (size_t i = 0; i < bytes_read; ++i) {
        printf("%c", is_ag_print(x16[i]) ? x16[i] : '.');
    }

}

void fill_screen(uint16_t start_offset) {

    unsigned char x16[16];
	unsigned char x16_b[16];
	
	if (start_offset > hex_file_size || start_offset > hex_file_size_b) return;
	
	top_offset = ((start_offset / 16) * 16);
	if (diff) top_offset_b = ((start_offset / 16) * 16);

	fseek(hex_file, top_offset, SEEK_SET); // Start from top_offset
	if (diff) fseek(hex_file_b, top_offset_b, SEEK_SET); // Start from top_offset_b
    
	size_t bytes_read;
    uint8_t line_count = 0;

	set_text_window(0,60,80,1);

    current_offset = top_offset; // Ensure current_offset starts at top_offset

    while ((line_count < lines_per_window) && (current_offset < hex_file_size)) {
        bytes_read = fread(x16, 1, 16, hex_file);
        printf("%06lX ", current_offset);
        for (size_t i = 0; i < bytes_read; ++i) {
            printf("%02X ", x16[i]);
        }
        if (bytes_read < 16) {
            printf("%*s", (int)(16 - bytes_read) * 3, "");
        }
        for (size_t i = 0; i < bytes_read; ++i) {
			printf("%c", is_ag_print(x16[i]) ? x16[i] : '.');
        }
        current_offset += bytes_read;
        line_count++;
        if (bytes_read < 16) break;
		if (line_count != lines_per_window) printf("\r\n");
    }
    bottom_offset = current_offset - bytes_read; // Update bottom_offset after filling the screen

	if (diff) {

		bytes_read = 0;
		line_count = 0;

		cursor_tab(0,29);
		header_bar();

		current_offset_b = top_offset_b; // Ensure current_offset starts at top_offset

		while ((line_count < lines_per_window) && (current_offset_b < hex_file_size_b)) {
			bytes_read = fread(x16_b, 1, 16, hex_file_b);
			printf("%06lX ", current_offset_b);
			for (size_t i = 0; i < bytes_read; ++i) {
				printf("%02X ", x16_b[i]);
			}
			if (bytes_read < 16) {
				printf("%*s", (int)(16 - bytes_read) * 3, "");
			}
			for (size_t i = 0; i < bytes_read; ++i) {
				printf("%c", is_ag_print(x16_b[i]) ? x16_b[i] : '.');
			}
			current_offset_b += bytes_read;
			line_count++;
			if (bytes_read < 16) break;
			if (line_count != lines_per_window) printf("\r\n");
		}
		bottom_offset_b = current_offset_b - bytes_read; // Update bottom_offset after filling the screen	

		set_text_window(0,lines_per_window,80,1);

	}

}

void fill_screen_diff(uint16_t start_offset) {

	unsigned char buffer_a[464], buffer_b[464];
	
    if (start_offset > hex_file_size || start_offset > hex_file_size_b) return;

    top_offset = (start_offset / 464) * 464;
    fseek(hex_file, top_offset, SEEK_SET);
    fseek(hex_file_b, top_offset, SEEK_SET);

    size_t bytes_read_a = fread(buffer_a, 1, 464, hex_file);
	current_offset += top_offset + bytes_read_a;
	current_offset = (current_offset / 16) * 16;
   
    size_t bytes_read_b = fread(buffer_b, 1, 464, hex_file_b);
	current_offset_b += top_offset + bytes_read_b;
	current_offset_b = (current_offset_b / 16) * 16;

	bottom_offset = ((bytes_read_a / 16) * 16) - 16;
	bottom_offset_b = ((bytes_read_b / 16) * 16) - 16;

    int row_a = 0, row_b = 30;

	set_text_window(0,60,80,1);
	cursor_tab(0,29);
	header_bar();

    for (size_t line = 0; line < 29; line++) {

        cursor_tab(0, row_a + line);
        printf("%06lX", top_offset + line * 16);

        cursor_tab(0, row_b + line);
        printf("%06lX", top_offset + line * 16);

        for (size_t i = 0; i < 16; i++) {
            size_t idx = line * 16 + i;
            int column = (i % 16) * 3 + 6;

			bool delta = false;
			if (buffer_a[idx] != buffer_b[idx]) {
				delta = true;
				if (delta) set_text_fg(2);
			}

            if (idx < bytes_read_a) {
                
				cursor_tab(1 + column, row_a + line);
                printf("%02X", buffer_a[idx]);
				cursor_tab(55 + i, row_a + line);
				printf("%c", is_ag_print(buffer_a[idx]) ? buffer_a[idx] : '.');
		
            }

            if (idx < bytes_read_b) {
                cursor_tab(1 + column, row_b + line);
                printf("%02X", buffer_b[idx]);
				cursor_tab(55 + i, row_b + line);
				printf("%c", is_ag_print(buffer_b[idx]) ? buffer_b[idx] : '.');				
            }
			
			if (delta) set_text_fg(3);
        }
    }

	set_text_window(0,29,80,1);

}

void unselect_hex() {

	cursor_tab(7 + (selected_x * 3), selected_y);

	current_offset = top_offset + (16 * selected_y) + selected_x;
	fseek(hex_file, current_offset, SEEK_SET);
	unsigned char byte;
	fread(&byte, 1, 1, hex_file);
	set_text_bg(0);
	set_text_fg(3);
	printf("%02X", byte);
	cursor_tab(55 + selected_x, selected_y);
	printf("%c", is_ag_print(byte) ? byte : '.');	

}

void unselect_bottom_hex() {

	set_text_window(0,60,80,31);

	cursor_tab(7 + (selected_x * 3), selected_y);
	current_offset_b = top_offset_b + (16 * selected_y) + selected_x;
	fseek(hex_file_b, current_offset_b, SEEK_SET);
	unsigned char byte;
	fread(&byte, 1, 1, hex_file_b);
	set_text_bg(0);
	set_text_fg(3);
	printf("%02X", byte);
	cursor_tab(55 + selected_x, selected_y);
	printf("%c", is_ag_print(byte) ? byte : '.');	

}

void clear_hex(uint8_t colour) {

	cursor_tab(7 + (selected_x * 3), selected_y);
	set_text_bg(colour);
	set_text_fg(3);
	printf("  \b\b");
	set_text_bg(0);
	set_text_fg(3);

}

void select_hex(int8_t delta_x, int8_t delta_y) {

	//left, bottom, right, top
	if (diff) set_text_window(0,30,80,1);
	else set_text_window(0,60,80,1);

	cursor_tab(7 + (selected_x * 3), selected_y);
	//print_to_debug("Tabbing to %u x %u", selected_x, selected_y);
	current_offset = top_offset + (16 * selected_y) + selected_x;
	fseek(hex_file, current_offset, SEEK_SET);
	
	unsigned char byte[4];
	unsigned char byte_b[4];
	
	fread(byte, 1, 1, hex_file);
	set_text_bg(0);
	set_text_fg(3);
	printf("%02X", byte[0]);
	cursor_tab(55 + selected_x, selected_y);
	printf("%c", is_ag_print(byte[0]) ? byte[0] : '.');	

	if (diff) {
	
		//left, bottom, right, top
		set_text_window(0,60,80,31);
		
		cursor_tab(7 + (selected_x * 3), selected_y);
		current_offset_b = top_offset_b + (16 * selected_y) + selected_x;
		fseek(hex_file_b, current_offset_b, SEEK_SET);
		
		fread(byte_b, 1, 1, hex_file_b);

		set_text_bg(0);
		set_text_fg(3);
		printf("%02X", byte_b[0]);
		cursor_tab(55 + selected_x, selected_y);
		printf("%c", is_ag_print(byte_b[0]) ? byte_b[0] : '.');	

		//left, bottom, right, top
		set_text_window(0,30,80,1);
	
	}

	selected_x += delta_x;
	selected_y += delta_y;

	cursor_tab(7 + (selected_x * 3), selected_y);
	current_offset = top_offset + (16 * selected_y) + selected_x;
	fseek(hex_file, current_offset, SEEK_SET);

	fread(byte, 1, 4, hex_file);
	
	set_text_bg(1);
	set_text_fg(3);
	
	printf("%02X", byte[0]);

	cursor_tab(55 + selected_x, selected_y);
	printf("%c", is_ag_print(byte[0]) ? byte[0] : '.');

	set_text_bg(0);
	set_text_fg(3);

	current_offset = top_offset + (16 * selected_y);

	uint8_t old_cursor_x = sv->cursorX;
	uint8_t old_cursor_y = sv->cursorY;

	//left, bottom, right, top
	set_text_window(71,lines_per_window,80,0);
	set_text_bg(3);
	set_text_fg(0);
	putch(12);

	set_text_bg(0);
	set_text_fg(3);
	printf(" 0x%06X\r\n", (unsigned int)ftell(hex_file) - 4);
	set_text_bg(3);
	set_text_fg(0);

	printf("%s",to_bin(byte[0]));
	printf("\r\n\nu8:\r\n%u", byte[0]);	
	printf("\r\n\ns8:\r\n%d", (char)byte[0]);
	printf("\r\n\nu16:\r\n%u", (uint16_t)byte[0] | ((uint16_t)byte[1] << 8));
	printf("\r\n\ns16:\r\n%d", (int16_t)byte[0] | ((int16_t)byte[1] << 8));
	printf("\r\n\nu24:\r\n%lu", ((uint32_t)byte[2] << 16) | ((uint32_t)byte[1] << 8) | (uint32_t)byte[0]);	
	int32_t int24 = ((int32_t)byte[2] << 16) | ((uint32_t)byte[1] << 8) | (uint32_t)byte[0];
	if(byte[2] & 0x80) {
    	int24 |= 0xFF000000;
	}
	printf("\r\n\ns24:\r\n%ld", int24);
	printf("\r\n\nu32:\r\n%lu", ((uint32_t)byte[3] << 24) | ((uint32_t)byte[2] << 16) | ((uint32_t)byte[1] << 8) | (uint32_t)byte[0]);
	printf("\r\n\ns32:\r\n%ld", ((int32_t)byte[3] << 24) | ((int32_t)byte[2] << 16) | ((int32_t)byte[1] << 8) | (int32_t)byte[0]);

	if (diff) {
		
		//left, bottom, right, top
		set_text_window(0,60,80,31);

		cursor_tab(7 + (selected_x * 3), selected_y);
		current_offset_b = top_offset_b + (16 * selected_y) + selected_x;
		fseek(hex_file_b, current_offset_b, SEEK_SET);

		fread(byte_b, 1, 4, hex_file_b);
		
		set_text_bg(1);
		set_text_fg(3);
		
		printf("%02X", byte_b[0]);

		cursor_tab(55 + selected_x, selected_y);
		printf("%c", is_ag_print(byte_b[0]) ? byte_b[0] : '.');

		set_text_bg(0);
		set_text_fg(3);

		current_offset_b = top_offset_b + (16 * selected_y);

		//left, bottom, right, top
		set_text_window(71,60,80,30);
		set_text_bg(3);
		set_text_fg(0);
		putch(12);

		set_text_bg(0);
		set_text_fg(3);
		printf(" 0x%06X\r\n\r", (unsigned int)ftell(hex_file_b) - 4);
		set_text_bg(3);
		set_text_fg(0);

		printf("%s",to_bin(byte_b[0]));
		printf("\r\n\nu8:\r\n%u", byte_b[0]);	
		printf("\r\n\ns8:\r\n%d", (char)byte_b[0]);
		printf("\r\n\nu16:\r\n%u", (uint16_t)byte_b[0] | ((uint16_t)byte_b[1] << 8));
		printf("\r\n\ns16:\r\n%d", (int16_t)byte_b[0] | ((int16_t)byte_b[1] << 8));
		printf("\r\n\nu24:\r\n%lu", ((uint32_t)byte_b[2] << 16) | ((uint32_t)byte_b[1] << 8) | (uint32_t)byte_b[0]);	
		int32_t int24 = ((int32_t)byte_b[2] << 16) | ((uint32_t)byte_b[1] << 8) | (uint32_t)byte_b[0];
		if(byte_b[2] & 0x80) {
			int24 |= 0xFF000000;
		}
		printf("\r\n\ns24:\r\n%ld", int24);
		printf("\r\n\nu32:\r\n%lu", ((uint32_t)byte_b[3] << 24) | ((uint32_t)byte_b[2] << 16) | ((uint32_t)byte_b[1] << 8) | (uint32_t)byte_b[0]);
		printf("\r\n\ns32:\r\n%ld", ((int32_t)byte_b[3] << 24) | ((int32_t)byte_b[2] << 16) | ((int32_t)byte_b[1] << 8) | (int32_t)byte_b[0]);

	}

	if (diff) set_text_window(0,30,80,1);
	else set_text_window(0,60,80,1);

	set_text_bg(0);
	set_text_fg(3);

	cursor_tab(old_cursor_x, old_cursor_y);

}

uint32_t search_string(const char *search_string, uint32_t start_offset) {

    size_t len = strlen(search_string);
    char *buffer = (char *)malloc(len + 1);
    buffer[len] = '\0';

    uint32_t  position = start_offset;
    while (fseek(hex_file, position, SEEK_SET) == 0) {
        if (fread(buffer, 1, len, hex_file) == len) {
            if (strncmp(buffer, search_string, len) == 0) {
                break;
            }
        } else {
            free(buffer);
			return 0;
        }
        position++;
    }

    free(buffer);

    return position;

}

void append(char *str, char ch) {
    int len = strlen(str);
    str[len] = ch;
    str[len + 1] = '\0';
}

int main(int argc, char * argv[])
{

	sv = vdp_vdu_init();
	if ( vdp_key_init() == -1 ) return 1;

	hex_file = fopen(argv[1], "rb+");
    if (hex_file == NULL) {
        printf("Could not open %s\r\n", argv[1]);
		return 0;
    }

	fseek(hex_file, 0, SEEK_END);
	hex_file_size = ftell(hex_file);
	fseek(hex_file, 0, SEEK_SET);

	hex_file_rump = hex_file_size % 16;
	hex_file_bottom = hex_file_size - hex_file_rump;

	if (argc >= 3) {

		hex_file_b = fopen(argv[2], "rb+");
		if (hex_file_b == NULL) {
			printf("Could not open second file %s\r\n", argv[1]);
			return 0;
		}

		fseek(hex_file_b, 0, SEEK_END);
		hex_file_size_b = ftell(hex_file_b);
		fseek(hex_file_b, 0, SEEK_SET);

		hex_file_rump_b = hex_file_size_b % 16;
		hex_file_bottom_b = hex_file_size_b - hex_file_rump_b;		

		diff = true;
		lines_per_window = 29;

	}	

	uint8_t old_mode = sv->scrMode;

	putch(22);
	putch(1);
	cursor_set(false);

	header_bar();

	//left, bottom, right, top

	if (diff) set_text_window(0,30,80,1);
	else set_text_window(0,60,80,1);

	cursor_tab(0,0);	

	uint32_t last_result = 0;

	if (diff) fill_screen_diff(0);
	else fill_screen(0);

	//fill_bottom_screen(0);

	select_hex(0,0);
	//select_bottom_hex(0,0);

	uint16_t old_key_count = sv->vkeycount;

	uint8_t input_type = 0;
	char jump[25] = "\0";
	char search[25] = "\0";

	char new_value[3] = {0};

	int16_t new_value_1 = -1;
	int16_t new_value_2 = -1;

	while(true) {

		// 27/125	ESC
		// 0/146	PG UP
		// 0/148	PG DOWN
		// 11/150	UP
		// 10/152	DOWN
		// 8/154	LEFT
		// 21/156	RIGHT
		// 13/143	ENTER
		// 32/1		SPACE
		// 0/67		F3

		#define INPUT_NONE		0
		#define INPUT_REPLACE	1
		#define INPUT_SEARCH	2
		#define INPUT_JUMP		3

		#define KEY_UP		11
		#define KEY_DOWN	10
		#define KEY_LEFT	8
		#define KEY_RIGHT	21
		#define KEY_ENTER	13
		#define KEY_SPACE	32
		#define KEY_BACK	127
		#define VKEY_PGDOWN	148
		#define VKEY_PGUP	146
		#define VKEY_F1		159
		#define VKEY_F2		160
		#define VKEY_F3		161

		if (sv->vkeycount != old_key_count && sv->vkeydown == 0) {

			print_to_debug("Key pressed: %u / %u\r\n", sv->keyascii, sv->vkeycode);

			if (sv->keyascii == 27 || sv->keyascii == 'q') {
				break;
			}

			if (input_type) {

				if (input_type == INPUT_REPLACE) {

					if (isxdigit(sv->keyascii)) {

						if (new_value_1 == -1 && new_value_2 == -1) {
							new_value_1 = sv->keyascii;
							printf("%c", toupper(new_value_1));
						} else if (new_value_1 != -1 && new_value_2 == -1) {
							new_value_2 = sv->keyascii;
							printf("%c", toupper(new_value_2));

							current_offset = top_offset + (16 * selected_y) + selected_x;
							fseek(hex_file, current_offset, SEEK_SET);
							
							new_value[0] = new_value_1;
							new_value[1] = new_value_2;
							new_value[2] = '\0';

							uint8_t val_out = (uint8_t)strtoul(new_value, NULL, 16);
							
							fputc(val_out, hex_file);
							//fwrite(&val_out, sizeof(uint8_t), 1, hex_file);
							
							current_offset = top_offset + (16 * selected_y);
							fseek(hex_file, current_offset, SEEK_SET);

							input_type = INPUT_NONE;
							new_value_1 = -1;
							new_value_2 = -1;						
							
							redraw_current_line();
							select_hex(0,0);

						}

					}

				} else if (input_type == INPUT_SEARCH) {

					if (isalnum(sv->keyascii) && strlen(search) < 16) {
						printf("%c", sv->keyascii);
						append(search, sv->keyascii);
					
					} else if (sv->keyascii == KEY_ENTER) {
						cursor_set(false);
						cursor_tab(0,0);
						printf("                ");
						set_text_bg(0);
						set_text_fg(3);								
							if (diff) set_text_window(0,30,80,1);
							else set_text_window(0,60,80,1);
						putch(0x0C); //CLS
						last_result = search_string(search, 0);
						fill_screen(last_result);
						selected_x = (last_result % 16);
						selected_y = 0;
						select_hex(0,0);
						input_type = INPUT_NONE;
						
					} else if (sv->keyascii == KEY_BACK) {

						printf("\b \b");
						search[strlen(search)] = '\0';

					}

				} else if (input_type == INPUT_JUMP) {

					if ((isxdigit(sv->keyascii) || sv->keyascii == 'x' || sv->keyascii == 'X') && strlen(jump) < 16) {
						printf("%c", sv->keyascii);
						append(jump, sv->keyascii);
					} else if (sv->keyascii == KEY_ENTER) {
						cursor_set(false);
						cursor_tab(0,0);
						printf("                ");
						set_text_bg(0);
						set_text_fg(3);								
							if (diff) set_text_window(0,30,80,1);
							else set_text_window(0,60,80,1);
						putch(0x0C); //CLS
						fill_screen(strtoul(jump,NULL,16));
						selected_x = 0;
						selected_y = 0;
						select_hex(0,0);
						input_type = INPUT_NONE;

					}

				}				

			} else {

				if (sv->vkeycode == VKEY_F3) {

					putch(0x0C); //CLS
					last_result = search_string(search, last_result + 1);
					fill_screen(last_result);
					selected_x = (last_result % 16);
					selected_y = 0;
					select_hex(0,0);

				}

				else if (sv->vkeycode == VKEY_F1) {

					//left, bottom, right, top
					set_text_window(55,1,80,0);
					cursor_tab(0,0);
					cursor_set(true);
					set_text_bg(3);
					set_text_fg(0);					
					
					input_type = INPUT_JUMP;
					jump[0] = '\0';

				}

				else if (sv->vkeycode == VKEY_F2) {

					//left, bottom, right, top
					set_text_window(55,1,80,0);
					cursor_tab(0,0);
					cursor_set(true);
					set_text_bg(3);
					set_text_fg(0);

					input_type = INPUT_SEARCH;
					search[0] = '\0';

				}				

				else if (!diff && sv->keyascii == KEY_SPACE) {

					redraw_current_line();
					clear_hex(2);
					input_type = INPUT_REPLACE;
					new_value_1 = -1;
					new_value_2 = -1;

				}

				else if (sv->keyascii == KEY_UP) {

					if (selected_y > 0) select_hex(0, -1);
					else if (selected_y == 0 && top_offset > 0) {

						//left, bottom, right, top
						set_text_window(0,29,70,1);

						unselect_hex();
						scroll_text_down(1);
						
						current_offset = top_offset - 16;
						top_offset -= 16;
						bottom_offset -= 16;

						cursor_tab(0,0);
						fill_line(false);
						
						//left, bottom, right, top
						set_text_window(0,60,70,31);

						unselect_bottom_hex();
						scroll_text_down(1);
						
						current_offset_b = top_offset_b - 16;
						top_offset_b -= 16;
						bottom_offset_b -= 16;

						cursor_tab(0,0);
						fill_line_bottom(false);

						select_hex(0,0);

					}

				}

				else if (sv->keyascii == KEY_DOWN) {

					if (
						(selected_y < (lines_per_window - 1))
						&& (current_offset + selected_x + 16 < hex_file_size)
						&& (current_offset_b + selected_x + 16 < hex_file_size_b)
						) {
						select_hex (0, 1);
						//if (diff) select_bottom_hex(0,1);
					}
					//else if (current_offset + selected_x + 16 < hex_file_size) select_hex(0, 1);
					else if (selected_y == (lines_per_window - 1) && (current_offset + selected_x + 16 < hex_file_size)) {
						
						//left, bottom, right, top
						set_text_window(0,29,80,1);

						unselect_hex();
						cursor_tab(79,28);
						
						current_offset = bottom_offset + 16;
						fill_line(true);
						
						top_offset += 16;
						bottom_offset += 16;
						
						//left, bottom, right, top
						set_text_window(0,60,80,32);
						
						unselect_bottom_hex();
						cursor_tab(78, lines_per_window);
						
						current_offset_b = bottom_offset_b + 16;
						fill_line_bottom(true);
						
						top_offset_b += 16;
						bottom_offset_b += 16;

						select_hex(0, 0);						

					}
					
					
				}

				else if (sv->vkeycode == VKEY_PGDOWN) {
					
					//if (selected_y + 8 < 58 && current_offset + selected_x + (16 * 8) < hex_file_bottom) select_hex(0, 8);
					if (bottom_offset < hex_file_bottom && bottom_offset_b < hex_file_bottom_b) {
						putch(0x0C); //CLS
						if (diff) fill_screen_diff(bottom_offset + 16);
						else fill_screen(bottom_offset + 16);
						selected_y = 0;
						select_hex(0,0);
					}

				}

				else if (sv->vkeycode == VKEY_PGUP) {
					
					//if (selected_y + 8 < 58 && current_offset + selected_x + (16 * 8) < hex_file_bottom) select_hex(0, 8);
					putch(0x0C); //CLS
					if (diff) fill_screen_diff(max(0, top_offset - (lines_per_window * 16)));
					else fill_screen(max(0, top_offset - (lines_per_window * 16)));					
					selected_y = 0;
					select_hex(0,0);

				}						

				else if (sv->keyascii == KEY_LEFT) {

					if (selected_x > 0) select_hex(-1, 0);

				}

				else if (sv->keyascii == KEY_RIGHT) {

					if (selected_x < 15) {
						if (current_offset != hex_file_bottom) select_hex(1, 0);
						else if (selected_x < hex_file_rump - 1) select_hex(1, 0);
					}

				}

			}
			
			old_key_count = sv->vkeycount;

		}			

	}

	fclose(hex_file);
	fclose(hex_file_b);

	putch(22);
	putch(old_mode);

	return 0;

}