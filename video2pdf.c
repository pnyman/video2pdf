#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include "lib/pdfgen.h"
#include "video2pdf.h"

#define MAX_TIMESTAMPS 100 // adjust if needed
#define MAX_PATH 1024

char *videofile = NULL;
int timestamps[MAX_TIMESTAMPS];
int timestamp_count = 0;
char outfilename[MAX_PATH];
char outputfile[MAX_PATH];

char *imgfile = "screenshot.jpg";
char *typeface = "Times-Roman";
int font_size = 12;

typedef unsigned char BYTE_ARRAY[];

int margins = 55;
int start_y_pos = 555; // magic number

static struct option long_options[] = {
    {"input", required_argument, 0, 'i'},
    {"output", required_argument, 0, 'o'},
    {"margins", optional_argument, 0, 'm'},
    {"timestamps", required_argument, 0, 't'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
};


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

// * prompt_help()

void prompt_help(void) {
    printf("Tillgängliga kommandon:\n");
    printf("  i <inputfil>\n");
    printf("  o <outputfil>\n");
    printf("  m <marginal>\n");
    printf("  t <timestamp>\n");
    printf("  s (status)\n");
    printf("  r (kör)\n");
    printf("  h (hjälp)\n");
    printf("  q (avsluta)\n");
}

// * prompt_for_input

void prompt_for_input(void) {
    char input[1024];
    printf("Ingen parameter angiven. Ange kommandon manuellt.\n");
    prompt_help();

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
            printf("Felaktig inmatning.\n");
            continue;
        }

        switch (command) {

        case 'i':
            strncpy(videofile, argument, MAX_PATH - 1);
            videofile[MAX_PATH - 1] = '\0';
            printf("Inputfil satt till: %s\n", videofile);
            break;

        case 'o':
            strncpy(outfilename, argument, MAX_PATH - 1);
            outfilename[MAX_PATH - 1] = '\0';
            printf("Outputfil satt till: %s\n", outfilename);
            break;

        case 'm':
            margins = atoi(argument);
            printf("Marginaler satta till: %d\n", margins);
            break;

        case 't': {
            if (strlen(argument) == 0) {
                printf("Ingen timestamp angiven.\n");
                break;
            }

            char *token = strtok(argument, " ");
            while (token != NULL) {
                if (timestamp_count >= MAX_TIMESTAMPS) {
                    fprintf(stderr, "För många timestamps (max %d).\n", MAX_TIMESTAMPS);
                    break;
                }
                timestamps[timestamp_count++] = parse_timestamp(token);
                token = strtok(NULL, " ");
            }
            break;
        }

        case 'r':
            if (!videofile || strlen(videofile) == 0 || strlen(outfilename) == 0 || timestamp_count == 0) {
                printf("Alla obligatoriska parametrar är inte satta.\n");
            }
            else {
                printf("\nSkapar PDF...\n");
                set_output_path(videofile, outfilename);
                create_pdf();
                printf("PDF skapad. Du kan nu ändra parametrar eller köra igen.\n");
            }
            break;

        case 's':
            printf("\nAktuella inställningar:\n");
            printf("  Inputfil: %s\n", videofile[0] ? videofile : "Ej satt");
            printf("  Outputfil: %s\n", outfilename[0] != '\0' ? outfilename : "Ej satt");
            printf("  Marginaler: %d\n", margins);
            printf("  Timestamps: ");
            if (timestamp_count > 0) {
                for (int i = 0; i < timestamp_count; i++) {
                    char *ts = format_timestamp(timestamps[i]);
                    printf("%s ", ts);
                    free(ts);
                }
                printf("\n");
            } else {
                printf("Inga timestamps satta.\n");
            }
            break;

        case 'h':
            prompt_help();
            break;

        case 'q':
            printf("Avslutar programmet.\n");
            return; // <-- Avsluta funktionen, och därmed programmet om du vill

        default:
            printf("Ange i, o, m, t, r, s, h eller q.\n");
            break;
        }
    }
}

// * help()

void help(void) {
    printf("Usage: vip -i <inputfile> -o <outputfile> -m <margins> -t <timestamps>\n\n");

    // Dynamiskt genererad hjälptext
    printf("Options:\n");
    for (int i = 0; long_options[i].name != NULL; i++) {
        if (long_options[i].has_arg == required_argument)
            printf("  -%c, --%s=<arg>   Required argument\n", long_options[i].val, long_options[i].name);
        else
            printf("  -%c, --%s        No argument\n", long_options[i].val, long_options[i].name);
    }
    printf("\n");
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
            /* tsparam = true; */
            break;

        case 'h':
        default:
            help();
            return EXIT_FAILURE;
        }
    }

    // Om inga parametrar är angivna, fråga användaren om input
    if (!videofile || !outputparam || margins == 0 || timestamp_count == 0) {
        prompt_for_input();
    }

    else {
        set_output_path(videofile, outfilename);
        create_pdf();
    }

    free(videofile);
    return EXIT_SUCCESS;
}
