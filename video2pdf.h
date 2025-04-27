#ifndef VIDEO2PDF_H
#define VIDEO2PDF_H

#include <stddef.h> // för size_t
#include <stdbool.h> // för bool

// Konstanter
#define MAX_TIMESTAMPS 100
#define MAX_PATH 1024
#define margin 55

// Globala variabler (deklarationer, ej definitioner)
extern char *videofile;
extern int timestamps[MAX_TIMESTAMPS];
extern int timestamp_count;
extern char outputfile[MAX_PATH];
extern char imgfile[MAX_PATH];
extern char *typeface;
extern int font_size;
extern int display_width;
extern int start_y_pos;

// Typdefinitioner
typedef unsigned char BYTE_ARRAY[];

// Funktioner
bool get_jpeg_dim(BYTE_ARRAY data, size_t data_size, int *width, int *height);
unsigned char* read_file(const char* filename, size_t* filesize);
void take_screenshot(int seconds);
int parse_timestamp(const char *str);
void set_output_path(const char *videopath, const char *outfilename);
int create_pdf();
char *format_timestamp(int seconds);
void prompt_help(void);
void prompt_for_input(void);
void help(void);

#endif // VIDEO2PDF_H
