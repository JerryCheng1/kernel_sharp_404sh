/*
 * Earlyprintk support.
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/io.h>

#include <linux/amba/serial.h>
#include <linux/serial_reg.h>

#include <asm/fixmap.h>

static void __iomem *early_base;
static void (*printch)(char ch);

/*
 * PL011 single character TX.
 */
static void pl011_printch(char ch)
{
	while (readl_relaxed(early_base + UART01x_FR) & UART01x_FR_TXFF)
		;
	writeb_relaxed(ch, early_base + UART01x_DR);
	while (readl_relaxed(early_base + UART01x_FR) & UART01x_FR_BUSY)
		;
}

/*
 * Semihosting-based debug console
 */
static void smh_printch(char ch)
{
	asm volatile("mov  x1, %0\n"
		     "mov  x0, #3\n"
		     "hlt  0xf000\n"
		     : : "r" (&ch) : "x0", "x1", "memory");
}

/*
 * 8250/16550 (8-bit aligned registers) single character TX.
 */
static void uart8250_8bit_printch(char ch)
{
	while (!(readb_relaxed(early_base + UART_LSR) & UART_LSR_THRE))
		;
	writeb_relaxed(ch, early_base + UART_TX);
}

/*
 * 8250/16550 (32-bit aligned registers) single character TX.
 */
static void uart8250_32bit_printch(char ch)
{
	while (!(readl_relaxed(early_base + (UART_LSR << 2)) & UART_LSR_THRE))
		;
	writel_relaxed(ch, early_base + (UART_TX << 2));
}

#define MSM_HSL_UART_SR			0xa4
#define MSM_HSL_UART_ISR		0xb4
#define MSM_HSL_UART_TF			0x100
#define MSM_HSL_UART_CR			0xa8
#define MSM_HSL_UART_NCF_TX		0x40
#define MSM_HSL_UART_SR_TXEMT		BIT(3)
#define MSM_HSL_UART_ISR_TXREADY	BIT(7)

void msm_hsl_uart_printch(char ch)
{
/* SHARP_EXTEND SYS_ANA C162 MOD shrink printk Start */
#ifndef CONFIG_SHSYS_CUST
/* SHARP_EXTEND SYS_ANA C162 MOD shrink printk End */
	while (!(readl_relaxed(early_base + MSM_HSL_UART_SR) &
						       MSM_HSL_UART_SR_TXEMT) &&
	       !(readl_relaxed(early_base + MSM_HSL_UART_ISR) &
						     MSM_HSL_UART_ISR_TXREADY))
		;

	writel_relaxed(0x300, early_base + MSM_HSL_UART_CR);
	writel_relaxed(1, early_base + MSM_HSL_UART_NCF_TX);
	readl_relaxed(early_base + MSM_HSL_UART_NCF_TX);
	writel_relaxed(ch, early_base + MSM_HSL_UART_TF);
/* SHARP_EXTEND SYS_ANA C162 MOD shrink printk Start */
#endif	/* CONFIG_SHSYS_CUST */
/* SHARP_EXTEND SYS_ANA C162 MOD shrink printk End */
}

struct earlycon_match {
	const char *name;
	void (*printch)(char ch);
};

static const struct earlycon_match earlycon_match[] __initconst = {
	{ .name = "pl011", .printch = pl011_printch, },
	{ .name = "smh", .printch = smh_printch, },
	{ .name = "uart8250-8bit", .printch = uart8250_8bit_printch, },
	{ .name = "uart8250-32bit", .printch = uart8250_32bit_printch, },
	{ .name = "msm_hsl_uart", .printch = msm_hsl_uart_printch, },
	{}
};

static void early_write(struct console *con, const char *s, unsigned n)
{
	while (n-- > 0) {
		if (*s == '\n')
			printch('\r');
		printch(*s);
		s++;
	}
}

static struct console early_console_dev = {
	.name =		"earlycon",
	.write =	early_write,
	.flags =	CON_PRINTBUFFER | CON_BOOT,
	.index =	-1,
};

/*
 * Parse earlyprintk=... parameter in the format:
 *
 *   <name>[,<addr>][,<options>]
 *
 * and register the early console. It is assumed that the UART has been
 * initialised by the bootloader already.
 */
static int __init setup_early_printk(char *buf)
{
	const struct earlycon_match *match = earlycon_match;
	phys_addr_t paddr = 0;

	if (!buf) {
		pr_warning("No earlyprintk arguments passed.\n");
		return 0;
	}

	while (match->name) {
		size_t len = strlen(match->name);
		if (!strncmp(buf, match->name, len)) {
			buf += len;
			break;
		}
		match++;
	}
	if (!match->name) {
		pr_warning("Unknown earlyprintk arguments: %s\n", buf);
		return 0;
	}

	/* I/O address */
	if (!strncmp(buf, ",0x", 3)) {
		char *e;
		paddr = simple_strtoul(buf + 1, &e, 16);
		buf = e;
	}
	/* no options parsing yet */

	if (paddr)
		early_base = (void __iomem *)set_fixmap_offset_io(FIX_EARLYCON_MEM_BASE, paddr);

	printch = match->printch;
	early_console = &early_console_dev;
	register_console(&early_console_dev);

	return 0;
}

early_param("earlyprintk", setup_early_printk);
