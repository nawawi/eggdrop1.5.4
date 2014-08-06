/* 
 * tcl.c -- handles:
 *   the code for every command eggdrop adds to Tcl
 *   Tcl initialization
 *   getting and setting Tcl/eggdrop variables
 * 
 * $Id: tcl.c,v 1.24 2000/08/11 22:40:26 fabian Exp $
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

#include "main.h"

/* Used for read/write to internal strings */
typedef struct {
  char *str;			/* Pointer to actual string in eggdrop	     */
  int max;			/* max length (negative: read-only var
				   when protect is on) (0: read-only ALWAYS) */
  int flags;			/* 1 = directory			     */
} strinfo;

typedef struct {
  int *var;
  int ro;
} intinfo;


extern time_t	online_since;
extern int	backgrd, flood_telnet_thr, flood_telnet_time;
extern int	shtime, share_greet, require_p, keep_all_logs;
extern int	allow_new_telnets, stealth_telnets, use_telnet_banner;
extern int	default_flags, conmask, switch_logfiles_at, connect_timeout;
extern int	firewallport, reserved_port, notify_users_at;
extern int	flood_thr, ignore_time;
extern char	origbotname[], botuser[], motdfile[], admin[], userfile[],
		firewall[], helpdir[], notify_new[], hostname[], myip[],
		moddir[], tempdir[], owner[], network[], botnetnick[],
		bannerfile[], egg_version[], natip[], configfile[],
		logfile_suffix[];
extern int	die_on_sighup, die_on_sigterm, max_logs, max_logsize,
		enable_simul, dcc_total, debug_output, identtimeout,
		protect_telnet, dupwait_timeout, egg_numver, share_unlinks,
		dcc_sanitycheck, sort_users, tands, resolve_timeout,
		default_uflags, strict_host, userfile_perm;
extern struct dcc_t	*dcc;
extern tcl_timer_t	*timer, *utimer;
extern log_t		*logs;

int	    protect_readonly = 0;	/* turn on/off readonly protection */
char	    whois_fields[1025] = "";	/* fields to display in a .whois */
Tcl_Interp *interp;			/* eggdrop always uses the same
					   interpreter */
int	    dcc_flood_thr = 3;
int	    debug_tcl = 0;
int	    use_invites = 0;		/* Jason/drummer */
int	    use_exempts = 0;		/* Jason/drummer */
int	    force_expire = 0;		/* Rufus */
int	    remote_boots = 2;
int	    allow_dk_cmds = 1;
int	    must_be_owner = 1;
int	    max_dcc = 20;		/* needs at least 4 or 5 just to
					   get started. 20 should be enough   */
int	    min_dcc_port = 1024;	/* dcc-portrange, min port - dw/guppy */
int	    max_dcc_port = 65535;	/* dcc-portrange, max port - dw/guppy */
int	    quick_logs = 0;		/* quick write logs? (flush them
					   every min instead of every 5	      */
int	    par_telnet_flood = 1;       /* trigger telnet flood for +f
					   ppl? - dw			      */
int	    quiet_save = 0;             /* quiet-save patch by Lucas	      */
int	    strtot = 0;


/* Prototypes for tcl */
Tcl_Interp *Tcl_CreateInterp();


int expmem_tcl()
{
  int i, tot = 0;

  Context;
  for (i = 0; i < max_logs; i++)
    if (logs[i].filename != NULL) {
      tot += strlen(logs[i].filename) + 1;
      tot += strlen(logs[i].chname) + 1;
    }
  return tot + strtot;
}


/*
 *      Logging
 */

