#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include "lib/pdfgen.h"

// * Globals

#define MAX_TIMESTAMPS 100 // adjust if needed

#ifdef _WIN32
#  include <direct.h>   // _getcwd
#  define getcwd _getcwd
#  define PATH_SEP '\\'
#else
#  include <unistd.h>   // getcwd
#  define PATH_SEP '/'
#endif

#ifdef _WIN32
#  define MAX_PATH_LEN 260
#else
#  include <limits.h>
#  define MAX_PATH_LEN PATH_MAX
#endif

char *videofile = NULL;
int timestamps[MAX_TIMESTAMPS];
int timestamp_count = 0;
char outfilename[MAX_PATH_LEN];
char outputfile[MAX_PATH_LEN];
char imgfile[MAX_PATH_LEN];

char *typeface = "Times-Roman";
int font_size = 12;

typedef unsigned char BYTE_ARRAY[];

int margins = 0;
int top_margin = 0;
int start_y_pos = 455; // magic number

static struct option long_options[] = {
    {"input", required_argument, 0, 'i'},
    {"output", required_argument, 0, 'o'},
    {"timestamps", required_argument, 0, 't'},
    {"margins", optional_argument, 0, 'm'},
    {"top_margin", optional_argument, 0, 'u'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
};



// * Forward declarations

bool get_jpeg_dim(BYTE_ARRAY data, size_t data_size, int *width, int *height);
unsigned char* read_file(const char* filename, size_t* filesize);
void take_screenshot(int seconds);
int parse_timestamp(const char *str);
void set_output_path(const char *videopath, const char *outfilename);
int create_pdf();
char *format_timestamp(int seconds);
void open_outputfile(void);
void download_video(const char *url);
void prompt_help(void);
void prompt_for_input(void);
void help(void);

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

    if (return_code != 0) {
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
#ifdef _WIN32
    if (!last_sep) {
        last_sep = strrchr(videopath, '\\');  // Windows-separator
    }
#endif

    if (!last_sep) {
        // Ingen separator – använd nuvarande arbetskatalog
        if (!getcwd(outputfile, MAX_PATH_LEN)) {
            perror("getcwd failed");
            exit(EXIT_FAILURE);
        }

        size_t len = strlen(outputfile);
        if (len + 1 >= MAX_PATH_LEN) {
            fprintf(stderr, "Sökvägen är för lång\n");
            exit(EXIT_FAILURE);
        }

        outputfile[len] = '/';
        outputfile[len + 1] = '\0';

        strncat(outputfile, outfilename, MAX_PATH_LEN - strlen(outputfile) - 1);
        return;
    }

    // Hämta katalogdelen av sökvägen
    size_t dirlen = last_sep - videopath + 1;
    if (dirlen >= MAX_PATH_LEN) {
        fprintf(stderr, "Sökvägen är för lång\n");
        exit(EXIT_FAILURE);
    }

    strncpy(outputfile, videopath, dirlen);
    outputfile[dirlen] = '\0';

    strncat(outputfile, outfilename, MAX_PATH_LEN - dirlen - 1);
}

/* void set_output_path(const char *videopath, const char *outfilename) { */
/*     const char *last_sep = strrchr(videopath, '/'); */
/*     if (!last_sep) { */
/*         last_sep = strrchr(videopath, '\\');  // support Windows paths too */
/*     } */
/*  */
/*     if (!last_sep) { */
/*         fprintf(stderr, "Kunde inte hitta sökvägsseparator i videofil: %s\n", videopath); */
/*         exit(EXIT_FAILURE); */
/*     } */
/*  */
/*     size_t dirlen = last_sep - videopath + 1; */
/*     if (dirlen >= MAX_PATH_LEN) { */
/*         fprintf(stderr, "Sökvägen är för lång\n"); */
/*         exit(EXIT_FAILURE); */
/*     } */
/*  */
/*     strncpy(outputfile, videopath, dirlen); */
/*     outputfile[dirlen] = '\0'; */
/*  */
/*     strncat(outputfile, outfilename, MAX_PATH_LEN - dirlen - 1); */
/* } */

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

