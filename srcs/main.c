#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <elf.h>
#include <sys/stat.h>
#include <errno.h>

typedef struct s_elffile
{
	unsigned char *content;
	size_t size;
	Elf64_Ehdr *header;
	Elf64_Shdr *section_header_table;
	Elf64_Phdr *program_header_table;
	Elf64_Shdr *shstrtab_section_header;
} t_elffile;

#define ENTRY_DELTA_PAYLOAD_OFFSET 2  				// offset in the payload where the entry delta is stored
#define ENCRYPTION_KEY_PAYLOAD_OFFSET 2+8 			// offset in the payload where the encryption key is stored
#define TEXT_SECTION_SIZE_PAYLOAD_OFFSET 2+8+8 		// offset in the payload where the text section size is stored
#define TEXT_SECTION_DELTA_PAYLOAD_OFFSET 2+8+8+8 	// offset in the payload where the text section delta is stored


unsigned char payload_bin[] = {
	// 0xe9, 0xaa => payload
	// 0xeb, 0x20 => decrypt
  0xeb, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x53,
  0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x56, 0x57, 0x41, 0x56,
  0x41, 0x57, 0x48, 0x8b, 0x05, 0xd9, 0xff, 0xff, 0xff, 0x4c, 0x8b, 0x05,
  0xda, 0xff, 0xff, 0xff, 0x4c, 0x8d, 0x0d, 0xb9, 0xff, 0xff, 0xff, 0x4d,
  0x01, 0xc8, 0x4c, 0x89, 0xc3, 0x48, 0x8b, 0x0d, 0xb6, 0xff, 0xff, 0xff,
  0x48, 0xc7, 0xc2, 0x00, 0x00, 0x00, 0x00, 0x50, 0x53, 0x51, 0x52, 0x49,
  0x89, 0xde, 0x49, 0xc7, 0xc1, 0xff, 0x0f, 0x00, 0x00, 0x49, 0xf7, 0xd1,
  0x4d, 0x21, 0xce, 0x49, 0x89, 0xc7, 0x49, 0x81, 0xc7, 0xff, 0x0f, 0x00,
  0x00, 0x4d, 0x21, 0xcf, 0x4c, 0x89, 0xf7, 0x4c, 0x89, 0xfe, 0x48, 0xc7,
  0xc2, 0x07, 0x00, 0x00, 0x00, 0x48, 0xc7, 0xc0, 0x0a, 0x00, 0x00, 0x00,
  0x0f, 0x05, 0x5a, 0x59, 0x5b, 0x58, 0x48, 0x39, 0xc2, 0x7d, 0x18, 0x48,
  0x89, 0xdf, 0x48, 0x01, 0xd7, 0x48, 0x0f, 0xb6, 0x37, 0x49, 0x89, 0xc8,
  0x4c, 0x31, 0xc6, 0x40, 0x88, 0x37, 0x48, 0xff, 0xc2, 0xeb, 0xe3, 0x4c,
  0x89, 0xf7, 0x4c, 0x89, 0xfe, 0x48, 0xc7, 0xc2, 0x05, 0x00, 0x00, 0x00,
  0x48, 0xc7, 0xc0, 0x0a, 0x00, 0x00, 0x00, 0x0f, 0x05, 0x41, 0x5f, 0x41,
  0x5e, 0x5f, 0x5e, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x5a, 0x59, 0x5b,
  0x58, 0xeb, 0x0e, 0x2e, 0x2e, 0x2e, 0x2e, 0x57, 0x4f, 0x4f, 0x44, 0x59,
  0x2e, 0x2e, 0x2e, 0x2e, 0x0a, 0x57, 0x56, 0x52, 0x50, 0x48, 0xc7, 0xc7,
  0x01, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x35, 0xe0, 0xff, 0xff, 0xff, 0x48,
  0xc7, 0xc2, 0x0e, 0x00, 0x00, 0x00, 0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00,
  0x00, 0x0f, 0x05, 0x48, 0x8b, 0x05, 0xf0, 0xfe, 0xff, 0xff, 0x4c, 0x8d,
  0x15, 0xe7, 0xfe, 0xff, 0xff, 0x49, 0x29, 0xc2, 0x58, 0x5a, 0x5e, 0x5f,
  0x41, 0xff, 0xe2
};
uint64_t payload_size = 291;

unsigned char encryption_key = 0x42;

// https://tmpout.sh/1/2.html
// https://www.symbolcrash.com/2019/03/27/pt_note-to-pt_load-injection-in-elf/
// https://github.com/zznop/drow

