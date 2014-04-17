#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "devices.h"

void show_usage() {
	printf(
		"mktiupgrade - Makes TI calculator upgrade files from ROM dumps\n"
		"\n"
		"Usage: mktiupgrade [options] input output [pages...]\n"
		"See `man 1 mktiupgrade` for details.\n"
		"\n"
		"Examples:\n"
		"\tTo build and sign a 4 page upgrade file:\n"
		"\t\tmktiupgrade --key 0A.key input.rom output.8xu 00 01 02 03\n\n"
		"\tTo build and specify your own signature (for example, to exploit boot code vulnerabilities):\n"
		"\t\tmktiupgrade --signature exploit.sig input.rom output.8xu 00 01 02 03\n"
		"\n"
		"Page numbers should be specified in hex.\n"
		"\n"
		"Options:\n"
		"\t-k <key>, --key <key>\n"
		"\t\tSpecify a key file to sign with. If omitted, the upgrade will be unsigned.\n"
		"\t-n <name>, --key-name <name>\n"
		"\t\tSpecify the name of the key, in hex. Default value is taken from the upgrade file name (i.e. 0A.key).\n"
		"\t\tSpecify -n after specifying the key file, or it will be overwritten.\n"
		"\t-s <signature>, --signature <signature>\n"
		"\t\tSpecify a signature file.\n"
		"\t-v <major>.<minor>, --version <major>.<minor>\n"
		"\t\tSpecify the upgrade version. Default value is 1.0\n"
		"\t-r <revision>, --hw-revision <revision>\n"
		"\t\tSpecify the maximum hardware revision. Default is 5 (taken from official upgrades).\n"
		"\t-d <device>, --device <device>\n"
		"\t\tSpecify device type. Default is guessed from ROM dump length. Valid options are:\n"
		"\t\tTI-73, TI-83+, TI-83+SE, TI-84+, TI-84+SE, TI-84+CSE\n"
		"\t-p, --print\n"
		"\t\tPrints information about the OS before writing it.\n"
		"\t--help\n"
		"\t\tShow this message, then exit.\n"
		"\n"
		"Supported TI devices:\n"
		"\t- TI-73\n"
		"\t- TI-83+\n"
		"\t- TI-83+ Silver Edition\n"
		"\t- TI-84+\n"
		"\t- TI-84+ Silver Edition\n"
		"\t- TI-84+ Color Silver Edition\n"
	);
}

struct {
	char *infile;
	char *outfile;
	char *keyfile;
	char *sigfile;
	uint8_t key_name;
	uint8_t major_version;
	uint8_t minor_version;
	uint8_t hardware_revision;
	uint8_t device_type;
	int print_info;
	int pages[256];
} context;

void set_defaults() {
	int i;
	for (i = 0; i < 256; i++) {
		context.pages[i] = 0;
	}
	context.key_name = 0x0A;
	context.major_version = 1;
	context.minor_version = 0;
	context.hardware_revision = 5;
	context.device_type = 0xFF; // Will be automatically selected if not changed
	context.print_info = 0;
}

int parse_device(char *device, uint8_t *type) {
	if (strcasecmp("TI-73", device) == 0) {
		*type = TI73;
		return 1;
	} else if (strcasecmp("TI-83+", device) == 0) {
		*type = TI83p;
		return 1;
	} else if (strcasecmp("TI-83+SE", device) == 0) {
		*type = TI83pSE;
		return 1;
	} else if (strcasecmp("TI-84+", device) == 0) {
		*type = TI84p;
		return 1;
	} else if (strcasecmp("TI-84+SE", device) == 0) {
		*type = TI84pSE;
		return 1;
	} else if (strcasecmp("TI-84+CSE", device) == 0) {
		*type = TI84pCSE;
		return 1;
	}
	return 0;
}

uint8_t detect_device(FILE *rom) {
	fseek(rom, 0L, SEEK_END);
	long length = ftell(rom);
	fseek(rom, 0L, SEEK_SET);

	switch (length) {
		case 524288:
			fprintf(stderr, "Warning: Device detection ambiguous between TI-73 and TI-83+. Defaulting to TI-83+.\n");
			return TI83p;
		case 1048576:
			return TI84p;
		case 2097152:
			fprintf(stderr, "Warning: Device detection ambiguous between TI-83+ SE and TI-84+ SE. Defaulting to TI-84+ SE.\n");
			return TI84pSE;
		case 4194304:
			return TI84pCSE;
		default:
			return 0xFF;
	}
}

#define arg(short, long) strcasecmp(short, argv[i]) == 0 || strcasecmp(long, argv[i]) == 0

