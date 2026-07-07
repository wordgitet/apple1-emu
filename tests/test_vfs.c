#include "../bus.h"
#include "../io.h"
#include "../port.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * test_vfs.c -- Verify port_vfs swap: core code must use g_port_vfs, not
 * fopen/fread directly.  Installs an in-memory RAM VFS and exercises both
 * the vtable and bus_load_wozmon_txt().
 */

#define MOCK_PATH_WOZ  "ram:wozmon.txt"
#define MOCK_PATH_BIN  "ram:prog.bin"
#define MOCK_MAX_DATA  512
#define MOCK_MAX_FILES 4

struct mock_slot {
	char path[48];
	char data[MOCK_MAX_DATA];
	long size;
	int used;
};

struct mock_handle {
	int slot;
	long pos;
};

static struct mock_slot mock_files[MOCK_MAX_FILES];
static struct mock_handle mock_handles[MOCK_MAX_FILES];
static int mock_handle_used[MOCK_MAX_FILES];
static struct port_vfs *saved_vfs;

static int
mock_find_slot(const char *path)
{
	int i;

	for (i = 0; i < MOCK_MAX_FILES; i++) {
		if (mock_files[i].used != 0 &&
		    port_strcmp(mock_files[i].path, path) == 0) {
			return (i);
		}
	}
	return (-1);
}

static int
mock_alloc_slot(const char *path)
{
	int i;

	i = mock_find_slot(path);
	if (i >= 0) {
		return (i);
	}
	for (i = 0; i < MOCK_MAX_FILES; i++) {
		if (mock_files[i].used == 0) {
			mock_files[i].used = 1;
			port_strncpy(mock_files[i].path,
			    path,
			    sizeof(mock_files[i].path));
			mock_files[i].size = 0;
			return (i);
		}
	}
	return (-1);
}

static int
mock_alloc_handle(int slot)
{
	int i;

	for (i = 0; i < MOCK_MAX_FILES; i++) {
		if (mock_handle_used[i] == 0) {
			mock_handle_used[i] = 1;
			mock_handles[i].slot = slot;
			mock_handles[i].pos = 0;
			return (i);
		}
	}
	return (-1);
}

static void
mock_install_file(const char *path, const char *content)
{
	int slot;
	port_size_t len;

	slot = mock_alloc_slot(path);
	assert(slot >= 0);
	len = port_strlen(content);
	assert(len < MOCK_MAX_DATA);
	port_memcpy(mock_files[slot].data, content, len);
	mock_files[slot].size = (long)len;
}

static port_file_t
mock_vfs_open(const char *path, int flags)
{
	int slot;
	int h;

	(void)flags;
	if (path == NULL) {
		return (PORT_FILE_INVALID);
	}
	if (flags == PORT_VFS_WRITE) {
		slot = mock_alloc_slot(path);
		if (slot < 0) {
			return (PORT_FILE_INVALID);
		}
	} else {
		slot = mock_find_slot(path);
		if (slot < 0) {
			return (PORT_FILE_INVALID);
		}
	}
	h = mock_alloc_handle(slot);
	if (h < 0) {
		return (PORT_FILE_INVALID);
	}
	return ((port_file_t)&mock_handles[h]);
}

static void
mock_vfs_close(port_file_t f)
{
	struct mock_handle *mh;
	int idx;

	if (f == PORT_FILE_INVALID) {
		return;
	}
	mh = (struct mock_handle *)f;
	idx = (int)(mh - mock_handles);
	if (idx >= 0 && idx < MOCK_MAX_FILES) {
		mock_handle_used[idx] = 0;
	}
}

static int
mock_vfs_read(port_file_t f, void *buf, port_size_t sz, port_size_t *nread)
{
	struct mock_handle *mh;
	struct mock_slot *slot;
	long avail;
	port_size_t got;

	if (f == PORT_FILE_INVALID || buf == NULL) {
		return (-1);
	}
	mh = (struct mock_handle *)f;
	slot = &mock_files[mh->slot];
	avail = slot->size - mh->pos;
	if (avail < 0) {
		avail = 0;
	}
	got = (port_size_t)avail;
	if (got > sz) {
		got = sz;
	}
	if (got > 0) {
		port_memcpy(buf, slot->data + mh->pos, got);
		mh->pos += (long)got;
	}
	if (nread != NULL) {
		*nread = got;
	}
	return (0);
}

static int
mock_vfs_size(port_file_t f, port_size_t *size)
{
	struct mock_handle *mh;

	if (f == PORT_FILE_INVALID || size == NULL) {
		return (-1);
	}
	mh = (struct mock_handle *)f;
	*size = (port_size_t)mock_files[mh->slot].size;
	return (0);
}

static int
mock_vfs_seek(port_file_t f, int32_t offset, int whence)
{
	struct mock_handle *mh;
	long base;
	long next;

	if (f == PORT_FILE_INVALID) {
		return (-1);
	}
	mh = (struct mock_handle *)f;
	switch (whence) {
	case PORT_VFS_SEEK_SET:
		base = 0;
		break;
	case PORT_VFS_SEEK_CUR:
		base = mh->pos;
		break;
	case PORT_VFS_SEEK_END:
		base = mock_files[mh->slot].size;
		break;
	default:
		return (-1);
	}
	next = base + (long)offset;
	if (next < 0) {
		return (-1);
	}
	mh->pos = next;
	return (0);
}

