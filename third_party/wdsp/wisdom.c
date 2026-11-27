/*  wisdom.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013-2026 Warren Pratt, NR0V

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at  

warren@pratt.one

*/

#define _CRT_SECURE_NO_WARNINGS
#include "comm.h"

static char status[128];

PORT
char* wisdom_get_status()
{
	return status;
}

PORT
int WDSPwisdom (char* directory)
{
	char wisdom_file[1024];
	strcpy (wisdom_file, directory);
	strncat (wisdom_file, "wdspWisdom01", 16);
	if (fftw_import_wisdom_from_filename(wisdom_file))
	{
		sprintf(status, "Loaded FFTW wisdom");
		return 0;
	}
	sprintf(status, "No wisdom file; using FFTW estimate");
	return 0;
}