/* logfile [<modes> <channel> <filename>] */
static int tcl_logfile STDVAR
{
  int i;
  char s[151];

  BADARGS(1, 4, " ?logModes channel logFile?");
  if (argc == 1) {
    /* They just want a list of the logfiles and modes */
    for (i = 0; i < max_logs; i++)
      if (logs[i].filename != NULL) {
	strcpy(s, masktype(logs[i].mask));
	strcat(s, " ");
	strcat(s, logs[i].chname);
	strcat(s, " ");
	strcat(s, logs[i].filename);
	Tcl_AppendElement(interp, s);
      }
    return TCL_OK;
  }
  BADARGS(4, 4, " ?logModes channel logFile?");
  for (i = 0; i < max_logs; i++)
    if ((logs[i].filename != NULL) && (!strcmp(logs[i].filename, argv[3]))) {
      logs[i].flags &= ~LF_EXPIRING;
      logs[i].mask = logmodes(argv[1]);
      nfree(logs[i].chname);
      logs[i].chname = NULL;
      if (!logs[i].mask) {
	/* ending logfile */
	nfree(logs[i].filename);
	logs[i].filename = NULL;
	if (logs[i].f != NULL) {
	  fclose(logs[i].f);
	  logs[i].f = NULL;
	}
        logs[i].flags = 0;
      } else {
	logs[i].chname = (char *) nmalloc(strlen(argv[2]) + 1);
	strcpy(logs[i].chname, argv[2]);
      }
      Tcl_AppendResult(interp, argv[3], NULL);
      return TCL_OK;
    }
  /* Do not add logfiles without any flags to log ++rtc */
  if (!logmodes (argv [1])) {
    Tcl_AppendResult (interp, "can't remove \"", argv[3], 
                     "\" from list: no such logfile", NULL);
    return TCL_ERROR;
  }
  for (i = 0; i < max_logs; i++)
    if (logs[i].filename == NULL) {
      logs[i].flags = 0;
      logs[i].mask = logmodes(argv[1]);
      logs[i].filename = (char *) nmalloc(strlen(argv[3]) + 1);
      strcpy(logs[i].filename, argv[3]);
      logs[i].chname = (char *) nmalloc(strlen(argv[2]) + 1);
      strcpy(logs[i].chname, argv[2]);
      Tcl_AppendResult(interp, argv[3], NULL);
      return TCL_OK;
    }
  Tcl_AppendResult(interp, "reached max # of logfiles", NULL);
  return TCL_ERROR;
}

int findidx(int z)
{
  int j;

  for (j = 0; j < dcc_total; j++)
    if ((dcc[j].sock == z) && (dcc[j].type->flags & DCT_VALIDIDX))
      return j;
  return -1;
}

static void botnet_change(char *new)
{
  if (egg_strcasecmp(botnetnick, new)) {
    /* Trying to change bot's nickname */
    if (tands > 0) {
      putlog(LOG_MISC, "*", "* Tried to change my botnet nick, but I'm still linked to a botnet.");
      putlog(LOG_MISC, "*", "* (Unlink and try again.)");
      return;
    } else {
      if (botnetnick[0])
	putlog(LOG_MISC, "*", "* IDENTITY CHANGE: %s -> %s", botnetnick, new);
      strcpy(botnetnick, new);
    }
  }
}


/*
 *     Vars, traces, misc
 */

int init_dcc_max(), init_misc();

/* Used for read/write to integer couplets */
typedef struct {
  int *left;			/* left side of couplet */
  int *right;			/* right side */
} coupletinfo;

/* Read/write integer couplets (int1:int2) */
static char *tcl_eggcouplet(ClientData cdata, Tcl_Interp *irp, char *name1,
			    char *name2, int flags)
{
  char *s, s1[41];
  coupletinfo *cp = (coupletinfo *) cdata;

  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    sprintf(s1, "%d:%d", *(cp->left), *(cp->right));
    Tcl_SetVar2(interp, name1, name2, s1, TCL_GLOBAL_ONLY);
    if (flags & TCL_TRACE_UNSETS)
      Tcl_TraceVar(interp, name1,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggcouplet, cdata);
  } else {			/* writes */
    s = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    if (s != NULL) {
      int nr1, nr2;

      if (strlen(s) > 40)
	s[40] = 0;
      sscanf(s, "%d%*c%d", &nr1, &nr2);
      *(cp->left) = nr1;
      *(cp->right) = nr2;
    }
  }
  return NULL;
}

/* Read or write normal integer.
 */
