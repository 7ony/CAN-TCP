/*
 *	This file is part of libpegase.
 *
 *	Libpegase is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	Libpegase is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 *
 *	Copyright (C) 2009	William MARTIN <william.martin@lcpc.fr>
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdio.h>

/****************************************************************************************
 *
 *	Fichier de définition de Macro de debbugage et de Niveau de debuggage
 *
 *	Debug level of the application (0 to 3)
 *	- 0 : Silent
 *	- 1 : DEBUG_INFO only
 *	- 2 : DEBUG_INFO and DEBUG
 *	- 3 : DEBUG_INFO, DEBUG, DEBUG_FLOOD, DUMP_MEMORY and DUMP_MEMORY_ASCII
 *
 *	The debug level is set to 2 by default, if not already define
 *
 ****************************************************************************************/
#ifndef DEBUG_LEVEL
#define	DEBUG_LEVEL 2
#endif

/*
 *	Main debug macro
 */
#if(DEBUG_LEVEL >= 1)
	#define DEBUG_INFO(fmt, args...)	printf("%s @ %d : " fmt, __FUNCTION__, __LINE__, ## args)
#else
	#define DEBUG_INFO(fmt, args...) 	do {} while(0)
#endif

#if(DEBUG_LEVEL >= 2)
	#define DEBUG(fmt, args...) 		printf("%s @ %d : " fmt, __FUNCTION__, __LINE__, ## args)
#else
	#define DEBUG(fmt, args...) 		do {} while(0)
#endif

#if(DEBUG_LEVEL >= 3)
	#define DEBUG_FLOOD(fmt, args...) 	printf("%s @ %d : " fmt, __FUNCTION__, __LINE__, ## args)
#else
	#define DEBUG_FLOOD(fmt, args...) 	do {} while(0)
#endif

/*
 *	Memory dump macro
 */
#if(DEBUG_LEVEL >= 3)
	#define DUMP_MEMORY(buffer, size) 	do {	unsigned int i = 0;						\
							unsigned char *pbuffer = (unsigned char *) buffer;		\
							DEBUG("Zone memoire %p sur %d octets\n", buffer, size); 	\
							do {	printf("%02hhX ", (unsigned char) pbuffer[i++]);		\
								if((i % 16) == 0) { printf("\n"); }			\
							} while( i < size );						\
							printf("\n");							\
						} while(0)
#else
	#define DUMP_MEMORY(buffer, size) 	do {} while(0)
#endif

#if(DEBUG_LEVEL >= 3)
	#define DUMP_MEMORY_ASCII(buffer, size)	do {	unsigned int i = 0; 								\
							DEBUG("Traduction ASCII de la zone memoire %p sur %d octets\n", buffer, size); 	\
							do {	if((buffer[i] >= 32) && (buffer[i] <= 126))				\
									printf("%c", buffer[i]);					\
								else	printf(".");							\
								if(((++i) % 64) == 0) { printf("\n"); }					\
							} while( i != size );								\
							printf("\n");									\
						} while(0)
#else
	#define DUMP_MEMORY_ASCII(buffer, size) do {} while(0)
#endif

#endif	/* _DEBUG_H_ */

