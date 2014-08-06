/* 
 * assoc.c -- part of assoc.mod
 *   the assoc code, moved here mainly from botnet.c for module work
 * 
 * $Id: assoc.c,v 1.9 2000/03/23 23:17:56 fabian Exp $
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

#define MODULE_NAME "assoc"
#define MAKING_ASSOC
#include "src/mod/module.h"
#include "src/tandem.h"
#include <stdlib.h>
#undef global
static Function *global = NULL;

/* Keep track of channel associations */
typedef struct travis {
  char name[21];
  unsigned int channel;
  struct travis *next;
} assoc_t;

/* Channel name-number associations */
static assoc_t *assoc;

static void botnet_send_assoc(int idx, int chan, char *nick,
			      char *buf)
{
  char x[1024];
  int idx2;

  simple_sprintf(x, "assoc %D %s %s", chan, nick, buf);
  for (idx2 = 0; idx2 < dcc_total; idx2++)
    if ((dcc[idx2].type == &DCC_BOT) && (idx2 != idx) &&
	(b_numver(idx2) >= NEAT_BOTNET) &&
	!(bot_flags(dcc[idx2].user) & BOT_ISOLATE))
      botnet_send_zapf(idx2, botnetnick, dcc[idx2].nick, x);
}

static int assoc_expmem()
{
  assoc_t *a = assoc;
  int size = 0;

  Context;
  while (a != NULL) {
    size += sizeof(assoc_t);
    a = a->next;
  }
  return size;
}

static void link_assoc(char *bot, char *via)
{
  char x[1024];

  if (!egg_strcasecmp(via, botnetnick)) {
    int idx = nextbot(bot);
    assoc_t *a = assoc;

    if (!(bot_flags(dcc[idx].user) & BOT_ISOLATE)) {
      Context;
      while (a != NULL) {
	if (a->name[0]) {
          simple_sprintf(x, "assoc %D %s %s", (int) a->channel, botnetnick,
           		a->name);
	  botnet_send_zapf(idx, botnetnick, dcc[idx].nick, x);
	}
	a = a->next;
      }
    }
  }
}

static void kill_assoc(int chan)
{
  assoc_t *a = assoc, *last = NULL;

  Context;
  while (a != NULL) {
    if (a->channel == chan) {
      if (last != NULL)
	last->next = a->next;
      else
	assoc = a->next;
      nfree(a);
      a = NULL;
    } else {
      last = a;
      a = a->next;
    }
  }
}

static void kill_all_assoc()
{
  assoc_t *a = assoc, *x;

  Context;
  while (a != NULL) {
    x = a;
    a = a->next;
    nfree(x);
  }
  assoc = NULL;
}

static void add_assoc(char *name, int chan)
{
  assoc_t *a = assoc, *b, *old = NULL;

  Context;
  while (a != NULL) {
    if (name[0] != 0 && !egg_strcasecmp(a->name, name)) {
      kill_assoc(a->channel);
      add_assoc(name, chan);
      return;
    }
    if (a->channel == chan) {
      strncpy(a->name, name, 20);
      a->name[20] = 0;
      return;
    }
    a = a->next;
  }
  /* Add in numerical order */
  a = assoc;
  while (a != NULL) {
    if (a->channel > chan) {
      b = (assoc_t *) nmalloc(sizeof(assoc_t));
      b->next = a;
      b->channel = chan;
      strncpy(b->name, name, 20);
      b->name[20] = 0;
      if (old == NULL)
	assoc = b;
      else
	old->next = b;
      return;
    }
    old = a;
    a = a->next;
  }
  /* Add at the end */
  b = (assoc_t *) nmalloc(sizeof(assoc_t));
  b->next = NULL;
  b->channel = chan;
  strncpy(b->name, name, 20);
  b->name[20] = 0;
  if (old == NULL)
    assoc = b;
  else
    old->next = b;
}

static int get_assoc(char *name)
{
  assoc_t *a = assoc;

  Context;
  while (a != NULL) {
    if (!egg_strcasecmp(a->name, name))
      return a->channel;
    a = a->next;
  }
  return -1;
}

static char *get_assoc_name(int chan)
{
  assoc_t *a = assoc;

  Context;
  while (a != NULL) {
    if (a->channel == chan)
      return a->name;
    a = a->next;
  }
  return NULL;
}