static char *tcl_eggint(ClientData cdata, Tcl_Interp *irp, char *name1,
			char *name2, int flags)
{
  char *s, s1[40];
  long l;
  intinfo *ii = (intinfo *) cdata;

  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    /* Special cases */
    if ((int *) ii->var == &conmask)
      strcpy(s1, masktype(conmask));
    else if ((int *) ii->var == &default_flags) {
      struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};
      fr.global = default_flags;
      fr.udef_global = default_uflags;
      build_flags(s1, &fr, 0);
    } else if ((int *) ii->var == &userfile_perm) {
      sprintf(s1, "0%o", userfile_perm);
    } else
      sprintf(s1, "%d", *(int *) ii->var);
    Tcl_SetVar2(interp, name1, name2, s1, TCL_GLOBAL_ONLY);
    if (flags & TCL_TRACE_UNSETS)
      Tcl_TraceVar(interp, name1,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggint, cdata);
    return NULL;
  } else {			/* Writes */
    s = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    if (s != NULL) {
      if ((int *) ii->var == &conmask) {
	if (s[0])
	  conmask = logmodes(s);
	else
	  conmask = LOG_MODES | LOG_MISC | LOG_CMDS;
      } else if ((int *) ii->var == &default_flags) {
	struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

	break_down_flags(s, &fr, 0);
	default_flags = sanity_check(fr.global); /* drummer */
	default_uflags = fr.udef_global;
      } else if ((int *) ii->var == &userfile_perm) {
	int p = oatoi(s);

	if (p <= 0)
	  return "invalid userfile permissions";
	userfile_perm = p;
      } else if ((ii->ro == 2) || ((ii->ro == 1) && protect_readonly)) {
	return "read-only variable";
      } else {
	if (Tcl_ExprLong(interp, s, &l) == TCL_ERROR)
	  return interp->result;
	if ((int *) ii->var == &max_dcc) {
	  if (l < max_dcc)
	    return "you can't DECREASE max-dcc";
	  max_dcc = l;
	  init_dcc_max();
	} else if ((int *) ii->var == &max_logs) {
	  if (l < max_logs)
	    return "you can't DECREASE max-logs";
	  max_logs = l;
	  init_misc();
	} else
	  *(ii->var) = (int) l;
      }
    }
    return NULL;
  }
}

/* Read/write normal string variable
 */
static char *tcl_eggstr(ClientData cdata, Tcl_Interp *irp, char *name1,
			char *name2, int flags)
{
  char *s;
  strinfo *st = (strinfo *) cdata;

  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    if ((st->str == firewall) && (firewall[0])) {
      char s1[161];

      sprintf(s1, "%s:%d", firewall, firewallport);
      Tcl_SetVar2(interp, name1, name2, s1, TCL_GLOBAL_ONLY);
    } else
      Tcl_SetVar2(interp, name1, name2, st->str, TCL_GLOBAL_ONLY);
    if (flags & TCL_TRACE_UNSETS) {
      Tcl_TraceVar(interp, name1, TCL_TRACE_READS | TCL_TRACE_WRITES |
		   TCL_TRACE_UNSETS, tcl_eggstr, cdata);
      if ((st->max <= 0) && (protect_readonly || (st->max == 0)))
	return "read-only variable"; /* it won't return the error... */
    }
    return NULL;
  } else {			/* writes */
    if ((st->max <= 0) && (protect_readonly || (st->max == 0))) {
      Tcl_SetVar2(interp, name1, name2, st->str, TCL_GLOBAL_ONLY);
      return "read-only variable";
    }
    s = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    if (s != NULL) {
      if (strlen(s) > abs(st->max))
	s[abs(st->max)] = 0;
      if (st->str == botnetnick)
	botnet_change(s);
      else if (st->str == logfile_suffix)
	logsuffix_change(s);
      else if (st->str == firewall) {
	splitc(firewall, s, ':');
	if (!firewall[0])
	  strcpy(firewall, s);
	else
	  firewallport = atoi(s);
      } else
	strcpy(st->str, s);
      if ((st->flags) && (s[0])) {
	if (st->str[strlen(st->str) - 1] != '/')
	  strcat(st->str, "/");
      }
    }
    return NULL;
  }
}

/* Add/remove tcl commands
 */
void add_tcl_commands(tcl_cmds *tab)
{
  int i;

  for (i = 0; tab[i].name; i++)
    Tcl_CreateCommand(interp, tab[i].name, tab[i].func, NULL, NULL);
}

void rem_tcl_commands(tcl_cmds *tab)
{
  int i;

  for (i = 0; tab[i].name; i++)
    Tcl_DeleteCommand(interp, tab[i].name);
}