int main(int argc, char **argv) {
	set_defaults();
	int i;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (arg("-h", "--help")) {
				show_usage();
				exit(0);
			} else if (arg("-k", "--key")) {
				context.keyfile = argv[++i];
				char *end = context.keyfile + strlen(context.keyfile);
				long int name = strtol(context.keyfile, &end, 16);
				if (end == context.keyfile + 2 && name >= 0 && name < 0x100) {
					context.key_name = (uint8_t)name;
				}
			} else if (arg("-n", "--key-name")) {
				char *namestr = argv[++i];
				char *end = namestr + strlen(namestr);
				long int name = strtol(namestr, &end, 16);
				if (*end == '\0' && name >= 0 && name < 0x100) {
					context.key_name = (uint8_t)name;
				} else {
					fprintf(stderr, "Invalid key name specified. Must be between 0x00-0xFF, inclusive.\n");
					exit(1);
				}
			} else if (arg("-s", "--signature")) {
				context.sigfile = argv[++i];
			} else if (arg("-v", "--version")) {
				const char *errstr = "Version numbers must be in format \"major.minor\".\n";
				char *majorstr = argv[++i];
				char *minorstr = strstr(majorstr, ".");
				if (minorstr == NULL) {
					fprintf(stderr, errstr);
					exit(1);
				}
				minorstr++;
				long int major, minor;
				char *end = majorstr;
				major = strtol(majorstr, &end, 10);
				if (*end != '.') {
					fprintf(stderr, errstr);
					exit(1);
				}
				minor = strtol(minorstr, &end, 10);
				if (*end != '\0') {
					fprintf(stderr, errstr);
					exit(1);
				}
				if (major < 0 || major >= 0x100 || minor < 0 || minor >= 0x100) {
					fprintf(stderr, "Version numbers must be between 0 and 255, inclusive.\n");
					exit(1);
				}
				context.major_version = (uint8_t)major;
				context.minor_version = (uint8_t)minor;
			} else if (arg("-r", "--hw-revision")) {
				char *str = argv[++i];
				char *end = str;
				long int rev = strtol(str, &end, 10);
				if (*end != '\0') {
					fprintf(stderr, "Hardware revision must be between 0 and 255, inclusive.\n");
					exit(1);
				}
				context.hardware_revision = (uint8_t)rev;
			} else if (arg("-d", "--device")) {
				char *device = argv[++i];
				if (!parse_device(device, &context.device_type)) {
					fprintf(stderr, "Invalid device: %s\n", device);
					exit(1);
				}
			} else if (arg("-p", "--print")) {
				context.print_info = 1;
			}
		} else {
			if (context.infile == NULL) {
				context.infile = argv[i];
			} else if (context.outfile == NULL) {
				context.outfile = argv[i];
			} else {
				char *end = argv[i];
				long int page = strtol(argv[i], &end, 16);
				if (*end != '\0') {
					fprintf(stderr, "Error: %s is not a valid page number.", argv[i]);
					exit(1);
				}
				if (page < 0 || page >= 0x100) {
					fprintf(stderr, "Error: %s is outside the acceptable range for pages.", argv[i]);
					exit(1);
				}
				context.pages[page] = 1;
			}
		}
	}

	if (context.infile == NULL) {
		fprintf(stderr, "Error: no input file specified.\n");
		exit(1);
	}
	if (context.outfile == NULL) {
		fprintf(stderr, "Error: no output file specified.\n");
		exit(1);
	}

	FILE *rom = fopen(context.infile, "r");
	if (rom == NULL) {
		fprintf(stderr, "Error: unable to open input file.\n");
		exit(1);
	}
	FILE *output = fopen(context.outfile, "w");
	if (rom == NULL) {
		fprintf(stderr, "Error: unable to open output file.\n");
		exit(1);
	}

	/* Update pages to include the first page of each section */
	int page_count = 0;
	for (i = 0; i < 0x100; i++) {
		if (context.pages[i]) {
			page_count++;
			if (!context.pages[i & ~3]) {
				page_count++;
				context.pages[i & ~3] = 2;
			}
		}
	}

	if (page_count == 0) {
		fprintf(stderr, "Warning: no pages specified");
	}

	if (context.device_type == 0xFF) {
		context.device_type = detect_device(rom);
		if (context.device_type == 0xFF) {
			fprintf(stderr, "Unable to detect device type from ROM, use --device to set manually.\n");
			exit(1);
		}
	}

	if (context.print_info) {
		printf(
			"Upgrade info:\n"
			"- Version: %d.%d\n"
			"- Key: %02X\n"
			"- Hardware revision: %d\n"
			"- Device type: %s\n"
			"- Pages: ",
			context.major_version,
			context.minor_version,
			context.key_name,
			context.hardware_revision,
			device_type_str(context.device_type)
		);
		for (i = 0; i < 0x100; i++) {
			if (context.pages[i]) {
				printf("%02X ", i);
			}
		}
		printf("\n");
		if (context.keyfile) {
			printf("- Key file: %s", context.keyfile);
		}
	}

	fclose(rom);
	fclose(output);
	return 0;
}