/*
 * Copyright (c) Souldbminer, Lightos_ and Horizon OC Contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */
 
/* --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy us a beer in return.  - The sys-clk authors
 * --------------------------------------------------------------------------
 */


#include "errors.h"
#include <cstdarg>
#include <cstring>
#include "file_utils.h"
void Errors::ThrowException(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const char* msg = Errors::FormatMessage(format, args);
    va_end(args);
    FileUtils::LogLine(format, args);
    throw std::runtime_error(msg);
}

const char* Errors::FormatMessage(const char* format, va_list args)
{
    size_t len = vsnprintf(NULL, 0, format, args) * sizeof(char);
    char* buf = (char*)malloc(len + 1);
    if (buf == NULL)
    {
        return format;
    }

    vsnprintf(buf, len + 1, format, args);

    return buf;
}