/* Strings */
static tcl_strings def_tcl_strings[] =
{
  {"botnet-nick",	botnetnick,	HANDLEN,	0},
  {"userfile",		userfile,	120,		STR_PROTECT},
  {"motd",		motdfile,	120,		STR_PROTECT},
  {"admin",		admin,		120,		0},
  {"help-path",		helpdir,	120,		STR_DIR | STR_PROTECT},
  {"temp-path",		tempdir,	120,		STR_DIR | STR_PROTECT},
#ifndef STATIC
  {"mod-path",		moddir,		120,		STR_DIR | STR_PROTECT},
#endif
  {"notify-newusers",	notify_new,	120,		0},
  {"owner",		owner,		120,		STR_PROTECT},
  {"my-hostname",	hostname,	120,		0},
  {"my-ip",		myip,		120,		0},
  {"network",		network,	40,		0},
  {"whois-fields",	whois_fields,	1024,		0},
  {"nat-ip",		natip,		120,		0},
  {"username",		botuser,	10,		0},
  {"version",		egg_version,	0,		0},
  {"firewall",		firewall,	120,		0},
/* confvar patch by aaronwl */
  {"config",		configfile,	0,		0},
  {"telnet-banner",	bannerfile,	120,		STR_PROTECT},
  {"logfile-suffix",	logfile_suffix,	20,		0},
  {NULL,		NULL,		0,		0}
};

/* Ints */
static tcl_ints def_tcl_ints[] =
{
  {"ignore-time",		&ignore_time,		0},
  {"dcc-flood-thr",		&dcc_flood_thr,		0},
  {"hourly-updates",		&notify_users_at,	0},
  {"switch-logfiles-at",	&switch_logfiles_at,	0},
  {"connect-timeout",		&connect_timeout,	0},
  {"reserved-port",		&reserved_port,		0},
  /* booleans (really just ints) */
  {"require-p",			&require_p,		0},
  {"keep-all-logs",		&keep_all_logs,		0},
  {"open-telnets",		&allow_new_telnets,	0},
  {"stealth-telnets",		&stealth_telnets,	0},
  {"use-telnet-banner",		&use_telnet_banner,	0},
  {"uptime",			(int *) &online_since,	2},
  {"console",			&conmask,		0},
  {"default-flags",		&default_flags,		0},
  /* moved from eggdrop.h */
  {"numversion",		&egg_numver,		2},
  {"debug-tcl",			&debug_tcl,		1},
  {"die-on-sighup",		&die_on_sighup,		1},
  {"die-on-sigterm",		&die_on_sigterm,	1},
  {"remote-boots",		&remote_boots,		1},
  {"max-dcc",			&max_dcc,		0},
  {"max-logs",			&max_logs,		0},
  {"max-logsize",		&max_logsize,		0},
  {"quick-logs",		&quick_logs,		0},
  {"enable-simul",		&enable_simul,		1},
  {"debug-output",		&debug_output,		1},
  {"protect-telnet",		&protect_telnet,	0},
  {"dcc-sanitycheck",		&dcc_sanitycheck,	0},
  {"sort-users",		&sort_users,		0},
  {"ident-timeout",		&identtimeout,		0},
  {"share-unlinks",		&share_unlinks,		0},
  {"log-time",			&shtime,		0},
  {"allow-dk-cmds",		&allow_dk_cmds,		0},
  {"resolve-timeout",		&resolve_timeout,	0},
  {"must-be-owner",		&must_be_owner,		1},
  {"paranoid-telnet-flood",	&par_telnet_flood,	0},
  {"use-exempts",		&use_exempts,		0},			/* Jason/drummer */
  {"use-invites",		&use_invites,		0},			/* Jason/drummer */
  {"quiet-save",		&quiet_save,		0},			/* Lucas */
  {"force-expire",		&force_expire,		0},			/* Rufus */
  {"dupwait-timeout",		&dupwait_timeout,	0},
  {"strict-host",		&strict_host,		0}, 			/* drummer */
  {"userfile-perm",		&userfile_perm,		0},
  {NULL,			NULL,			0}	/* arthur2 */
};

static tcl_coups def_tcl_coups[] =
{
  {"telnet-flood",	&flood_telnet_thr,	&flood_telnet_time},
  {"dcc-portrange",	&min_dcc_port,		&max_dcc_port},	/* dw */
  {NULL,		NULL,			NULL}
};

/* Set up Tcl variables that will hook into eggdrop internal vars via
 * trace callbacks.
 */
