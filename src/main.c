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
long hex_file_size = 0;
long hex_file_bottom = 0;
uint8_t hex_file_rump = 0;
long current_offset = 0;
long top_offset = 0;
long bottom_offset = 0;


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

void fill_top_line() {
    
	unsigned char x16[16];

	fseek(hex_file, current_offset, SEEK_SET);

    size_t bytes_read = fread(x16, sizeof(uint8_t), 16, hex_file);

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

void fill_bottom_line() {
    
	unsigned char x16[16];

	fseek(hex_file, current_offset, SEEK_SET);

    size_t bytes_read = fread(x16, sizeof(uint8_t), 16, hex_file);

    printf("\r\n");

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
	top_offset = ((start_offset / 16) * 16);
    fseek(hex_file, top_offset, SEEK_SET); // Start from top_offset
    size_t bytes_read;
    uint8_t line_count = 0;

    current_offset = top_offset; // Ensure current_offset starts at top_offset

    while ((line_count < 59) && (current_offset < hex_file_size)) {
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
		if (line_count != 59) printf("\r\n");
    }
    bottom_offset = current_offset - bytes_read; // Update bottom_offset after filling the screen
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
	cursor_tab(53 + selected_x, selected_y);
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

	cursor_tab(7 + (selected_x * 3), selected_y);
	current_offset = top_offset + (16 * selected_y) + selected_x;
	fseek(hex_file, current_offset, SEEK_SET);
	
	unsigned char byte[4];
	
	fread(byte, 1, 1, hex_file);
	set_text_bg(0);
	set_text_fg(3);
	printf("%02X", byte[0]);
	cursor_tab(53 + selected_x, selected_y);
	printf("%c", is_ag_print(byte[0]) ? byte[0] : '.');	

	selected_x += delta_x;
	selected_y += delta_y;

	cursor_tab(7 + (selected_x * 3), selected_y);
	current_offset = top_offset + (16 * selected_y) + selected_x;
	fseek(hex_file, current_offset, SEEK_SET);

	fread(byte, 1, 4, hex_file);
	
	set_text_bg(1);
	set_text_fg(3);
	
	printf("%02X", byte[0]);

	cursor_tab(53 + selected_x, selected_y);
	printf("%c", is_ag_print(byte[0]) ? byte[0] : '.');

	set_text_bg(0);
	set_text_fg(3);

	current_offset = top_offset + (16 * selected_y);

	uint8_t old_cursor_x = sv->cursorX;
	uint8_t old_cursor_y = sv->cursorY;

	//left, bottom, right, top
	set_text_window(72,60,80,1);
	putch(12);
	printf("\r\n");
	printf("Binary:\r\n%s\r\n",to_bin(byte[0]));
	printf("\r\n");
	printf("uint8:\r\n%u\r\n", byte[0]);	
	printf("\r\n");
	printf("int8:\r\n%d\r\n", (char)byte[0]);
	printf("\r\n");
	printf("uint16:\r\n%u\r\n", (uint16_t)byte[0] | ((uint16_t)byte[1] << 8));
	printf("\r\n");
	printf("int16:\r\n%d\r\n", (int16_t)byte[0] | ((int16_t)byte[1] << 8));
	printf("\r\n");
	printf("uint24:\r\n%lu\r\n", ((uint32_t)byte[2] << 16) | ((uint32_t)byte[1] << 8) | (uint32_t)byte[0]);	
	printf("\r\n");
	int32_t int24 = ((int32_t)byte[2] << 16) | ((uint32_t)byte[1] << 8) | (uint32_t)byte[0];
	if(byte[2] & 0x80) {
    	int24 |= 0xFF000000;
	}
	printf("int24:\r\n%ld\r\n", int24);
	printf("\r\n");
	printf("uint32:\r\n%lu\r\n", ((uint32_t)byte[3] << 24) | ((uint32_t)byte[2] << 16) | ((uint32_t)byte[1] << 8) | (uint32_t)byte[0]);
	printf("\r\n");	
	printf("int32:\r\n%ld\r\n", ((int32_t)byte[3] << 24) | ((int32_t)byte[2] << 16) | ((int32_t)byte[1] << 8) | (int32_t)byte[0]);

	set_text_window(0,60,80,1);
	cursor_tab(old_cursor_x, old_cursor_y);

}

uint32_t search_string(const char *search_string, uint32_t start_offset) {

    size_t len = strlen(search_string);
    char *buffer = (char *)malloc(len + 1);
    if (!buffer) {
        printf("Memory allocation failed\r\n");
		return 0;
    }
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
        printf("Could not open %s.\r\n", argv[1]);
		return 0;
    }

	fseek(hex_file, 0, SEEK_END);
	hex_file_size = ftell(hex_file);
	fseek(hex_file, 0, SEEK_SET);

	hex_file_rump = hex_file_size % 16;
	hex_file_bottom = hex_file_size - hex_file_rump;

	uint8_t old_mode = sv->scrMode;

	putch(22);
	putch(1);
	cursor_set(false);
	
	set_text_bg(3);
	set_text_fg(0);
	//printf("0x   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F ################ Decoded   ");
	printf("0x     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F                            ");
	set_text_bg(0);
	set_text_fg(3);

	//left, bottom, right, top
	set_text_window(0,60,80,1);
	cursor_tab(0,0);	

	uint32_t last_result = 0;

	fill_screen(0);

	select_hex(0,0);

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
		#define VKEY_PGDOWN	148
		#define VKEY_PGUP	146
		#define VKEY_F1		159
		#define VKEY_F2		160
		#define VKEY_F3		161

		if (sv->vkeycount != old_key_count && sv->vkeydown == 0) {

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

					if (isalnum(sv->keyascii) && strlen(search) < 25) {
						printf("%c", sv->keyascii);
						append(search, sv->keyascii);
					
					} else if (sv->keyascii == KEY_ENTER) {
						cursor_set(false);
						cursor_tab(0,0);
						printf("                           ");
						set_text_bg(0);
						set_text_fg(3);								
						set_text_window(0,60,80,1);
						putch(0x0C); //CLS
						last_result = search_string(search, 0);
						fill_screen(last_result);
						selected_x = (last_result % 16);
						select_hex(0,0);
						input_type = INPUT_NONE;
						//search[0] = '\0';
					}

				} else if (input_type == INPUT_JUMP) {

					if ((isxdigit(sv->keyascii) || sv->keyascii == 'x' || sv->keyascii == 'X') && strlen(jump) < 25) {
						printf("%c", sv->keyascii);
						append(jump, sv->keyascii);
					} else if (sv->keyascii == KEY_ENTER) {
						cursor_set(false);
						cursor_tab(0,0);
						printf("                           ");
						set_text_bg(0);
						set_text_fg(3);								
						set_text_window(0,60,80,1);
						putch(0x0C); //CLS
						fill_screen(strtoul(jump,NULL,16));
						selected_x = 0;
						selected_y = 0;
						select_hex(0,0);
						input_type = INPUT_NONE;

					}

				}				

			} else {
			
				//print_to_debug("Key pressed: %u / %u\r\n", sv->keyascii, sv->vkeycode);

				if (sv->vkeycode == VKEY_F3) {

					putch(0x0C); //CLS
					last_result = search_string(search, last_result + 1);
					fill_screen(last_result);
					selected_x = (last_result % 16);
					select_hex(0,0);

				}

				else if (sv->vkeycode == VKEY_F1) {

					//left, bottom, right, top
					set_text_window(53,1,80,0);
					cursor_tab(0,0);
					cursor_set(true);
					set_text_bg(3);
					set_text_fg(0);					
					
					input_type = INPUT_JUMP;
					jump[0] = '\0';

				}

				else if (sv->vkeycode == VKEY_F2) {

					//left, bottom, right, top
					set_text_window(53,1,80,0);
					cursor_tab(0,0);
					cursor_set(true);
					set_text_bg(3);
					set_text_fg(0);

					input_type = INPUT_SEARCH;
					search[0] = '\0';

				}				

				else if (sv->keyascii == KEY_SPACE) {

					redraw_current_line();
					clear_hex(2);
					input_type = INPUT_REPLACE;
					new_value_1 = -1;
					new_value_2 = -1;

				}

				else if (sv->keyascii == KEY_UP) {

					if (selected_y > 0) select_hex(0, -1);
					else if (selected_y == 0 && top_offset > 0) {

						unselect_hex();
						scroll_text_down(1);
						current_offset = top_offset - 16;
						top_offset -= 16;
						bottom_offset -= 16;

						cursor_tab(0,0);
						fill_top_line();
						select_hex(0,0);

					}

				}

				else if (sv->keyascii == KEY_DOWN) {

					if (selected_y < 58 && (current_offset + selected_x + 16 < hex_file_size)) select_hex (0, 1);
					//else if (current_offset + selected_x + 16 < hex_file_size) select_hex(0, 1);
					else if (selected_y == 58 && (current_offset + selected_x + 16 < hex_file_size)) {
						
						unselect_hex();
						cursor_tab(78,58);
						current_offset = bottom_offset + 16;
						fill_bottom_line();
						top_offset += 16;
						bottom_offset += 16;

						select_hex(0, 0);
					}
					
					//current_offset + selected_x + 16 < hex_file_size) select_hex(0, 1);
					
				}

				else if (sv->vkeycode == VKEY_PGDOWN) {
					
					//if (selected_y + 8 < 58 && current_offset + selected_x + (16 * 8) < hex_file_bottom) select_hex(0, 8);
					if (bottom_offset < hex_file_bottom) {
						putch(0x0C); //CLS
						fill_screen(bottom_offset + 16);
						selected_y = 0;
						select_hex(0,0);
					}

				}

				else if (sv->vkeycode == VKEY_PGUP) {
					
					//if (selected_y + 8 < 58 && current_offset + selected_x + (16 * 8) < hex_file_bottom) select_hex(0, 8);
					putch(0x0C); //CLS
					fill_screen(max(0, top_offset - (59 * 16)));
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

	putch(22);
	putch(old_mode);

	return 0;

}