    int this_y_pos = start_y_pos - top_margin;
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
            this_y_pos = start_y_pos - top_margin;
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

// * format_timestamp()

char *format_timestamp(int seconds) {
    int minutes = seconds / 60;
    int secs = seconds % 60;

    // Allokera tillräckligt med plats för "mmm:ss\0" (max 5+1 tecken)
    // 999:59 är största normala fallet => 6 tecken.
    char *buffer = malloc(8); // 7 tecken + nullbyte

    if (buffer == NULL) {
        return NULL; // Hantera minnesfel om malloc misslyckas
    }

    snprintf(buffer, 8, "%d:%02d", minutes, secs);
    return buffer;
}

// * open_outputfile()

void open_outputfile(void) {
    if (outputfile[0] == '\0') {
        printf("No output file set.\n");
        return;
    }

    char command[MAX_PATH_LEN + 20];

#ifdef _WIN32
    snprintf(command, sizeof(command), "start \"\" \"%s\"", outputfile);
#elif __APPLE__
    snprintf(command, sizeof(command), "open \"%s\"", outputfile);
#else // Linux och andra Unix
    snprintf(command, sizeof(command), "xdg-open \"%s\"", outputfile);
#endif

    int result = system(command);
    if (result != 0) {
        printf("Failed to open file.\n");
    }
}

// * download_video()

void download_video(const char *url) {
    char command[512];
    snprintf(command, sizeof(command),
             "yt-dlp -f mp4 -o \"%s\" \"%s\"",
             videofile, url);
    printf("Downloading video %s, saving as %s...\n", url, videofile);
    system(command);
}

// * prompt_help()

void prompt_help(void) {
    printf("Available commands:\n");
    printf("  d <url>\n");
    printf("  i <input file>\n");
    printf("  o <output file>\n");
    printf("  t <time stamps>\n");
    printf("  m <left/right margins> (optional)\n");
    printf("  u <top margin> (optional)\n");
    printf("  s show settings\n");
    printf("  c clear settings\n");
    printf("  r run\n");
    printf("  v view pdf-file\n");
    printf("  h help\n");
    printf("  q quit\n");
}

// * prompt_for_input

void prompt_for_input(void) {
    char input[1024];
    printf("Welcome to VIP!\n");
    prompt_help();

    char *url = NULL;
    url = malloc(MAX_PATH_LEN);
    url[0] = '\0';

    bool ready = false;
    while (!ready) {
        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        // Ta bort newline
        input[strcspn(input, "\n")] = 0;

        // Hoppa över tomma rader
        if (input[0] == '\0') {
            continue;
        }

        char command;
        char argument[1000] = "";

        // Plocka ut första bokstaven (kommando) och resten som argument
        if (sscanf(input, "%c %[^\n]", &command, argument) < 1) {
            printf("Incorrect argument.\n");
            continue;
        }

        switch (command) {

        case 'd':
            strncpy(url, argument, MAX_PATH_LEN - 1);
            url[MAX_PATH_LEN - 1] = '\0';
            printf("Video url set to: %s\n", url);
            break;

        case 'i':
            /* strncpy(videofile, argument, MAX_PATH_LEN - 1); */
            /* videofile[MAX_PATH_LEN - 1] = '\0'; */
            snprintf(videofile, MAX_PATH_LEN, "%s.mp4", argument);
            printf("Input file set to: %s\n", videofile);

            /* strncpy(outfilename, argument, MAX_PATH_LEN - 1); */
            /* outfilename[MAX_PATH_LEN - 1] = '\0'; */
            snprintf(outfilename, MAX_PATH_LEN, "%s.pdf", argument);
            printf("Output file set to: %s\n", outfilename);

            break;

        case 'o':
            strncpy(outfilename, argument, MAX_PATH_LEN - 1);
            outfilename[MAX_PATH_LEN - 1] = '\0';
            printf("Output file set to: %s\n", outfilename);
            break;

        case 'm':
            margins = atoi(argument);
            printf("Margins set to: %d\n", margins);
            break;

        case 'u':
            top_margin = atoi(argument);
            printf("Top margin set to: %d\n", top_margin);
            break;

        case 't': {
            if (strlen(argument) == 0) {
                printf("No time stamps given.\n");
                break;
            }

            // clear old timestamps
            timestamp_count = 0;

            char *token = strtok(argument, " ");
            while (token != NULL) {
                if (timestamp_count >= MAX_TIMESTAMPS) {
                    fprintf(stderr, "Too many time stamps (max %d).\n", MAX_TIMESTAMPS);
                    break;
                }
                timestamps[timestamp_count++] = parse_timestamp(token);
                token = strtok(NULL, " ");
            }
            break;
        }

        case 'r':
            if (url && (!videofile || strlen(videofile) == 0)) {
                printf("Missing filename for download.\n");
                break;
            }
            if (!videofile || strlen(videofile) == 0 || strlen(outfilename) == 0 || timestamp_count == 0) {
                printf("Not all mandatory parameters are set.\n");
            }
            else {
                printf("Creating PDF...\n");
                set_output_path(videofile, outfilename);
                if (url[0]) {
                    download_video(url);
                }
                create_pdf();
                printf("PDF created. You can change parameters and run again.\n");
            }
            break;

        case 'v':
            open_outputfile();
            break;

        case 's':
            printf("Settings:\n");
            printf("  URL: %s\n", url[0] ? url : "Not set.");
            printf("  Input file: %s\n", videofile[0] ? videofile : "Not set.");
            printf("  Output file: %s\n", outfilename[0] != '\0' ? outfilename : "Not set.");
            printf("  Margins: %d\n", margins);
            printf("  Top margin: %d\n", top_margin);
            printf("  Time stamps: ");
            if (timestamp_count > 0) {
                for (int i = 0; i < timestamp_count; i++) {
                    char *ts = format_timestamp(timestamps[i]);
                    printf("%s ", ts);
                    free(ts);
                }
                printf("\n");
            }
            else {
                printf("No time stamps set.\n");
            }
            break;

        case 'c':
            printf("Clearing all settings.\n");
            videofile[0] = '\0';
            outfilename[0] = '\0';
            outputfile[0] = '\0';
            margins = 0;
            top_margin = 0;
            timestamp_count = 0;
            // (valfritt) nollställ timestamps-arrayen
            memset(timestamps, 0, sizeof(timestamps));
            break;

        case 'h':
            prompt_help();
            break;

        case 'q':
            printf("Quitting.\n");
            return;

        default:
            printf("Type i, o, m, u, t, r, s, c, h or q.\n");
            break;
        }
    }
}

// * help()

void help(void) {
    printf("Options:\n  %s\n  %s\n  %s\n  %s\n  %s\n  %s\n\n",
           "-i, --input=<inputfile>",
           "-o, --output=<outputfile>",
           "-m, --margins=<left/right margins>",
           "-u, --top_margin=<top margin>",
           "-t, --timestamps=<timestamps>",
           "-h, --help");

    /* printf("Options:\n"); */
    /* for (int i = 0; long_options[i].name != NULL; i++) { */
    /*     if (long_options[i].has_arg == required_argument) */
    /*         printf("  -%c, --%s=<arg>   Required argument\n", long_options[i].val, long_options[i].name); */
    /*     else if (long_options[i].has_arg == optional_argument) */
    /*         printf("  -%c, --%s=<arg>   Optional argument\n", long_options[i].val, long_options[i].name); */
    /*     else */
    /*         printf("  -%c, --%s        No argument\n", long_options[i].val, long_options[i].name); */
    /* } */
    /* printf("\n"); */
}

// * main

int main(int argc, char *argv[]) {

#ifdef _WIN32
    system("chcp 65001 > nul");
    setlocale(LC_ALL, ".UTF-8");
#else
    setlocale(LC_ALL, "");
#endif

#ifdef _WIN32
    const char *tempdir = getenv("TEMP");
    if (!tempdir) {
        tempdir = "."; // Om TEMP inte är satt, använd aktuell katalog
    }
    snprintf(imgfile, sizeof(imgfile), "%s\\vip-screenshot.jpg", tempdir);
#else
    const char *tempdir = getenv("TMPDIR");
    if (!tempdir) {
        tempdir = "/tmp";
    }
    snprintf(imgfile, sizeof(imgfile), "%s/vip-screenshot.jpg", tempdir);
#endif

    int opt;
    int option_index = 0;
    bool outputparam = false;

    videofile = malloc(MAX_PATH);
    videofile[0] = '\0';

    while ((opt = getopt_long(argc, argv, "i:o:m:u:t:h", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'i':
            videofile = optarg;
            break;

        case 'o':
            strncpy(outfilename, optarg, MAX_PATH_LEN - 1);
            outfilename[MAX_PATH_LEN - 1] = '\0';
            outputparam = true;
            break;

        case 'm':
            margins = atoi(optarg);
            break;

        case 'u':
            top_margin = atoi(optarg);
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
            break;

        case 'h':
        default:
            help();
            return EXIT_FAILURE;
        }
    }

    // Om inga parametrar är angivna, fråga användaren om input
    if (!videofile || !outputparam || timestamp_count == 0) {
        prompt_for_input();
    }

    else {
        set_output_path(videofile, outfilename);
        create_pdf();
    }

    free(videofile);
    return EXIT_SUCCESS;
}
