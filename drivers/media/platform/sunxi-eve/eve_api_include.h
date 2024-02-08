/*
 * (C) Copyright 2016-2017
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * xiongyi<xiongyi@allwinnertech.com>
 *
 * Driver for embedded vision engine(EVE).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef EVE_API_INCLUDE_H
#define EVE_API_INCLUDE_H

#define EVE_MAGIC 'x'

#define EVE_READ_RESNUM			_IO(EVE_MAGIC, 0)
#define EVE_WRITE_REGISTER	_IO(EVE_MAGIC, 1)
#define EVE_READ_REGISTER		_IO(EVE_MAGIC, 2)
#define EVE_OPEN_CLK				_IO(EVE_MAGIC, 3)
#define EVE_CLOSE_CLK				_IO(EVE_MAGIC, 4)
#define EVE_PLL_SET					_IO(EVE_MAGIC, 5)
#define EVE_SYS_RESET				_IO(EVE_MAGIC, 6)
#define EVE_MOD_RESET				_IO(EVE_MAGIC, 7)

struct eve_register {
	unsigned long addr;
	unsigned long value;
};

#endif
