/*
    MIT License

    Copyright (c) 2024 Roy Merkel

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include "memmem.h"

void *memmem_impl(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen)
{
	const unsigned char *cmpp;
	const unsigned char *p;
	const unsigned char *endp;
	const unsigned char *q;
	const unsigned char *endq;
	unsigned char found;

	if(haystack == NULL)
	{
		return NULL;
	}
	if(needle == NULL)
	{
		return (void*)haystack;
	}
	if(haystacklen == 0)
	{
		return NULL;
	}
	if(needlelen == 0)
	{
		return (void*)haystack;
	}

	if(needlelen > haystacklen)
	{
		return NULL;
	}

	endp = haystack + haystacklen - needlelen;
	endq = needle + needlelen;
	for(p = haystack; p <= endp; p++)
	{
		found = 1;
		cmpp = p;
		for(q = needle; q < endq; q++)
		{
			if(*cmpp != *q)
			{
				found = 0;
				break;
			}
			else
			{
				cmpp++;
			}
		}
		if(found)
		{
			return (void*)p;
		}
	}

  return NULL;
}