static int
mock_vfs_write(port_file_t f,
    const void *buf,
    port_size_t sz,
    port_size_t *nwritten)
{
	struct mock_handle *mh;
	struct mock_slot *slot;
	long room;

	if (f == PORT_FILE_INVALID || buf == NULL) {
		return (-1);
	}
	mh = (struct mock_handle *)f;
	slot = &mock_files[mh->slot];
	if (mh->pos < 0) {
		return (-1);
	}
	room = MOCK_MAX_DATA - mh->pos;
	if ((long)sz > room) {
		return (-1);
	}
	port_memcpy(slot->data + mh->pos, buf, sz);
	mh->pos += (long)sz;
	if (mh->pos > slot->size) {
		slot->size = mh->pos;
	}
	if (nwritten != NULL) {
		*nwritten = sz;
	}
	return (0);
}

static int
mock_vfs_read_line(port_file_t f, char *buf, port_size_t size)
{
	struct mock_handle *mh;
	struct mock_slot *slot;
	port_size_t i;
	char c;

	if (f == PORT_FILE_INVALID || buf == NULL || size == 0) {
		return (0);
	}
	mh = (struct mock_handle *)f;
	slot = &mock_files[mh->slot];
	for (i = 0; i + 1 < size; i++) {
		if (mh->pos >= slot->size) {
			break;
		}
		c = slot->data[mh->pos++];
		buf[i] = c;
		if (c == '\n') {
			i++;
			break;
		}
	}
	if (i == 0) {
		return (0);
	}
	buf[i] = '\0';
	return (1);
}

static struct port_vfs mock_vfs = { 1,
	mock_vfs_open,
	mock_vfs_close,
	mock_vfs_read,
	mock_vfs_size,
	mock_vfs_seek,
	mock_vfs_write,
	mock_vfs_read_line };

static void
mock_vfs_reset(void)
{
	int i;

	for (i = 0; i < MOCK_MAX_FILES; i++) {
		mock_files[i].used = 0;
		mock_files[i].size = 0;
		mock_handle_used[i] = 0;
	}
}

static void
mock_vfs_install(void)
{
	saved_vfs = g_port_vfs;
	mock_vfs_reset();
	g_port_vfs = &mock_vfs;
}

static void
mock_vfs_restore(void)
{
	g_port_vfs = saved_vfs;
}

static void
test_vfs_vtable(void)
{
	port_file_t f;
	char buf[64];
	port_size_t nread;
	port_size_t fsize;
	port_size_t nwritten;
	char line[32];

	mock_install_file(MOCK_PATH_WOZ,
	    "0100: A9 42 00\n"
	    "0103R\n");
	mock_install_file(MOCK_PATH_BIN, "ABCD");

	f = port_vfs_default.open(MOCK_PATH_BIN, PORT_VFS_READ);
	assert(f != PORT_FILE_INVALID);
	assert(port_vfs_default.size(f, &fsize) == 0);
	assert(fsize == 4);

	nread = 0;
	assert(port_vfs_default.read(f, buf, 2, &nread) == 0);
	assert(nread == 2);
	assert(buf[0] == 'A' && buf[1] == 'B');

	assert(port_vfs_default.seek(f, 0, PORT_VFS_SEEK_SET) == 0);
	nread = 0;
	assert(port_vfs_default.read(f, buf, 4, &nread) == 0);
	assert(nread == 4);
	assert(buf[0] == 'A' && buf[3] == 'D');
	port_vfs_default.close(f);

	f = port_vfs_default.open("ram:new.txt", PORT_VFS_WRITE);
	assert(f != PORT_FILE_INVALID);
	nwritten = 0;
	assert(port_vfs_default.write(f, "OK", 2, &nwritten) == 0);
	assert(nwritten == 2);
	port_vfs_default.close(f);

	f = port_vfs_default.open("ram:new.txt", PORT_VFS_READ);
	assert(f != PORT_FILE_INVALID);
	assert(mock_vfs_read_line(f, line, sizeof(line)) == 1);
	assert(port_strcmp(line, "OK") == 0);
	port_vfs_default.close(f);
}

static void
test_bus_through_mock_vfs(void)
{
	struct bus bus;
	uint8_t ram[4096];
	uint16_t run_addr;
	bool has_run;
	port_result_t rc;

	mock_install_file(MOCK_PATH_WOZ,
	    "0200: A9 01 85 00\n"
	    "0204R\n");

	assert(bus_init(&bus, ram, sizeof(ram)) == PORT_OK);
	run_addr = 0;
	has_run = false;
	rc = bus_load_wozmon_txt(&bus, MOCK_PATH_WOZ, &run_addr, &has_run);
	assert(rc == PORT_OK);
	assert(has_run == true);
	assert(run_addr == 0x0204);
	assert(ram[0x0200] == 0xA9);
	assert(ram[0x0201] == 0x01);
	assert(ram[0x0202] == 0x85);
	assert(ram[0x0203] == 0x00);
	bus_free(&bus);
}

int
main(void)
{
	mock_vfs_install();
	test_vfs_vtable();
	test_bus_through_mock_vfs();
	mock_vfs_restore();

	printf("test_vfs passed.\n");
	return (0);
}
