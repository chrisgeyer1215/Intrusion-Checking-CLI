#include "common.h"
#include "../config.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <inotifytools/inotifytools.h>

namespace {

constexpr size_t kMaxPathLength = 4096;
constexpr size_t kInitialListCapacity = 1024;

void free_path_list(char const** paths) {
	if (!paths)
		return;

	for (size_t i = 0; paths[i]; ++i)
		free(const_cast<char*>(paths[i]));

	free(paths);
}

bool append_path(char const*** paths,
		 size_t* count,
		 size_t* capacity,
		 const char* path) {
	if (*count + 1 >= *capacity) {
		const size_t new_capacity = *capacity * 2;
		if (new_capacity <= *capacity) {
			errno = ENOMEM;
			return false;
		}

		auto resized = static_cast<char const**>(
		    realloc(*paths, sizeof(char*) * new_capacity));
		if (!resized)
			return false;

		*paths = resized;
		*capacity = new_capacity;
	}

	char* copy = strdup(path);
	if (!copy)
		return false;

	(*paths)[(*count)++] = copy;
	(*paths)[*count] = nullptr;
	return true;
}

void trim_line_ending(char* line) {
	size_t length = strlen(line);
	while (length > 0 &&
	       (line[length - 1] == '\n' || line[length - 1] == '\r')) {
		line[--length] = '\0';
	}
}

struct InputFile {
	FILE* stream = nullptr;
	bool is_stdin = false;

	InputFile() = default;
	InputFile(const InputFile&) = delete;
	InputFile& operator=(const InputFile&) = delete;

	~InputFile() {
		if (stream)
			fclose(stream);
	}

	bool open(const char* filename) {
		stream = fopen(filename, "r");
		return stream != nullptr;
	}

	FILE* get() const {
		return is_stdin ? stdin : stream;
	}
};

} // namespace

void print_event_descriptions() {
	printf(
	    "\taccess\t\tfile or directory contents were read\n"
	    "\tmodify\t\tfile or directory contents were written\n"
	    "\tattrib\t\tfile or directory attributes changed\n"
	    "\tclose_write\tfile or directory closed, after being opened in\n"
	    "\t           \twritable mode\n"
	    "\tclose_nowrite\tfile or directory closed, after being opened in\n"
	    "\t           \tread-only mode\n"
	    "\tclose\t\tfile or directory closed, regardless of read/write "
	    "mode\n"
	    "\topen\t\tfile or directory opened\n"
	    "\tmoved_to\tfile or directory moved to watched directory\n"
	    "\tmoved_from\tfile or directory moved from watched directory\n"
	    "\tmove\t\tfile or directory moved to or from watched directory\n"
	    "\tmove_self\t\tA watched file or directory was moved.\n"
	    "\tcreate\t\tfile or directory created within watched directory\n"
	    "\tdelete\t\tfile or directory deleted within watched directory\n"
	    "\tdelete_self\tfile or directory was deleted\n"
	    "\tunmount\t\tfile system containing file or directory "
	    "unmounted\n");
}

int isdir(char const* path) {
	struct stat path_stat;

	if (-1 == lstat(path, &path_stat)) {
		if (errno == ENOENT)
			return 0;
		fprintf(stderr, "Stat failed on %s: %s\n", path,
			strerror(errno));
		return 0;
	}

	return S_ISDIR(path_stat.st_mode) && !S_ISLNK(path_stat.st_mode);
}

FileList::FileList(int argc, char** argv)
    : watch_files_(nullptr), exclude_files_(nullptr), argc_(argc), argv_(argv) {}

FileList::~FileList() {
	free_path_list(watch_files_);
	free_path_list(exclude_files_);
}

bool construct_path_list(int argc,
			 char** argv,
			 char const* filename,
			 FileList* list) {
	InputFile input;

	if (filename) {
		if (filename[0] == '-' && !filename[1])
			input.is_stdin = true;
		else if (!input.open(filename)) {
			fprintf(stderr, "Couldn't open %s: %s\n", filename,
				strerror(errno));
			return false;
		}
	}

	size_t watch_count = 0;
	size_t watch_capacity = kInitialListCapacity;
	size_t exclude_count = 0;
	size_t exclude_capacity = kInitialListCapacity;
	list->watch_files_ = static_cast<char const**>(
	    calloc(watch_capacity, sizeof(char*)));
	list->exclude_files_ = static_cast<char const**>(
	    calloc(exclude_capacity, sizeof(char*)));
	if (!list->watch_files_ || !list->exclude_files_) {
		fprintf(stderr, "Couldn't allocate memory for path lists.\n");
		return false;
	}

	char name[kMaxPathLength];
	while (input.get() && fgets(name, sizeof(name), input.get())) {
		trim_line_ending(name);
		const size_t length = strlen(name);

		if (length == 0 || (name[0] == '@' && length == 1))
			continue;

		const bool is_exclude = name[0] == '@';
		const char* path = is_exclude ? &name[1] : name;
		if (!append_path(is_exclude ? &list->exclude_files_
					    : &list->watch_files_,
			 is_exclude ? &exclude_count : &watch_count,
			 is_exclude ? &exclude_capacity : &watch_capacity,
			 path)) {
			fprintf(stderr, "Couldn't allocate memory for path lists.\n");
			return false;
		}
	}

	for (int i = 0; i < argc; ++i) {
		const size_t length = strlen(argv[i]);
		if (length == 0 || (argv[i][0] == '@' && length == 1))
			continue;

		const bool is_exclude = argv[i][0] == '@';
		const char* path = is_exclude ? &argv[i][1] : argv[i];
		if (!append_path(is_exclude ? &list->exclude_files_
					    : &list->watch_files_,
			 is_exclude ? &exclude_count : &watch_count,
			 is_exclude ? &exclude_capacity : &watch_capacity,
			 path)) {
			fprintf(stderr, "Couldn't allocate memory for path lists.\n");
			return false;
		}
	}

	return true;
}

void warn_inotify_init_error(int fanotify) {
	const char* backend = fanotify ? "fanotify" : "inotify";
	const char* resource = fanotify ? "groups" : "instances";
	int error = inotifytools_error();

	fprintf(stderr, "Couldn't initialize %s: %s\n", backend,
		strerror(error));
	if (error == EMFILE) {
		fprintf(stderr,
			"Try increasing the value of "
			"/proc/sys/fs/%s/max_user_%s\n",
			backend, resource);
	}
	if (fanotify && error == EINVAL) {
		fprintf(stderr,
			"fanotify support for reporting the events with "
			"file names was added in kernel v5.9.\n");
	}
	if (fanotify && error == EPERM) {
		fprintf(stderr, "fanotify watch requires admin privileges\n");
	}
}

bool is_timeout_option_valid(long* timeout, const char* option) {
	if ((option == nullptr) || (*option == '\0')) {
		fprintf(stderr,
			"The provided value is not a valid timeout value.\n"
			"Please specify a long int value.\n");
		return false;
	}

	char* timeout_end = nullptr;
	errno = 0;
	*timeout = strtol(option, &timeout_end, 10);

	if (errno) {
		fprintf(stderr,
			"Something went wrong with the timeout "
			"value you provided.\n");
		fprintf(stderr, "%s\n", strerror(errno));
		return false;
	}

	if (*timeout_end != '\0') {
		fprintf(stderr,
			"'%s' is not a valid timeout value.\n"
			"Please specify a long int value.\n",
			option);
		return false;
	}

	return true;
}