void encrypt(void *addr, size_t size, unsigned char key)
{
	// simple XOR encryption

	for (size_t i = 0; i < size; i++)
	{
		((unsigned char *)addr)[i] ^= key;
	}
}

Elf64_Shdr *find_section_by_name(t_elffile *elffile, const char *section_name)
{
	for (unsigned int i = 0; i < elffile->header->e_shnum; i++)
	{
		if (strcmp((char *)(elffile->content + elffile->shstrtab_section_header->sh_offset + elffile->section_header_table[i].sh_name), section_name) == 0)
			return &(elffile->section_header_table[i]);
	}
	return NULL;
}

mode_t get_file_mode(const char *filename)
{
	struct stat sb;
	if (stat(filename, &sb) == -1)
	{
		perror("stat");
		return 0;
	}
	return sb.st_mode;
}

int encrypt_section(t_elffile *elffile, const char *section_name, unsigned char key)
{
	Elf64_Shdr *section = find_section_by_name(elffile, section_name);
	if (section == NULL)
	{
		fprintf(stderr, "Could not find section %s\n", section_name);
		return 1;
	}
	encrypt(elffile->content + section->sh_offset, section->sh_size, key);
	return 0;
}

// set some data into a payload at a specific offset
// data_size is the size in bytes of the data to inject (eg uint64_t = 8 bytes)
int set_payload_data(unsigned char *payload, size_t payload_size, void *data, size_t data_size, size_t offset)
{
	if (offset + data_size > payload_size)
	{
		fprintf(stderr, "Cannot set data at offset %ld, not enough space in the payload\n", offset);
		return 1;
	}
	memcpy(payload + offset, data, data_size);
	return 0;
}


void usage()
{
	fprintf(stderr, "Usage: woody_woodpacker [source_exec]\n");
}

int is_elf_file(unsigned char *file_content, size_t file_size)
{
    if (file_size < 4)
        return 0;
    // https://en.wikipedia.org/wiki/Executable_and_Linkable_Format
    // 0x00	4	e_ident[EI_MAG0] through e_ident[EI_MAG3]	0x7F followed by ELF(45 4c 46) in ASCII; these four bytes constitute the magic number.
	if (file_content[EI_MAG0] == ELFMAG0 && file_content[EI_MAG1] == ELFMAG1 && file_content[EI_MAG2] == ELFMAG2 && file_content[EI_MAG3] == ELFMAG3)
        return 1;
    return 0;
}

