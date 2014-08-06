/* 
 * tclserv.c -- part of server.mod
 * 
 * $Id: tclserv.c,v 1.6 2000/03/23 23:17:58 fabian Exp $
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

static int tcl_isbotnick STDVAR
{
  BADARGS(2, 2, " nick");
  if (match_my_nick(argv[1]))
    Tcl_AppendResult(irp, "1", NULL);
  else
    Tcl_AppendResult(irp, "0", NULL);
  return TCL_OK;
}

static int tcl_putquick STDVAR
{
  char s[511], *p;

  Context;
  BADARGS(2, 3, " text ?options?");
  if ((argc == 3) &&
      egg_strcasecmp(argv[2], "-next") && egg_strcasecmp(argv[2], "-normal")) {
      Tcl_AppendResult(irp, "unknown putquick option: should be one of: ",
		       "-normal -next", NULL);
    return TCL_ERROR;
  }
  strncpy(s, argv[1], 510);
  s[510] = 0;
  p = strchr(s, '\n');
  if (p != NULL)
    *p = 0;
   p = strchr(s, '\r');
  if (p != NULL)
    *p = 0;
  if (argc == 3 && !egg_strcasecmp(argv[2], "-next"))
    dprintf(DP_MODE_NEXT, "%s\n", s);
  else
    dprintf(DP_MODE, "%s\n", s);
  return TCL_OK;
}

static int tcl_putserv STDVAR
{
  char s[511], *p;

  Context;
  BADARGS(2, 3, " text ?options?");
  if ((argc == 3) &&
    egg_strcasecmp(argv[2], "-next") && egg_strcasecmp(argv[2], "-normal")) {
    Tcl_AppendResult(irp, "unknown putserv option: should be one of: ",
		     "-normal -next", NULL);
    return TCL_ERROR;
  }
  strncpy(s, argv[1], 510);
  s[510] = 0;
  p = strchr(s, '\n');
  if (p != NULL)
    *p = 0;
   p = strchr(s, '\r');
  if (p != NULL)
    *p = 0;
  if (argc == 3 && !egg_strcasecmp(argv[2], "-next"))
    dprintf(DP_SERVER_NEXT, "%s\n", s);
  else
    dprintf(DP_SERVER, "%s\n", s);
  return TCL_OK;
}

static int tcl_puthelp STDVAR
{
  char s[511], *p;

  Context;
  BADARGS(2, 3, " text ?options?");
  if ((argc == 3) &&
    egg_strcasecmp(argv[2], "-next") && egg_strcasecmp(argv[2], "-normal")) {
    Tcl_AppendResult(irp, "unknown puthelp option: should be one of: ",
		     "-normal -next", NULL);
    return TCL_ERROR;
  }
  strncpy(s, argv[1], 510);
  s[510] = 0;
  p = strchr(s, '\n');
  if (p != NULL)
    *p = 0;
   p = strchr(s, '\r');
  if (p != NULL)
    *p = 0;
  if (argc == 3 && !egg_strcasecmp(argv[2], "-next"))
    dprintf(DP_HELP_NEXT, "%s\n", s);
  else
    dprintf(DP_HELP, "%s\n", s);
  return TCL_OK;
}

static int tcl_jump STDVAR
{
  BADARGS(1, 4, " ?server? ?port? ?pass?");
  if (argc >= 2) {
    strcpy(newserver, argv[1]);
    if (argc >= 3)
      newserverport = atoi(argv[2]);
    else
      newserverport = default_port;
    if (argc == 4)
      strcpy(newserverpass, argv[3]);
  }
  cycle_time = 0;
  nuke_server("changing servers\n");
  return TCL_OK;
}

static int tcl_clearqueue STDVAR
{
  struct msgq *q, *qq;
  int msgs;
  char s[20];

  msgs = 0;
  BADARGS(2,2, " queue");
  if (!strcmp(argv[1],"all")) {
    msgs = (int) (modeq.tot + mq.tot + hq.tot);
    q = modeq.head;
    while (q) {
      qq = q->next;
      nfree(q->msg);
      nfree(q);
      q = qq;
    }
    q = mq.head;
    while (q) {
      qq = q->next;
      nfree(q->msg);
      nfree(q);
      q = qq;
    }
    q = hq.head;
    while (q) {
      qq = q->next;
      nfree(q->msg);
      nfree(q);
      q = qq;
    }
    modeq.tot = mq.tot = hq.tot = modeq.warned = mq.warned = hq.warned = 0;
    mq.head = hq.head = modeq.head = mq.last = hq.last = modeq.last = 0;
    double_warned = 0;
    burst = 0;
    simple_sprintf(s, "%d", msgs);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_OK;
  } else if (!strncmp(argv[1],"serv", 4)) {
    msgs = mq.tot;
    q = mq.head;
    while (q) {
      qq = q->next;
      nfree(q->msg);
      nfree(q);
      q = qq;
    }
    mq.tot = mq.warned = 0;
    mq.head = mq.last = 0;
    if (modeq.tot == 0)
      burst = 0;
    double_warned = 0;
    mq.tot = mq.warned = 0;
    mq.head = mq.last = 0;
    simple_sprintf(s, "%d", msgs);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_OK;
  } else if (!strcmp(argv[1],"mode")) {
    msgs = modeq.tot;
    q = modeq.head;
    while (q) {
      qq = q->next;
      nfree(q->msg);
      nfree(q);
      q = qq;
    }
    if (mq.tot == 0)
      burst = 0;
    double_warned = 0;
    modeq.tot = modeq.warned = 0;
    modeq.head = modeq.last = 0;
    simple_sprintf(s, "%d", msgs);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_OK;
  } else if (!strcmp(argv[1],"help")) {
    msgs = hq.tot;
    q = hq.head;
    while (q) {
      qq = q->next;
      nfree(q->msg);
      nfree(q);
      q = qq;
    }
    double_warned = 0;
    hq.tot = hq.warned = 0;
    hq.head = hq.last = 0;
    simple_sprintf(s, "%d", msgs);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_OK;
  }
  Tcl_AppendResult(irp, "bad option: must be mode, server, help, or all",
                   NULL);
  return TCL_ERROR;
}

static int tcl_queuesize STDVAR
{
  char s[20];
  int x;

  BADARGS(1, 2, " ?queue?");
  if (argc == 1) {
    x = (int) (modeq.tot + hq.tot + mq.tot);
    simple_sprintf(s, "%d", x);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_OK;
  } else if (!strncmp(argv[1], "serv", 4)) {
    x = (int) (mq.tot);
    simple_sprintf(s, "%d", x);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_OK;
  } else if (!strcmp(argv[1], "mode")) {
    x = (int) (modeq.tot);
    simple_sprintf(s, "%d", x);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_OK;
  } else if (!strcmp(argv[1], "help")) {
    x = (int) (hq.tot);
    simple_sprintf(s, "%d", x);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_OK;
  }
  Tcl_AppendResult(irp, "bad option: must be mode, server, or help", NULL);
  return TCL_ERROR;
}

static tcl_cmds my_tcl_cmds[] =
{
  {"jump",		tcl_jump},
  {"isbotnick",		tcl_isbotnick},
  {"clearqueue",	tcl_clearqueue},
  {"queuesize",		tcl_queuesize},
  {"puthelp",		tcl_puthelp},
  {"putserv",		tcl_putserv},
  {"putquick",		tcl_putquick},
  {NULL,		NULL},
};