static void dump_assoc(int idx)
{
  assoc_t *a = assoc;

  Context;
  if (a == NULL) {
    dprintf(idx, "No channel names.\n");
    return;
  }
  dprintf(idx, " Chan  Name\n");
  while (a != NULL) {
    if (a->name[0])
      dprintf(idx, "%c%5d %s\n", (a->channel < 100000) ? ' ' : '*',
	      a->channel % 100000, a->name);
    a = a->next;
  }
  return;
}

static int cmd_assoc(struct userrec *u, int idx, char *par)
{
  char *num;
  int chan;

  Context;
  if (!par[0]) {
    putlog(LOG_CMDS, "*", "#%s# assoc", dcc[idx].nick);
    dump_assoc(idx);
  } else if (!u || !(u->flags & USER_BOTMAST)) {
    dprintf(idx, "What? You need '.help'.\n");
  } else {
    num = newsplit(&par);
    if (num[0] == '*') {
      chan = 100000 + atoi(num + 1);
      if ((chan < 100000) || (chan > 199999)) {
	dprintf(idx, "Channel # out of range: must be *0-*99999\n");
	return 0;
      }
    } else {
      chan = atoi(num);
      if (chan == 0) {
	dprintf(idx, "You can't name the main party line; it's just a party line.\n");
	return 0;
      } else if ((chan < 1) || (chan > 99999)) {
	dprintf(idx, "Channel # out of range: must be 1-99999\n");
	return 0;
      }
    }
    if (!par[0]) {
      /* Remove an association */
      if (get_assoc_name(chan) == NULL) {
	dprintf(idx, "Channel %s%d has no name.\n",
		(chan < 100000) ? "" : "*", chan % 100000);
	return 0;
      }
      kill_assoc(chan);
      putlog(LOG_CMDS, "*", "#%s# assoc %d", dcc[idx].nick, chan);
      dprintf(idx, "Okay, removed name for channel %s%d.\n",
	      (chan < 100000) ? "" : "*", chan % 100000);
      chanout_but(-1, chan, "--- %s removed this channel's name.\n",
		  dcc[idx].nick);
      if (chan < 100000)
	botnet_send_assoc(-1, chan, dcc[idx].nick, "0");
      return 0;
    }
    if (strlen(par) > 20) {
      dprintf(idx, "Channel's name can't be that long (20 chars max).\n");
      return 0;
    }
    if ((par[0] >= '0') && (par[0] <= '9')) {
      dprintf(idx, "First character of the channel name can't be a digit.\n");
      return 0;
    }
    add_assoc(par, chan);
    putlog(LOG_CMDS, "*", "#%s# assoc %d %s", dcc[idx].nick, chan, par);
    dprintf(idx, "Okay, channel %s%d is '%s' now.\n",
	    (chan < 100000) ? "" : "*", chan % 100000, par);
    chanout_but(-1, chan, "--- %s named this channel '%s'\n", dcc[idx].nick,
		par);
    if (chan < 100000)
      botnet_send_assoc(-1, chan, dcc[idx].nick, par);
  }
  return 0;
}

static int tcl_killassoc STDVAR {
  int chan;

  Context;
  BADARGS(2, 2, " chan");
  if (argv[1][0] == '&')
    kill_all_assoc();
  else {
    chan = atoi(argv[1]);
    if ((chan < 1) || (chan > 199999)) {
      Tcl_AppendResult(irp, "invalid channel #", NULL);
      return TCL_ERROR;
    }
    kill_assoc(chan);
    botnet_send_assoc(-1, chan, "*script*", "0");
  }
  return TCL_OK;
}

static int tcl_assoc STDVAR {
  int chan;
  char name[21], *p;

  Context;
  BADARGS(2, 3, " chan ?name?");
  if ((argc == 2) && ((argv[1][0] < '0') || (argv[1][0] > '9'))) {
    chan = get_assoc(argv[1]);
    if (chan == -1)
      Tcl_AppendResult(irp, "", NULL);
    else {
      simple_sprintf(name, "%d", chan);
      Tcl_AppendResult(irp, name, NULL);
    }
    return TCL_OK;
  }
  chan = atoi(argv[1]);
  if ((chan < 1) || (chan > 199999)) {
    Tcl_AppendResult(irp, "invalid channel #", NULL);
    return TCL_ERROR;
  }
  if (argc == 3) {
    strncpy(name, argv[2], 20);
    name[20] = 0;
    add_assoc(name, chan);
    botnet_send_assoc(-1, chan, "*script*", name);
  }
  p = get_assoc_name(chan);
  if (p == NULL)
    name[0] = 0;
  else
    strcpy(name, p);
  Tcl_AppendResult(irp, name, NULL);
  return TCL_OK;
}