static void init_traces()
{
  add_tcl_coups(def_tcl_coups);
  add_tcl_strings(def_tcl_strings);
  add_tcl_ints(def_tcl_ints);
}

void kill_tcl()
{
  Context;
  rem_tcl_coups(def_tcl_coups);
  rem_tcl_strings(def_tcl_strings);
  rem_tcl_ints(def_tcl_ints);
  kill_bind();
  Tcl_DeleteInterp(interp);
}

extern tcl_cmds tcluser_cmds[], tcldcc_cmds[], tclmisc_cmds[], tcldns_cmds[];

/* Not going through Tcl's crazy main() system (what on earth was he
 * smoking?!) so we gotta initialize the Tcl interpreter
 */
void init_tcl(int argc, char **argv)
{
#ifndef HAVE_PRE7_5_TCL
  int i;
  char pver[1024] = "";
#endif

  Context;
#ifndef HAVE_PRE7_5_TCL
  /* This is used for 'info nameofexecutable'.
   * The filename in argv[0] must exist in a directory listed in
   * the environment variable PATH for it to register anything.
   */
  Tcl_FindExecutable(argv[0]);
#endif

  /* Initialize the interpreter */
  interp = Tcl_CreateInterp();
  Tcl_Init(interp);

#ifdef DEBUG_MEM
  /* Initialize Tcl's memory debugging if we have it */
  Tcl_InitMemory(interp);
#endif

  /* Set Tcl variable tcl_interactive to 0 */
  Tcl_SetVar(interp, "tcl_interactive", "0", TCL_GLOBAL_ONLY);

  /* Initialize binds and traces */
  init_bind();
  init_traces();

  /* Add new commands */
  Tcl_CreateCommand(interp, "logfile", tcl_logfile, NULL, NULL);
  /* Isnt this much neater :) */
  add_tcl_commands(tcluser_cmds);
  add_tcl_commands(tcldcc_cmds);
  add_tcl_commands(tclmisc_cmds);
  add_tcl_commands(tcldns_cmds);

#ifndef HAVE_PRE7_5_TCL
  /* Add eggdrop to Tcl's package list */
  for (i = 0; i <= strlen(egg_version); i++) {
    if ((egg_version[i] == ' ') || (egg_version[i] == '+'))
      break;
    pver[strlen(pver)] = egg_version[i];
  }
  Tcl_PkgProvide(interp, "eggdrop", pver);
#endif
}

void do_tcl(char *whatzit, char *script)
{
  int code;
  FILE *f = 0;

  if (debug_tcl) {
    f = fopen("DEBUG.TCL", "a");
    if (f != NULL)
      fprintf(f, "eval: %s\n", script);
  }
  Context;
  code = Tcl_Eval(interp, script);
  if (debug_tcl && (f != NULL)) {
    fprintf(f, "done eval, result=%d\n", code);
    fclose(f);
  }
  if (code != TCL_OK) {
    putlog(LOG_MISC, "*", "Tcl error in script for '%s':", whatzit);
    putlog(LOG_MISC, "*", "%s", interp->result);
  }
}

/* Read the tcl file fname into memory and interpret it. Not using
 * Tcl_EvalFile avoids problems with high ascii characters.
 *
 * returns:   1 - if everything was okay
 */
int readtclprog(char *fname)
{
  int	 code;
  long	 size;
  char	*script;
  FILE	*f;

  if ((f = fopen(fname, "r")) == NULL)
    return 0;

  /* Find out file size. */
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);

  /* Allocate buffer to save the file's data in. */
  if ((script = nmalloc(size + 1)) == NULL) {
    fclose(f);
    return 0;
  }
  script[size] = 0;

  /* Read file's data to the allocated buffer. */
  fread(script, 1, size, f);
  fclose(f);

  if (debug_tcl) {
    if ((f = fopen("DEBUG.TCL", "a")) != NULL)
      fprintf(f, "*** eval: %s\n", script);
  }
  code = Tcl_Eval(interp, script);
  nfree(script);
  if (debug_tcl && f) {
    fprintf(f, "*** done eval, result=%d\n", code);
    fclose(f);
  }

  if (code != TCL_OK) {
    putlog(LOG_MISC, "*", "Tcl error in file '%s':", fname);
    putlog(LOG_MISC, "*", "%s",
	   Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY));
    return 0;
  }

  /* Refresh internal variables */
  return 1;
}

