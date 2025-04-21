#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "pdfgen.h"

char *videofile = "videoplayback.mp4";
char *imgfile = "screenshot.jpg";
int width = 575;
typedef unsigned char BYTE_ARRAY[];
#define start_pos 500;

bool get_jpeg_dim(BYTE_ARRAY data, size_t data_size, int *width, int *height);
unsigned char* read_file(const char* filename, size_t* filesize);
void take_screenshot(int seconds);

/* * get_jpeg_dim */
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
			/* int bpc = data[off];     // Precision (bits per channel) */
			*height = (data[off+1]<<8) | data[off+2];
			*width = (data[off+3]<<8) | data[off+4];
			/* int cps = data[off+5];    // Number of color components */
			return true;
		}
		off += len - 2;
	}
	return false;
}

/* * read_file */
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

/* * take_screenshot */
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

/* * main */
int main(void) {
    struct pdf_info info = {
        .creator = "My software",
        .producer = "My software",
        .title = "My document",
        .author = "My name",
        .subject = "My subject",
        .date = "Today"
    };

    struct pdf_doc *pdf = pdf_create(PDF_A4_WIDTH, PDF_A4_HEIGHT, &info);
    pdf_set_font(pdf, "Times-Roman");

    int timestamps[] = {1, 34, 51, 67, 82, 97,
                        114, 129, 144, 154, 170, 189, 199,
                        209, 220, 230, 235, 255, 265, 275, 285,
                        300, 320, 330, 350, 360, 380,
                        400, 425};
    size_t number_of_ts = sizeof(timestamps) / sizeof(int);
    int top_y = start_pos;

    for (size_t i = 0; i < number_of_ts; i++) {
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

        float scale = (float)width / img_width;
        int scaled_height = img_height * scale;

        if (i == 0 || top_y - scaled_height < 0) {
            pdf_append_page(pdf);
            top_y = start_pos;
        }
        else {
            top_y -= scaled_height;
        }

        pdf_add_image_file(pdf, NULL, 10, top_y, width, -1, imgfile);
        remove(imgfile);
    }

    pdf_save(pdf, "output.pdf");
    pdf_destroy(pdf);
    return 0;
}