static void zapf_assoc(char *botnick, char *code, char *par)
{
  int idx = nextbot(botnick);
  char *s, *s1, *nick;
  int linking = 0, chan;

  Context;
  if ((idx >= 0) && !(bot_flags(dcc[idx].user) & BOT_ISOLATE)) {
    if (!egg_strcasecmp(dcc[idx].nick, botnick))
      linking = b_status(idx) & STAT_LINKING;
    s = newsplit(&par);
    chan = base64_to_int(s);
    if ((chan > 0) || (chan < GLOBAL_CHANS)) {
      nick = newsplit(&par);
      s1 = get_assoc_name(chan);
      if (linking && ((s1 == NULL) || (s1[0] == 0) ||
		      (((int) get_user(find_entry_type("BOTFL"),
				       dcc[idx].user) & BOT_HUB)))) {
	add_assoc(par, chan);
	botnet_send_assoc(idx, chan, nick, par);
	chanout_but(-1, chan, "--- (%s) named this channel '%s'.\n",
		    nick, par);
      } else if (par[0] == '0') {
	kill_assoc(chan);
	chanout_but(-1, chan, "--- (%s) %s removed this channel's name.\n",
		    botnick, nick);
      } else if (get_assoc(par) != chan) {
	/* New one i didn't know about -- pass it on */
	s1 = get_assoc_name(chan);
	add_assoc(par, chan);
	chanout_but(-1, chan, "--- (%s) %s named this channel '%s'.\n",
		    botnick, nick, par);
      }
    }
  }
}

/* A report on the module status.
 */
static void assoc_report(int idx, int details)
{
  assoc_t *a = assoc;
  int size = 0, count = 0;;

  Context;
  if (details) {
    while (a != NULL) {
      count++;
      size += sizeof(assoc_t);
      a = a->next;
    }
    dprintf(idx, "    %d assocs using %d bytes\n",
	    count, size);
  }
}

static cmd_t mydcc[] =
{
  {"assoc",	"",	cmd_assoc,		NULL},
  {NULL, 	NULL,	NULL,			NULL}
};

static cmd_t mybot[] =
{
  {"assoc",	"",	(Function) zapf_assoc,	NULL},
};

static cmd_t mylink[] =
{
  {"*",		"",	(Function) link_assoc,	"assoc"},
  {NULL,	NULL,	NULL,			NULL}
};

static tcl_cmds mytcl[] =
{
  {"assoc",		tcl_assoc},
  {"killassoc",		tcl_killassoc},
  {NULL,		NULL}
};

static char *assoc_close()
{
  Context;
  kill_all_assoc();
  rem_builtins(H_dcc, mydcc);
  rem_builtins(H_bot, mybot);
  rem_builtins(H_link, mylink);
  module_undepend(MODULE_NAME);
  rem_tcl_commands(mytcl);
  rem_help_reference("assoc.help");
  return NULL;
}

char *assoc_start();

static Function assoc_table[] =
{
  (Function) assoc_start,
  (Function) assoc_close,
  (Function) assoc_expmem,
  (Function) assoc_report,
};

char *assoc_start(Function * global_funcs)
{
  global = global_funcs;

  Context;
  module_register(MODULE_NAME, assoc_table, 2, 0);
  if (!module_depend(MODULE_NAME, "eggdrop", 105, 0)) {
    module_undepend(MODULE_NAME);
    return "This module requires eggdrop1.5.0 or later";
  }
  assoc = NULL;
  add_builtins(H_dcc, mydcc);
  add_builtins(H_bot, mybot);
  add_builtins(H_link, mylink);
  add_tcl_commands(mytcl);
  add_help_reference("assoc.help");
  return NULL;
}
