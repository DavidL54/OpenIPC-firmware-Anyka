#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct mdq_block {
	const unsigned char *data;
	int size;
};

typedef int (*mdq_push_fn)(int queue, const struct mdq_block *blocks,
	int block_count, const void *info);

static mdq_push_fn real_mdq_push;
static int log_fd = -1;
static unsigned int logged_frames[2];
static unsigned int patched_slots;

int gv_mdq_push(int queue, const struct mdq_block *blocks, int block_count,
	const void *info);

static uintptr_t runtime_address(uintptr_t base, uintptr_t value)
{
	return value < base ? base + value : value;
}

static int patch_mdq_relocations(struct dl_phdr_info *object, size_t size,
	void *data)
{
	const Elf32_Phdr *program_header;
	const Elf32_Dyn *dynamic = NULL;
	const Elf32_Rel *relocations = NULL;
	const Elf32_Sym *symbols = NULL;
	const char *strings = NULL;
	uintptr_t base = object->dlpi_addr;
	unsigned int relocation_count = 0;
	unsigned int index;

	(void)size;
	(void)data;
	if (object->dlpi_name != NULL && strstr(object->dlpi_name, "mdq-tap.so"))
		return 0;

	for (index = 0; index < object->dlpi_phnum; index++) {
		program_header = &object->dlpi_phdr[index];
		if (program_header->p_type == PT_DYNAMIC) {
			dynamic = (const Elf32_Dyn *)(base + program_header->p_vaddr);
			break;
		}
	}
	if (dynamic == NULL)
		return 0;

	for (; dynamic->d_tag != DT_NULL; dynamic++) {
		switch (dynamic->d_tag) {
		case DT_JMPREL:
			relocations = (const Elf32_Rel *)runtime_address(base,
				dynamic->d_un.d_ptr);
			break;
		case DT_PLTRELSZ:
			relocation_count = dynamic->d_un.d_val / sizeof(*relocations);
			break;
		case DT_SYMTAB:
			symbols = (const Elf32_Sym *)runtime_address(base,
				dynamic->d_un.d_ptr);
			break;
		case DT_STRTAB:
			strings = (const char *)runtime_address(base, dynamic->d_un.d_ptr);
			break;
		}
	}
	if (relocations == NULL || symbols == NULL || strings == NULL)
		return 0;

	for (index = 0; index < relocation_count; index++) {
		const Elf32_Rel *relocation = &relocations[index];
		const Elf32_Sym *symbol = &symbols[ELF32_R_SYM(relocation->r_info)];

		if (strcmp(strings + symbol->st_name, "gv_mdq_push") == 0) {
			void **slot = (void **)(base + relocation->r_offset);

			*slot = (void *)gv_mdq_push;
			patched_slots++;
		}
	}

	return 0;
}

__attribute__((constructor))
static void mdq_tap_init(void)
{
	void *core_handle;
	char line[128];
	int length;

	log_fd = open("/tmp/mdq-tap.log", O_WRONLY | O_CREAT | O_TRUNC | O_APPEND,
		0644);
	core_handle = dlopen("libgv_core.so", RTLD_NOW);
	real_mdq_push = core_handle != NULL ?
		(mdq_push_fn)dlsym(core_handle, "gv_mdq_push") : NULL;
	if (real_mdq_push != NULL)
		dl_iterate_phdr(patch_mdq_relocations, NULL);

	length = snprintf(line, sizeof(line), "init real=%p patched=%u\n",
		(void *)real_mdq_push, patched_slots);
	if (log_fd >= 0 && length > 0)
		write(log_fd, line, length < (int)sizeof(line) ? length : sizeof(line));
}

int gv_mdq_push(int queue, const struct mdq_block *blocks, int block_count,
	const void *info)
{
	int result;

	if (real_mdq_push == NULL)
		return -1;

	result = real_mdq_push(queue, blocks, block_count, info);

	if (log_fd >= 0 && queue >= 0 && queue < 2 &&
		logged_frames[queue] < 64 && blocks != NULL && block_count > 0 &&
		info != NULL) {
		const uint32_t *words = info;
		const unsigned char *data = blocks[0].data;
		int data_size = blocks[0].size;
		char line[256];
		int length;

		length = snprintf(line, sizeof(line),
			"q=%d n=%u blocks=%d size=%d info=%08x,%08x,%08x,%08x "
			"head=%02x%02x%02x%02x%02x%02x%02x%02x ret=%d\n",
			queue, logged_frames[queue], block_count, data_size,
			words[0], words[1], words[2], words[3],
			data_size > 0 ? data[0] : 0,
			data_size > 1 ? data[1] : 0,
			data_size > 2 ? data[2] : 0,
			data_size > 3 ? data[3] : 0,
			data_size > 4 ? data[4] : 0,
			data_size > 5 ? data[5] : 0,
			data_size > 6 ? data[6] : 0,
			data_size > 7 ? data[7] : 0, result);
		if (length > 0)
			write(log_fd, line, length < (int)sizeof(line) ? length :
				sizeof(line));
		logged_frames[queue]++;
	}

	return result;
}