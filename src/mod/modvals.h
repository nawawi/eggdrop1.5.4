/* 
 * modvals.h
 * 
 * $Id: modvals.h,v 1.13 2000/05/28 17:32:44 fabian Exp $
 */
/* 
 * Copyright (C) 1997  Robey Pointer
 * Copyright (C) 1999, 2000  Eggheads
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _EGG_MOD_MODVALS_H
#define _EGG_MOD_MODVALS_H

/* #define HOOK_GET_FLAGREC	  0 	*/
/* #define HOOK_BUILD_FLAGREC	  1 	*/
/* #define HOOK_SET_FLAGREC	  2 	*/
#define HOOK_READ_USERFILE	  3
#define HOOK_REHASH		  4
#define HOOK_MINUTELY		  5
#define HOOK_DAILY		  6
#define HOOK_HOURLY		  7
#define HOOK_USERFILE		  8
#define HOOK_SECONDLY		  9
#define HOOK_PRE_REHASH		 10
#define HOOK_IDLE		 11
#define HOOK_5MINUTELY		 12
#define REAL_HOOKS		 13
#define HOOK_SHAREOUT		105
#define HOOK_SHAREIN		106
#define HOOK_ENCRYPT_PASS	107
#define HOOK_QSERV		108
#define HOOK_ADD_MODE		109
#define HOOK_MATCH_NOTEREJ	110
#define HOOK_RFC_CASECMP	111
#define HOOK_DNS_HOSTBYIP	112
#define HOOK_DNS_IPBYHOST	113

/* These are FIXED once they are in a release they STAY
 */
#define MODCALL_START		  0
#define MODCALL_CLOSE		  1
#define MODCALL_EXPMEM		  2
#define MODCALL_REPORT		  3
/* Filesys */
#define FILESYS_REMOTE_REQ	  4
#define FILESYS_ADDFILE		  5
#define FILESYS_INCRGOTS	  6
#define FILESYS_ISVALID		  7
/* Share */
#define SHARE_FINISH		  4
#define SHARE_DUMP_RESYNC	  5
/* Channels */
#define CHANNEL_CLEAR		 15
/* Server */
#define SERVER_BOTNAME		  4
#define SERVER_BOTUSERHOST	  5
#define SERVER_NICKLEN		 38
/* IRC */
#define IRC_RECHECK_CHANNEL	  15
#define IRC_RECHECK_CHANNEL_MODES 17
/* Notes */
#define NOTES_CMD_NOTE		  4
/* Console */
#define CONSOLE_DOSTORE		  4
 
#ifdef HPUX_HACKS
#  include <dl.h>
#endif

typedef struct _module_entry {
  struct _module_entry	*next;
  char			*name;	/* Name of the module (without .so)	*/
  int			 major;	/* Major version number MUST match	*/
  int			 minor;	/* Minor version number MUST be >=	*/
#ifndef STATIC
#  ifdef HPUX_HACKS
  shl_t			 hand;
#  else		/* !HPUX_HACKS */
  void			*hand;	/* Module handle			*/
#  endif	/* !HPUX_HACKS */
#endif		/* STATIC */
  Function		*funcs;
#  ifdef DEBUG_MEM
  int			 mem_work;
#  endif	/* DEBUG_MEM */
} module_entry;

#endif		/* _EGG_MOD_MODVALS_H */
