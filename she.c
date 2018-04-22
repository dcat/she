#include <sys/types.h>
#include <sys/mman.h>
#include <termbox.h>
#include <string.h>
#include <locale.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <err.h>

#define PATH  argv[1]
#define FG TB_DEFAULT
#define BG TB_DEFAULT
#define PGSIZ 16

typedef struct {
	char *map;
	int d;
	off_t siz;
	off_t off;
	off_t csr;
} he_t;

static he_t he;

void
tb_print(char *str, int x, int y, int fg, int bg, int len) {
	int n = 0;

	while (*str) {
		uint32_t utf;

		str += tb_utf8_char_to_unicode(&utf, str);
		tb_change_cell(x, y, utf, fg, bg);
		x++;

		if (++n == len)
			return;
	}
}

void
tb_printf(int x, int y, int fg, int bg, const char *fmt, ...) {
	char buf[256];
	size_t n;

	va_list args;
	va_start(args, fmt);
	vsnprintf(buf,256,  fmt, args);
	va_end(args);

	n = strnlen(buf, 256);
	tb_print(buf, x, y, fg, bg, n);
}

void
redraw(void) {
	int y, x, z, j, i;

	tb_clear();

	for (y = 0, i = he.off; y < tb_height() - 1; y++) {
		/* line numbers */
		if (i < he.siz)
			tb_printf(0, y, FG, BG, "%08x", he.off + (y * 16));
		else
			tb_printf(0, y, 239, BG, "~");

		for (x = 0; x < 4; x++) {
			for (z = 0; z < 4; z++, i++) {
				/* hex chars */
				tb_printf(13 + (x * 12) + (z * 3), y,
					i < he.siz ? FG : 239, BG, "%02x",
					i > he.siz ? 0 :
					(unsigned char)he.map[i]);

				tb_printf(64 + i % 16, y, i >= he.siz ? 239
					: FG, BG, "%c", isprint(he.map[i])
					? he.map[i] : '.');

				if (he.csr == i) {
					tb_printf(13 + (x * 12) + (z * 3), y,
						16, 255, "%02x",
						(unsigned char)he.map[i]);
					tb_printf(64 + i % 16, y, 16, 255, "%c",
 						isprint(he.map[i]) ? he.map[i]
						: '.');
				}
			}
		}
	}


	tb_present();
}

void
cursor(uint16_t key) {
	switch (key) {
	case TB_KEY_ARROW_UP:
		if (he.csr >= 16) {
			if ((he.csr - he.off) / 16 == 0)
				he.off -= 16;

			he.csr -= 1 * 16;
		}
		break;
	case TB_KEY_ARROW_DOWN:
		if ((he.csr - he.off) / 16 == tb_height() - 2)
			he.off += 16;

		if (he.csr < he.siz - 16)
			he.csr += 16;
		break;
	case TB_KEY_ARROW_RIGHT:
		if ((he.csr - he.off) / 16 == tb_height() - 2)
			if ((he.csr % 16) == 15)
				he.off += 16;

		if (he.csr < he.siz - 1)
			he.csr += 1;
		break;
	case TB_KEY_ARROW_LEFT:
		if (he.csr > 0)
			he.csr -= 1;
		break;
	}
}

int
main(int argc, char **argv) {
	struct tb_event ev;
	int i;

	setlocale(LC_ALL, "");
	tb_init();
	tb_select_output_mode(TB_OUTPUT_256);

	/* check args */
	he.d = open(PATH, O_RDWR);
	if (he.d < 0) {
		warn("open");
		goto done;
	}

	he.siz = lseek(he.d, 0, SEEK_END);
	if (he.siz < 0)
		err(1, "lseek");

	if (he.siz < 1) {
		warn("no content");
		goto done;
	}

	he.map = mmap(0, he.siz, PROT_READ | PROT_WRITE, MAP_SHARED, he.d, 0);
	if (he.map == MAP_FAILED)
		err(1, "mmap");

	he.off = 0;
	he.csr = 0;
	redraw();

	while (tb_poll_event(&ev) > 0) {
		switch (ev.type) {
		case TB_EVENT_KEY:
			switch (ev.key) {
			case TB_KEY_CTRL_C:
				goto done;
				/* NOTREACHED */
			case TB_KEY_PGDN:
				for (i = 0; i < PGSIZ; i++)
					cursor(TB_KEY_ARROW_DOWN);
				break;
			case TB_KEY_PGUP:
				for (i = 0; i < PGSIZ; i++)
					cursor(TB_KEY_ARROW_UP);
				break;
			case TB_KEY_CTRL_L:
			case TB_KEY_ARROW_UP:
			case TB_KEY_ARROW_DOWN:
			case TB_KEY_ARROW_RIGHT:
			case TB_KEY_ARROW_LEFT:
				cursor(ev.key);
				break;
			default:
				switch (ev.ch) {
				case 'a': case 'A':
				case 'b': case 'B':
				case 'c': case 'C':
				case 'd': case 'D':
				case 'e': case 'E':
				case 'f': case 'F':
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9': {
					unsigned char s1, s2;
					char p[3] = { 0, 0, 0 };
					s1 = ev.ch;
					tb_printf(13 + (he.csr % 16) * 3,
						(he.csr - he.off) / 16,
						16, 255, "%c_", ev.ch);
					tb_present();
					if (tb_poll_event(&ev) != TB_EVENT_KEY)
						break;

					s2 = ev.ch;
					p[0] = s1;
					p[1] = s2;
					he.map[he.csr] =
						(unsigned char)strtoul(p, NULL, 16);
					msync(he.map, he.siz, MS_ASYNC);
					/* FALLTHROUGH */
				}
				default:
					redraw();
					break;
				}
			}
		case TB_EVENT_RESIZE:
			redraw();
		}
	}

done:
	munmap(he.map, he.siz);
	close(he.d);
	tb_shutdown();

	return 0;
}