void add_tcl_strings(tcl_strings *list)
{
  int i;
  strinfo *st;
  int tmp;

  for (i = 0; list[i].name; i++) {
    st = (strinfo *) nmalloc(sizeof(strinfo));
    strtot += sizeof(strinfo);
    st->max = list[i].length - (list[i].flags & STR_DIR);
    if (list[i].flags & STR_PROTECT)
      st->max = -st->max;
    st->str = list[i].buf;
    st->flags = (list[i].flags & STR_DIR);
    tmp = protect_readonly;
    protect_readonly = 0;
    tcl_eggstr((ClientData) st, interp, list[i].name, NULL, TCL_TRACE_WRITES);
    tcl_eggstr((ClientData) st, interp, list[i].name, NULL, TCL_TRACE_READS);
    Tcl_TraceVar(interp, list[i].name, TCL_TRACE_READS | TCL_TRACE_WRITES |
		 TCL_TRACE_UNSETS, tcl_eggstr, (ClientData) st);
  }
}

void rem_tcl_strings(tcl_strings *list)
{
  int i;
  strinfo *st;

  for (i = 0; list[i].name; i++) {
    st = (strinfo *) Tcl_VarTraceInfo(interp, list[i].name,
				      TCL_TRACE_READS |
				      TCL_TRACE_WRITES |
				      TCL_TRACE_UNSETS,
				      tcl_eggstr, NULL);
    Tcl_UntraceVar(interp, list[i].name,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggstr, st);
    if (st != NULL) {
      strtot -= sizeof(strinfo);
      nfree(st);
    }
  }
}

void add_tcl_ints(tcl_ints *list)
{
  int i, tmp;
  intinfo *ii;

  for (i = 0; list[i].name; i++) {
    ii = nmalloc(sizeof(intinfo));
    strtot += sizeof(intinfo);
    ii->var = list[i].val;
    ii->ro = list[i].readonly;
    tmp = protect_readonly;
    protect_readonly = 0;
    tcl_eggint((ClientData) ii, interp, list[i].name, NULL, TCL_TRACE_WRITES);
    protect_readonly = tmp;
    tcl_eggint((ClientData) ii, interp, list[i].name, NULL, TCL_TRACE_READS);
    Tcl_TraceVar(interp, list[i].name,
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 tcl_eggint, (ClientData) ii);
  }

}

void rem_tcl_ints(tcl_ints *list)
{
  int i;
  intinfo *ii;

  for (i = 0; list[i].name; i++) {
    ii = (intinfo *) Tcl_VarTraceInfo(interp, list[i].name,
				      TCL_TRACE_READS |
				      TCL_TRACE_WRITES |
				      TCL_TRACE_UNSETS,
				      tcl_eggint, NULL);
    Tcl_UntraceVar(interp, list[i].name,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggint, (ClientData) ii);
    if (ii) {
      strtot -= sizeof(intinfo);
      nfree(ii);
    }
  }
}

/* Allocate couplet space for tracing couplets
 */
void add_tcl_coups(tcl_coups *list)
{
  coupletinfo *cp;
  int i;

  for (i = 0; list[i].name; i++) {
    cp = (coupletinfo *) nmalloc(sizeof(coupletinfo));
    strtot += sizeof(coupletinfo);
    cp->left = list[i].lptr;
    cp->right = list[i].rptr;
    tcl_eggcouplet((ClientData) cp, interp, list[i].name, NULL,
		   TCL_TRACE_WRITES);
    tcl_eggcouplet((ClientData) cp, interp, list[i].name, NULL,
		   TCL_TRACE_READS);
    Tcl_TraceVar(interp, list[i].name,
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 tcl_eggcouplet, (ClientData) cp);
  }
}

void rem_tcl_coups(tcl_coups * list)
{
  coupletinfo *cp;
  int i;

  for (i = 0; list[i].name; i++) {
    cp = (coupletinfo *) Tcl_VarTraceInfo(interp, list[i].name,
					  TCL_TRACE_READS |
					  TCL_TRACE_WRITES |
					  TCL_TRACE_UNSETS,
					  tcl_eggcouplet, NULL);
    strtot -= sizeof(coupletinfo);
    Tcl_UntraceVar(interp, list[i].name,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggcouplet, (ClientData) cp);
    nfree(cp);
  }
}
