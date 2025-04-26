#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "lib/pdfgen.h"
#include "video2pdf.h"

#define MAX_TIMESTAMPS 100 // adjust if needed
#define MAX_PATH 1024

char *videofile;
int timestamps[MAX_TIMESTAMPS];
int timestamp_count = 0;
char outputfile[MAX_PATH];

char *imgfile = "screenshot.jpg";
char *typeface = "Times-Roman";
int font_size = 12;

typedef unsigned char BYTE_ARRAY[];

int margins = 55;
int start_y_pos = 555; // magic number

// * get_jpeg_dim

bool get_jpeg_dim(BYTE_ARRAY data, size_t data_size, int *width, int *height) {
	*width = *height = -1;
	size_t off = 0;

	while (off < data_size) {
		while(data[off] == 0xff) {
			off++;
		}

		int mrkr = data[off];  off++;

		if (mrkr == 0xd8) continue;    // SOI
		if (mrkr == 0xd9) break;       // EOI
		if (0xd0 <= mrkr && mrkr <= 0xd7) continue;
		if (mrkr == 0x01) continue;    // TEM

		const int len = (data[off]<<8) | data[off+1];  off += 2;

		if (mrkr == 0xc0) {
			*height = (data[off+1]<<8) | data[off+2];
			*width = (data[off+3]<<8) | data[off+4];
			return true;
		}
		off += len - 2;
	}
	return false;
}

// * read_file

unsigned char* read_file(const char* filename, size_t* filesize) {
	FILE* f = fopen(filename, "rb");
	if (!f) return NULL;

	fseek(f, 0, SEEK_END);
	*filesize = ftell(f);
	rewind(f);

	unsigned char* buffer = malloc(*filesize);
	if (!buffer) {
		fclose(f);
		return NULL;
	}

	if (fread(buffer, 1, *filesize, f) != *filesize) {
		free(buffer);
		fclose(f);
		return NULL;
	}

	fclose(f);
	return buffer;
}

// * take_screenshot

void take_screenshot(int seconds) {
    char command[512];
    snprintf(command, sizeof(command),
             "ffmpeg -y -loglevel error -ss %d -i %s -frames:v 1 -q:v 1 %s",
             seconds, videofile, imgfile);

    int return_code = system(command);

    if (return_code == 0) {
        printf("Command executed successfully.\n");
    }
    else {
        printf("Command execution failed or returned "
               "non-zero: %d", return_code);
    }
}

// * parse_timestamp

int parse_timestamp(const char *str) {
    int minutes, seconds;
    if (sscanf(str, "%d:%d", &minutes, &seconds) != 2) {
        fprintf(stderr, "Ogiltigt tidsformat: %s\n", str);
        exit(EXIT_FAILURE);
    }
    return minutes * 60 + seconds;
}

// * set_output_path

void set_output_path(const char *videopath, const char *outfilename) {
    const char *last_sep = strrchr(videopath, '/');
    if (!last_sep) {
        last_sep = strrchr(videopath, '\\');  // support Windows paths too
    }

    if (!last_sep) {
        fprintf(stderr, "Kunde inte hitta sökvägsseparator i videofil: %s\n", videopath);
        exit(EXIT_FAILURE);
    }

    size_t dirlen = last_sep - videopath + 1;
    if (dirlen >= MAX_PATH) {
        fprintf(stderr, "Sökvägen är för lång\n");
        exit(EXIT_FAILURE);
    }

    strncpy(outputfile, videopath, dirlen);
    outputfile[dirlen] = '\0';

    strncat(outputfile, outfilename, MAX_PATH - dirlen - 1);
}

// * create_pdf

int create_pdf() {
    struct pdf_info info = {
        .creator = "My software",
        .producer = "My software",
        .title = "My document",
        .author = "My name",
        .subject = "My subject",
        .date = "Today"
    };

    int display_width = (PDF_A4_WIDTH - 2 * margins);
    struct pdf_doc *pdf = pdf_create(PDF_A4_WIDTH, PDF_A4_HEIGHT, &info);
    pdf_set_font(pdf, typeface);

    int this_y_pos = start_y_pos;
    int pagenr = 0;
    char page_str[20];

    for (int i = 0; i < timestamp_count; i++) {
        take_screenshot(timestamps[i]);

        size_t filesize = 0;
        unsigned char* jpeg_data = read_file(imgfile, &filesize);
        if (!jpeg_data) {
            fprintf(stderr, "Failed to read file.\n");
            return 1;
        }

        int img_width;
        int img_height;
        get_jpeg_dim(jpeg_data, filesize, &img_width, &img_height);
        free(jpeg_data);

        float scale = (float)display_width / img_width;
        int scaled_height = img_height * scale;

        if (i == 0 || this_y_pos - scaled_height < 0) {
            pdf_append_page(pdf);
            this_y_pos = start_y_pos;
            pagenr++;
        }
        else {
            this_y_pos -= scaled_height;
        }

        pdf_add_image_file(pdf, NULL, margins, this_y_pos, display_width, -1, imgfile);
        remove(imgfile);

        sprintf(page_str, "%d", pagenr);
        float text_width;
        pdf_get_font_text_width(pdf, typeface, page_str, font_size, &text_width);
        int x = (PDF_A4_WIDTH - text_width) / 2;
        pdf_add_text(pdf, NULL, page_str, font_size, x, 15, PDF_BLACK);
    }

    pdf_save(pdf, outputfile);
    pdf_destroy(pdf);
    return 0;
}

// * help()

void help(void) {
    printf("Usage: vip -i <inputfile> -o <outputfile> -m <margins> -t <timestamps>\n");
    printf("-i, --input:      mp4 file\n");
    printf("-o, --output:     name of resulting pdf file\n");
    printf("-m, --margins:    left/right margins (optional)\n");
    printf("-t, --timestamps: list of timestamps [m]m:ss separated by space\n");
    printf("-h, --help:       show this help\n");
}

// * main

int main(int argc, char *argv[]) {
    char outfilename[MAX_PATH];
    int opt;
    int option_index = 0;
    bool outputparam = false;
    bool tsparam = false;

    static struct option long_options[] = {
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"margins", optional_argument, 0, 'm'},
        {"timestamps", required_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "i:o:m:t:h", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'i':
            videofile = optarg;
            break;
        case 'o':
            strncpy(outfilename, optarg, MAX_PATH - 1);
            outfilename[MAX_PATH - 1] = '\0';
            outputparam = true;
            break;
        case 'm':
            margins = atoi(optarg);
            break;
        case 't':
            if (timestamp_count >= MAX_TIMESTAMPS) {
                fprintf(stderr, "För många tidsstämplar (max %d)\n", MAX_TIMESTAMPS);
                return EXIT_FAILURE;
            }
            // Lägg till första timestampen från optarg
            timestamps[timestamp_count++] = parse_timestamp(optarg);
            while (optind < argc && argv[optind][0] != '-') {
                if (timestamp_count >= MAX_TIMESTAMPS) {
                    fprintf(stderr, "För många tidsstämplar (max %d)\n", MAX_TIMESTAMPS);
                    return EXIT_FAILURE;
                }
                timestamps[timestamp_count++] = parse_timestamp(argv[optind]);
                optind++;
            }
            tsparam = true;
            break;
        case 'h':
        default:
            help();
            return EXIT_FAILURE;
        }
    }

    if (!videofile || !outputparam || !tsparam) {
        fprintf(stderr, "Fel: obligatoriska parametrar saknas.\n\n");
        help();
        return EXIT_FAILURE;
    }

    set_output_path(videofile, outfilename);
    create_pdf();

    return EXIT_SUCCESS;
}