// Opens and parses an ELF file, returns 1 on error
int parse_elf_file(const char *filename, t_elffile *elffile)
{
	memset(elffile, 0, sizeof(t_elffile));

	int fd = open(filename, O_RDONLY);
	if (fd == -1)
	{
		fprintf(stderr, "woodpacker: cannot open file %s: %s\n", filename, strerror(errno));
		return 1;
	}

	elffile->size = lseek(fd, 0, SEEK_END);
	if (elffile->size == -1)
	{
		perror("woodpacker: lseek END error");
		return 1;
	}
	
	// malloc instead of mmap in case we want to call woody on woody
	elffile->content = malloc(elffile->size);
	if (elffile->content == NULL)
	{
		perror("woodpacker: malloc error");
		return 1;
	}

	if (lseek(fd, 0, SEEK_SET) == -1)
	{
		perror("woodpacker: lseek SET error");
		return 1;
	}

	if (read(fd, elffile->content, elffile->size) == -1)
	{
		fprintf(stderr, "woodpacker: cannot read file %s: %s\n", filename, strerror(errno));
		return 1;
	}

	if (close(fd == -1))
	{
		perror("woodpacker: close error");
		return 1;
	}

	if (elffile->size < EI_NIDENT) // If we cannot get identification infos (magic number, architecture etc.)
	{
		fprintf(stderr, "woodpacker: file %s is too small to be an ELF file\n", filename);
		return 1;
	}

	if (!is_elf_file(elffile->content, elffile->size))
	{
		fprintf(stderr, "woodpacker: file %s is not an ELF file\n", filename);
		return 1;
	}

	if (elffile->content[EI_VERSION] != EV_CURRENT)
	{
		fprintf(stderr, "woodpacker: file %s has invalid ELF version\n", filename);
		return 1;
	}

	if (elffile->content[EI_CLASS] != ELFCLASS64)
	{
		fprintf(stderr, "woodpacker: file %s is not a 64-bit ELF file\n", filename);
		return 1;
	}

	if (elffile->size < sizeof(Elf64_Ehdr))
	{
		fprintf(stderr, "woodpacker: file %s is truncated\n", filename);
		return 1;
	}

	elffile->header = (Elf64_Ehdr *)elffile->content;

	// check if section header table is within the file (header section start + header section size * header section entries count)
	if (elffile->content + elffile->header->e_shoff + (elffile->header->e_shentsize * elffile->header->e_shnum) > elffile->content + elffile->size)
	{
		fprintf(stderr, "woodpacker: invalid section header table\n");
		return 1;
	}

	// if shstrtab section index is invalid
	if (elffile->header->e_shstrndx >= elffile->header->e_shnum)
	{
		fprintf(stderr, "woodpacker: invalid shstrtab section index\n");
		return 1;
	}

	elffile->section_header_table = (Elf64_Shdr *)(elffile->content + elffile->header->e_shoff);
	elffile->shstrtab_section_header = elffile->section_header_table + elffile->header->e_shstrndx;
	elffile->program_header_table = (Elf64_Phdr *)(elffile->content + elffile->header->e_phoff);

	if (elffile->shstrtab_section_header->sh_type != SHT_STRTAB)
	{
		fprintf(stderr, "woodpacker: invalid shstrtab section type\n");
		return 1;
	}

	if (elffile->shstrtab_section_header->sh_offset + elffile->shstrtab_section_header->sh_size > elffile->size)
	{
		fprintf(stderr, "woodpacker: invalid shstrtab section size\n");
		return 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		usage();
		return EXIT_FAILURE;
	}

	t_elffile elffile = {0};

	if (parse_elf_file(argv[1], &elffile) != 0)
	{
		free(elffile.content);
		return EXIT_FAILURE;
	}

	unsigned char* payload_content = payload_bin;

	if (encrypt_section(&elffile, ".text", encryption_key) != 0)
	{
		free(elffile.content);
		return EXIT_FAILURE;
	}

	// inject decryption key into payload
	if (set_payload_data(payload_bin, payload_size, &encryption_key, sizeof(encryption_key), ENCRYPTION_KEY_PAYLOAD_OFFSET) != 0)
	{
		free(elffile.content);
		return EXIT_FAILURE;
	}

	Elf64_Shdr *text_section = find_section_by_name(&elffile, ".text");
	if (text_section == NULL)
	{
		fprintf(stderr, "Could not find .text section\n");
		free(elffile.content);
		return EXIT_FAILURE;
	}

	// inject text decrypt section size into payload
	if (set_payload_data(payload_bin, payload_size, &(text_section->sh_size), sizeof(text_section->sh_size), TEXT_SECTION_SIZE_PAYLOAD_OFFSET) != 0)
	{
		free(elffile.content);
		return EXIT_FAILURE;
	}

	// find segment where .text section is located
	Elf64_Phdr *text_segment = NULL;
	for (unsigned int i = 0; i < elffile.header->e_phnum; i++)
	{
		if (program_header_table[i].p_offset <= text_section->sh_offset && program_header_table[i].p_offset + program_header_table[i].p_filesz >= text_section->sh_offset + text_section->sh_size)
		{
			text_segment = &(program_header_table[i]);
			break;
		}
	}
	
	if (text_segment == NULL)
	{
		fprintf(stderr, "Could not find segment for .text section\n");
		free(elffile.content);
		return EXIT_FAILURE;
	}

	// printf("Encrypted .text section\n");
	
	int found_cave = 0;
	// Elf64_Shdr *section_header_table = (Elf64_Shdr *)(elffile.content + header->e_shoff);
	// We find an executable segment, find the last section in this segment, then try to expand it using the free padding space, try this for every executable segment
	for (unsigned int i = 0; i < elffile.header->e_phnum; i++)
	{
		if (found_cave)
			break;

		if (program_header_table[i].p_type == PT_LOAD && program_header_table[i].p_flags & PF_X)
		{
			printf("Exec segment from 0x%08lx to 0x%08lx (size: %ld bytes)\n", program_header_table[i].p_offset, program_header_table[i].p_offset + program_header_table[i].p_memsz, program_header_table[i].p_memsz);

			// list all sections in the segment
			uint64_t segment_start = program_header_table[i].p_offset;
			uint64_t segment_end = program_header_table[i].p_offset + program_header_table[i].p_memsz;


			for (unsigned int j = 0; j < elffile.header->e_shnum; j++)
			{
				uint64_t section_start = section_header_table[j].sh_offset;
				uint64_t section_end = section_header_table[j].sh_offset + section_header_table[j].sh_size;

				if (section_end == segment_end) {
					printf("Section %s at 0x%08lx (size: %ld bytes)\n", (char *)(elffile.content + shstrtab_section_header->sh_offset + section_header_table[j].sh_name), section_start, section_header_table[j].sh_size);
					
					// find next section start
					// TODO check if its last section of the file
					uint64_t next_section_start = section_header_table[j + 1].sh_offset;

					uint64_t available_space = next_section_start - section_end;
					if (available_space < payload_size)
					{
						printf("Not enough space for the payload\n");
						continue;
					}

					printf("Found %ld bytes of free space in the segment (payload size: %ld)\n", available_space, payload_size);
					if (payload_size > available_space)
					{
						printf("Payload is too big\n");
						continue;
					}

					void *payload_start = elffile.content + section_start + section_header_table[j].sh_size;
					uint64_t new_entry_point = section_start + section_header_table[j].sh_size;

					// now we expand the section and segment to fit the payload
					Elf64_Phdr *segment = &(program_header_table[i]);
					segment->p_memsz += payload_size;
					segment->p_filesz += payload_size;

					Elf64_Shdr *section = &(section_header_table[j]);
					section->sh_size += payload_size;

					// now modify entry point to point to the payload
					printf("Current entry point: 0x%08lx\n", elffile.header->e_entry);
					uint64_t old_entry_point = elffile.header->e_entry;
					header->e_entry = new_entry_point;
					printf("New entry point: 0x%08lx\n", new_entry_point);

					uint64_t entry_delta = new_entry_point - old_entry_point;
					printf("Entry point delta: 0x%lx\n", entry_delta);

					// inject the delta into the payload starting at the 3rd byte
					*(uint64_t *)(payload_content + 2) = entry_delta;


					// TODO TEMP set flags to RWX for testing (otherwise segfault on write decryption)
					// segment->p_flags = PF_R | PF_W | PF_X;


					// find the vaddr where the payload will be loaded
					uint64_t payload_vaddr = segment->p_vaddr + segment->p_memsz - payload_size; // vaddr where the payload will be loaded
					printf("Payload will be loaded at 0x%08lx\n", payload_vaddr);
					uint64_t text_section_vaddr = text_segment->p_vaddr + (text_section->sh_offset - text_segment->p_offset);
					printf("Text section is loaded at 0x%08lx\n", text_section_vaddr);
					printf("Delta: %lld\n", (long long)text_section_vaddr - (long long)payload_vaddr);

					*(int64_t *)(payload_content + 2+8+8+8) = (int64_t)text_section_vaddr - (int64_t)payload_vaddr; // offset

					// add a jump to the original entry point
					// E9 xx xx xx xx
					// *(uint8_t *)(payload_content + payload_size - 9) = 0xE9;
					// *(uint64_t *)(payload_content + payload_size - 8) = new_entry_point;
					
					// now insert the payload
					memcpy(payload_start, payload_content, payload_size);

					// TODO try to find a way to create codecave if there is no free space in the segment
					found_cave = 1;
				}
			}
			if (!found_cave)
			{
				printf("Segment did not fit in section or whatever\n");
				// possible due to PT_NOTE injection for instance, so now check if we can just expand the segment without expanding the section (eg if code is injected at the end of the file we should be able to expand the segment without expanding the section)

				// first check if its the highest virtual address PT_LOAD segment
				Elf64_Phdr *highest_vaddr_segment = NULL;
				for (unsigned int i = 0; i < elffile.header->e_phnum; i++)
				{
					if (program_header_table[i].p_type == PT_LOAD)
					{
						if (highest_vaddr_segment == NULL || program_header_table[i].p_vaddr > highest_vaddr_segment->p_vaddr)
							highest_vaddr_segment = &(program_header_table[i]);
					}
				}
				
				Elf64_Phdr *segment = &(program_header_table[i]);
				if (segment == highest_vaddr_segment)
				{
					printf("Segment is the highest virtual address PT_LOAD segment\n");
					// check if end of segment + payload_size overlaps other segment/section, if not we can expand

					uint64_t segment_end_offset = segment->p_offset + segment->p_memsz;
					uint64_t new_segment_end_offset = segment_end + payload_size;

					int overlaps = 0;
					// we use filesz and not memsz because we just want to see if we can expand in file. There might be overlap when loaded in memory but we don't care (since our code is executed first)
					// for instance with .bss section, which is probably loaded in same memory space as the segment but we don't care
					for (unsigned int j = 0; j < elffile.header->e_phnum; j++)
					{
						if (program_header_table[j].p_offset < new_segment_end_offset && program_header_table[j].p_offset + program_header_table[j].p_filesz > segment_end_offset)
						{
							Elf64_Phdr *overlapping_segment = &(program_header_table[j]);
							overlaps = 1;
							break;
						}
					}

					if (overlaps)
					{
						printf("Segment overlaps other segment, cannot expand\n");
						continue;
					}

					
					
					// simply expand the segment
					uint64_t old_entry_point = elffile.header->e_entry;
					uint64_t new_entry_point = segment->p_vaddr + segment->p_memsz;
					header->e_entry = new_entry_point;

					printf("Test: %08lx %08lx %08ld\n", old_entry_point, segment->p_vaddr, segment->p_memsz);

					int64_t entry_delta = (int64_t)new_entry_point - (int64_t)old_entry_point;
					printf("NEW Entry point delta: 0x%lx\n", entry_delta);

					segment->p_memsz += payload_size;
					segment->p_filesz += payload_size;

					// inject the delta into the payload starting at the 3rd byte
					*(int64_t *)(payload_content + 2) = entry_delta;

					elffile.content = realloc(elffile.content, elffile.size + payload_size); // expand the file to fit the payload
					memcpy(elffile.content + elffile.size, payload_content, payload_size);

					elffile.size += payload_size;
					printf("Successfully injected payload\n");
					printf("NEW New entry point: 0x%08lx\n", new_entry_point);
					found_cave = 1;
					break; // break cause header pointer is now invalid
				}
			}
		}
	}

	if (!found_cave)
	{
		// couldnt find already existing cave, so create one using PT_NOTE segment

		printf("didnt find a cave, try PT_NOTE inject\n");

		// find the highest virtual address in use
		uint64_t highest_vaddr = 0;
		for (unsigned int i = 0; i < elffile.header->e_phnum; i++)
		{
			if (program_header_table[i].p_vaddr + program_header_table[i].p_memsz > highest_vaddr)
			{
				highest_vaddr = program_header_table[i].p_vaddr + program_header_table[i].p_memsz;
			}
		}

		// now align it to page size
		uint64_t highest_vaddr_aligned = (highest_vaddr + 0xfff) & ~0xfff;
		printf("Highest virtual address: 0x%08lx\n", highest_vaddr_aligned);
	
		// find the PT_NOTE segment
		for (unsigned int i = 0; i < elffile.header->e_phnum; i++)
		{
			if (program_header_table[i].p_type == PT_NOTE)
			{
				printf("Found PT_NOTE segment\n");
				
				Elf64_Phdr *segment = &(program_header_table[i]);
				segment->p_type = PT_LOAD;
				segment->p_flags = PF_R | PF_X;
				// set high address to avoid collision
				segment->p_vaddr = highest_vaddr_aligned + elffile.size; // why is this + elffile.size mandatory ?? prob for alignment but why ?? (otherwise segfault)
				
				// remove all previous content (don't do += payload_size) otherwise if we later try to expand this segment it will segfault (why tho ?)
				segment->p_filesz = payload_size;
				segment->p_memsz = payload_size;

				segment->p_offset = elffile.size; // append the payload to the end of the file

				uint64_t old_entry_point = header->e_entry;
				uint64_t new_entry_point = segment->p_vaddr;
				header->e_entry = new_entry_point;

				uint64_t entry_delta = new_entry_point - old_entry_point;
				printf("Entry point delta: 0x%lx\n", entry_delta);

				// inject the delta into the payload starting at the 3rd byte
				*(uint64_t *)(payload_content + 2) = entry_delta;


				elffile.content = realloc(elffile.content, elffile.size + payload_size); // expand the file to fit the payload
				memcpy(elffile.content + elffile.size, payload_content, payload_size);

				elffile.size += payload_size;
				printf("Successfully injected payload\n");
				printf("New entry point: 0x%08lx\n", new_entry_point);

				found_cave = 1;
				break;
			}
		}
	}

	if (!found_cave)
	{
		fprintf(stderr, "Could not find a suitable cave\n");
		free(elffile.content);
		return EXIT_FAILURE;
	}



	// write the result to a new file
	FILE *new_file = fopen("woody", "w");
	if (new_file == NULL)
	{
		perror("error on fopen");
		free(elffile.content);
		return EXIT_FAILURE;
	}


	mode_t original_mode = get_file_mode(argv[1]);
	chmod("woody", original_mode);

	if (fwrite(elffile.content, 1, elffile.size, new_file) != elffile.size)
	{
		perror("error on fwrite");
		fclose(new_file);
		free(elffile.content);
		return EXIT_FAILURE;
	}
	

	free(elffile.content);
}