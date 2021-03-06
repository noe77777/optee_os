// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2017, Linaro Limited
 */

#include <console.h>
#include <compiler.h>
#include <drivers/serial.h>
#include <kernel/generic_boot.h>
#include <kernel/panic.h>
#include <stdlib.h>
#include <string.h>

#ifdef CFG_DT
#include <kernel/dt.h>
#include <libfdt.h>
#endif

static struct serial_chip *serial_console;

void __weak console_putc(int ch)
{
	if (!serial_console)
		return;

	if (ch == '\n')
		serial_console->ops->putc(serial_console, '\r');
	serial_console->ops->putc(serial_console, ch);
}

void __weak console_flush(void)
{
	if (!serial_console)
		return;

	serial_console->ops->flush(serial_console);
}

void register_serial_console(struct serial_chip *chip)
{
	serial_console = chip;
}

#ifdef CFG_DT
static int find_chosen_node(void *fdt)
{
	if (!fdt)
		return -1;

	int offset = fdt_path_offset(fdt, "/secure-chosen");

	if (offset < 0)
		offset = fdt_path_offset(fdt, "/chosen");

	return offset;
}

/*
 * Check if the /secure-chosen node in the DT contains an stdout-path value
 * for which we have a compatible driver. If so, switch the console to
 * this device.
 */
void configure_console_from_dt(void)
{
	const struct dt_driver *dt_drv;
	const struct serial_driver *sdrv;
	const struct fdt_property *prop;
	struct serial_chip *dev;
	char *stdout_data;
	const char *uart;
	const char *parms = NULL;
	void *fdt;
	int offs;
	char *p;

	/* Probe console from secure DT and fallback to non-secure DT */
	fdt = get_embedded_dt();
	offs = find_chosen_node(fdt);
	if (offs < 0) {
		fdt = get_external_dt();
		offs = find_chosen_node(fdt);
	}
	if (offs < 0) {
		DMSG("No console directive from DTB");
		return;
	}

	prop = fdt_get_property(fdt, offs, "stdout-path", NULL);
	if (!prop) {
		/*
		 * A secure-chosen or chosen node is present but defined
		 * no stdout-path property: no console expected
		 */
		IMSG("Switching off console");
		register_serial_console(NULL);
		return;
	}

	stdout_data = strdup(prop->data);
	if (!stdout_data)
		return;
	p = strchr(stdout_data, ':');
	if (p) {
		*p = '\0';
		parms = p + 1;
	}

	/* stdout-path may refer to an alias */
	uart = fdt_get_alias(fdt, stdout_data);
	if (!uart) {
		/* Not an alias, assume we have a node path */
		uart = stdout_data;
	}
	offs = fdt_path_offset(fdt, uart);
	if (offs < 0)
		goto out;

	dt_drv = dt_find_compatible_driver(fdt, offs);
	if (!dt_drv)
		goto out;

	sdrv = (const struct serial_driver *)dt_drv->driver;
	if (!sdrv)
		goto out;
	dev = sdrv->dev_alloc();
	if (!dev)
		goto out;
	/*
	 * If the console is the same as the early console, dev_init() might
	 * clear pending data. Flush to avoid that.
	 */
	console_flush();
	if (sdrv->dev_init(dev, fdt, offs, parms) < 0) {
		sdrv->dev_free(dev);
		goto out;
	}

	IMSG("Switching console to device: %s", uart);
	register_serial_console(dev);
out:
	free(stdout_data);
}

#endif /* CFG_DT */
