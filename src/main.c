/* 
 * main.c -- handles:
 *   core event handling
 *   signal handling
 *   command line arguments
 *   context and assert debugging
 * 
 * $Id: main.c,v 1.40 2000/06/10 01:00:22 fabian Exp $
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
/* 
 * The author (Robey Pointer) can be reached at:  robey@netcom.com
 * NOTE: Robey is no long working on this code, there is a discussion
 * list avaliable at eggheads@eggheads.org.
 */

#include "main.h"
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <netdb.h>
#include <setjmp.h>

#ifdef STOP_UAC				/* osf/1 complains a lot */
#include <sys/sysinfo.h>
#define UAC_NOPRINT    0x00000001	/* Don't report unaligned fixups */
#endif
/* Some systems have a working sys/wait.h even though configure will
 * decide it's not bsd compatable.  Oh well.
 */
#include "chan.h"
#include "modules.h"
#include "tandem.h"

#ifdef CYGWIN_HACKS
#include <windows.h>
#endif

#ifndef _POSIX_SOURCE
/* Solaris needs this */
#define _POSIX_SOURCE 1
#endif

extern char		 origbotname[], userfile[], botnetnick[];
extern int		 dcc_total, conmask, cache_hit, cache_miss,
			 max_logs, quick_logs;
extern struct dcc_t	*dcc;
extern struct userrec	*userlist;
extern struct chanset_t	*chanset;
extern log_t		*logs;
extern Tcl_Interp	*interp;
extern tcl_timer_t	*timer,
			*utimer;
extern jmp_buf		 alarmret;


/* 
 * Please use the PATCH macro instead of directly altering the version
 * string from now on (it makes it much easier to maintain patches).
 * Also please read the README file regarding your rights to distribute
 * modified versions of this bot.
 */

char	egg_version[1024] = "1.5.4";
int	egg_numver = 1050400;

char	notify_new[121] = "";	/* Person to send a note to for new users */
int	default_flags = 0;	/* Default user flags and */
int	default_uflags = 0;	/* Default userdefinied flags for people
				   who say 'hello' or for .adduser */

int	backgrd = 1;		/* Run in the background? */
int	con_chan = 0;		/* Foreground: constantly display channel
				   stats? */
int	term_z = 0;		/* Foreground: use the terminal as a party
				   line? */
char	configfile[121] = "eggdrop.conf"; /* Name of the config file */
char	helpdir[121];		/* Directory of help files (if used) */
char	textdir[121] = "";	/* Directory for text files that get dumped */
int	keep_all_logs = 0;	/* Never erase logfiles, no matter how old
				   they are? */
char	logfile_suffix[21] = ".%d%b%Y"; /* Format of logfile suffix. */
time_t	online_since;		/* Unix-time that the bot loaded up */
int	make_userfile = 0;	/* Using bot in make-userfile mode? (first
				   user to 'hello' becomes master) */
char	owner[121] = "";	/* Permanent owner(s) of the bot */
char	pid_file[40];		/* Name of the file for the pid to be
				   stored in */
int	save_users_at = 0;	/* How many minutes past the hour to
				   save the userfile? */
int	notify_users_at = 0;	/* How many minutes past the hour to
				   notify users of notes? */
int	switch_logfiles_at = 300; /* When (military time) to switch logfiles */
char	version[81];		/* Version info (long form) */
char	ver[41];		/* Version info (short form) */
char	egg_xtra[2048];		/* Patch info */
int	use_stderr = 1;		/* Send stuff to stderr instead of logfiles? */
int	do_restart = 0;		/* .restart has been called, restart asap */
int	die_on_sighup = 0;	/* die if bot receives SIGHUP */
int	die_on_sigterm = 0;	/* die if bot receives SIGTERM */
int	resolve_timeout = 15;	/* hostname/address lookup timeout */
time_t	now;			/* duh, now :) */

/* Traffic stats
 */
unsigned long	otraffic_irc = 0;
unsigned long	otraffic_irc_today = 0;
unsigned long	otraffic_bn = 0;
unsigned long	otraffic_bn_today = 0;
unsigned long	otraffic_dcc = 0;
unsigned long	otraffic_dcc_today = 0;
unsigned long	otraffic_filesys = 0;
unsigned long	otraffic_filesys_today = 0;
unsigned long	otraffic_trans = 0;
unsigned long	otraffic_trans_today = 0;
unsigned long	otraffic_unknown = 0;
unsigned long	otraffic_unknown_today = 0;
unsigned long	itraffic_irc = 0;
unsigned long	itraffic_irc_today = 0;
unsigned long	itraffic_bn = 0;
unsigned long	itraffic_bn_today = 0;
unsigned long	itraffic_dcc = 0;
unsigned long	itraffic_dcc_today = 0;
unsigned long	itraffic_trans = 0;
unsigned long	itraffic_trans_today = 0;
unsigned long	itraffic_unknown = 0;
unsigned long	itraffic_unknown_today = 0;

#ifdef DEBUG_CONTEXT
/* Context storage for fatal crashes */
char	cx_file[16][30];
char	cx_note[16][256];
int	cx_line[16];
int	cx_ptr = 0;
#endif


void fatal(char *s, int recoverable)
{
  int i;

  putlog(LOG_MISC, "*", "* %s", s);
  flushlogs();
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].sock >= 0)
      killsock(dcc[i].sock);
  unlink(pid_file);
  if (!recoverable)
    exit(1);
}


int expmem_chanprog(), expmem_users(), expmem_misc(), expmem_dccutil(),
 expmem_botnet(), expmem_tcl(), expmem_tclhash(), expmem_net(),
 expmem_modules(int), expmem_language(), expmem_tcldcc();

/* For mem.c : calculate memory we SHOULD be using
 */
int expected_memory()
{
  int tot;

  Context;
  tot = expmem_chanprog() + expmem_users() + expmem_misc() +
    expmem_dccutil() + expmem_botnet() + expmem_tcl() + expmem_tclhash() +
    expmem_net() + expmem_modules(0) + expmem_language() + expmem_tcldcc();
  return tot;
}

static void check_expired_dcc()
{
  int i;

  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type && dcc[i].type->timeout_val &&
	((now - dcc[i].timeval) > *(dcc[i].type->timeout_val))) {
      if (dcc[i].type->timeout)
	dcc[i].type->timeout(i);
      else if (dcc[i].type->eof)
	dcc[i].type->eof(i);
      else
	continue;
      /* Only timeout 1 socket per cycle, too risky for more */
      return;
    }
}

#ifdef DEBUG_CONTEXT
static int	nested_debug = 0;

void write_debug()
{
  int x;
  char s[80];
  int y;

  if (nested_debug) {
    /* Yoicks, if we have this there's serious trouble!
     * All of these are pretty reliable, so we'll try these.
     * 
     * NOTE: dont try and display context-notes in here, it's
     *       _not_ safe <cybah>
     */
    x = creat("DEBUG.DEBUG", 0644);
    setsock(x, SOCK_NONSOCK);
    if (x >= 0) {
      strcpy(s, ctime(&now));
      dprintf(-x, "Debug (%s) written %s", ver, s);
      dprintf(-x, "Please report problem to eggheads@eggheads.org");
      dprintf(-x, "after a visit to http://www.eggheads.org/bugs.html");
      dprintf(-x, "Full Patch List: %s\n", egg_xtra);
      dprintf(-x, "Context: ");
      cx_ptr = cx_ptr & 15;
      for (y = ((cx_ptr + 1) & 15); y != cx_ptr; y = ((y + 1) & 15))
	dprintf(-x, "%s/%d,\n         ", cx_file[y], cx_line[y]);
      dprintf(-x, "%s/%d\n\n", cx_file[y], cx_line[y]);
      killsock(x);
      close(x);
    }
    exit(1);			/* Dont even try & tell people about, that may
				   have caused the fault last time. */
  } else
    nested_debug = 1;
  putlog(LOG_MISC, "*", "* Last context: %s/%d [%s]", cx_file[cx_ptr],
	 cx_line[cx_ptr], cx_note[cx_ptr][0] ? cx_note[cx_ptr] : "");
  putlog(LOG_MISC, "*", "* Please REPORT this BUG!");
  putlog(LOG_MISC, "*", "* Check doc/BUG-REPORT on how to do so.");
  x = creat("DEBUG", 0644);
  setsock(x, SOCK_NONSOCK);
  if (x < 0) {
    putlog(LOG_MISC, "*", "* Failed to write DEBUG");
  } else {
    strcpy(s, ctime(&now));
    dprintf(-x, "Debug (%s) written %s", ver, s);
    dprintf(-x, "Full Patch List: %s\n", egg_xtra);
#ifdef STATIC
    dprintf(-x, "STATICALLY LINKED\n");
#endif
    strcpy(s, "info library");
    if (interp && (Tcl_Eval(interp, s) == TCL_OK))
      dprintf(-x, "Using tcl library: %s (header version %s)\n",
	      interp->result, TCL_VERSION);
    dprintf(-x, "Compile flags: %s\n", CCFLAGS);
    dprintf(-x, "Link flags   : %s\n", LDFLAGS);
    dprintf(-x, "Strip flags  : %s\n", STRIPFLAGS);
    dprintf(-x, "Context: ");
    cx_ptr = cx_ptr & 15;
    for (y = ((cx_ptr + 1) & 15); y != cx_ptr; y = ((y + 1) & 15))
      dprintf(-x, "%s/%d, [%s]\n         ", cx_file[y], cx_line[y],
	      (cx_note[y][0]) ? cx_note[y] : "");
    dprintf(-x, "%s/%d [%s]\n\n", cx_file[cx_ptr], cx_line[cx_ptr],
	    (cx_note[cx_ptr][0]) ? cx_note[cx_ptr] : "");
    tell_dcc(-x);
    dprintf(-x, "\n");
    debug_mem_to_dcc(-x);
    killsock(x);
    close(x);
    putlog(LOG_MISC, "*", "* Wrote DEBUG");
  }
}
#endif

static void got_bus(int z)
{
#ifdef DEBUG_CONTEXT
  write_debug();
#endif
  fatal("BUS ERROR -- CRASHING!", 1);
#ifdef SA_RESETHAND
  kill(getpid(), SIGBUS);
#else
  exit(1);
#endif
}

static void got_segv(int z)
{
#ifdef DEBUG_CONTEXT
  write_debug();
#endif
  fatal("SEGMENT VIOLATION -- CRASHING!", 1);
#ifdef SA_RESETHAND
  kill(getpid(), SIGSEGV);
#else
  exit(1);
#endif
}

static void got_fpe(int z)
{
#ifdef DEBUG_CONTEXT
  write_debug();
#endif
  fatal("FLOATING POINT ERROR -- CRASHING!", 0);
}

static void got_term(int z)
{
  write_userfile(-1);
  check_tcl_event("sigterm");
  if (die_on_sigterm) {
    botnet_send_chat(-1, botnetnick, "ACK, I've been terminated!");
    fatal("TERMINATE SIGNAL -- SIGNING OFF", 0);
  } else {
    putlog(LOG_MISC, "*", "RECEIVED TERMINATE SIGNAL (IGNORING)");
  }
}

static void got_quit(int z)
{
  check_tcl_event("sigquit");
  putlog(LOG_MISC, "*", "RECEIVED QUIT SIGNAL (IGNORING)");
  return;
}

static void got_hup(int z)
{
  write_userfile(-1);
  check_tcl_event("sighup");
  if (die_on_sighup) {
    fatal("HANGUP SIGNAL -- SIGNING OFF", 0);
  } else
    putlog(LOG_MISC, "*", "Received HUP signal: rehashing...");
  do_restart = -2;
  return;
}

/* A call to resolver (gethostbyname, etc) timed out
 */
static void got_alarm(int z)
{
  longjmp(alarmret, 1);

  /* -Never reached- */
}

/* Got ILL signal -- log context and continue
 */
static void got_ill(int z)
{
  check_tcl_event("sigill");
#ifdef DEBUG_CONTEXT
  putlog(LOG_MISC, "*", "* Context: %s/%d [%s]", cx_file[cx_ptr],
	 cx_line[cx_ptr], (cx_note[cx_ptr][0]) ? cx_note[cx_ptr] : "");
#endif
}

#ifdef DEBUG_CONTEXT
/* Context */
void eggContext(char *file, int line, char *module)
{
  char x[31], *p;

  p = strrchr(file, '/');
  if (!module) {
    strncpy(x, p ? p + 1 : file, 30);
    x[30] = 0;
  } else
    egg_snprintf(x, 31, "%s:%s", module, p ? p + 1 : file);
  cx_ptr = ((cx_ptr + 1) & 15);
  strcpy(cx_file[cx_ptr], x);
  cx_line[cx_ptr] = line;
  cx_note[cx_ptr][0] = 0;
}

/* Called from the ContextNote macro.
 */
void eggContextNote(char *file, int line, char *module, char *note)
{
  char x[31], *p;

  p = strrchr(file, '/');
  if (!module) {
    strncpy(x, p ? p + 1 : file, 30);
    x[30] = 0;
  } else
    egg_snprintf(x, 31, "%s:%s", module, p ? p + 1 : file);
  cx_ptr = ((cx_ptr + 1) & 15);
  strcpy(cx_file[cx_ptr], x);
  cx_line[cx_ptr] = line;
  strncpy(cx_note[cx_ptr], note, 255);
  cx_note[cx_ptr][255] = 0;
}
#endif

#ifdef DEBUG_ASSERT
/* Called from the Assert macro.
 */
void eggAssert(char *file, int line, char *module, int expr)
{
  if (!(expr)) {
#ifdef DEBUG_CONTEXT
    write_debug();
#endif
    if (!module) {
      putlog(LOG_MISC, "*", "* In file %s, line %u", file, line);
    } else {
      putlog(LOG_MISC, "*", "* In file %s:%s, line %u", module, file, line);
    }
    fatal("ASSERT FAILED -- CRASHING!", 1);
  }
}
#endif

static void do_arg(char *s)
{
  int i;

  if (s[0] == '-')
    for (i = 1; i < strlen(s); i++) {
      if (s[i] == 'n')
	backgrd = 0;
      if (s[i] == 'c') {
	con_chan = 1;
	term_z = 0;
      }
      if (s[i] == 't') {
	con_chan = 0;
	term_z = 1;
      }
      if (s[i] == 'm')
	make_userfile = 1;
      if (s[i] == 'v') {
	char x[256], *z = x;

	strcpy(x, egg_version);
	newsplit(&z);
	newsplit(&z);
	printf("%s\n", version);
	if (z[0])
	  printf("  (patches: %s)\n", z);
	exit(0);
      }
      if (s[i] == 'h') {
	printf("\n%s\n\n", version);
	printf(EGG_USAGE);
	printf("\n");
	exit(0);
      }
  } else
    strcpy(configfile, s);
}

void backup_userfile()
{
  char s[150];

  putlog(LOG_MISC, "*", USERF_BACKUP);
  strcpy(s, userfile);
  strcat(s, "~bak");
  copyfile(userfile, s);
}

/* Timer info */
static int		lastmin = 99;
static time_t		then;
static struct tm	nowtm;

/* Called once a second.
 *
 * Note:  Try to not put any Context lines in here (guppy 21Mar2000).
 */
static void core_secondly()
{
  static int cnt = 0;
  int miltime;

  do_check_timers(&utimer);	/* Secondly timers */
  cnt++;
  if (cnt >= 10) {		/* Every 10 seconds */
    cnt = 0;
    check_expired_dcc();
    if (con_chan && !backgrd) {
      dprintf(DP_STDOUT, "\033[2J\033[1;1H");
      tell_verbose_status(DP_STDOUT);
      do_module_report(DP_STDOUT, 0, "server");
      do_module_report(DP_STDOUT, 0, "channels");
      tell_mem_status_dcc(DP_STDOUT);
    }
  }
  egg_memcpy(&nowtm, localtime(&now), sizeof(struct tm));
  if (nowtm.tm_min != lastmin) {
    int i = 0;

    /* Once a minute */
    lastmin = (lastmin + 1) % 60;
    call_hook(HOOK_MINUTELY);
    check_expired_ignores();
    autolink_cycle(NULL);	/* Attempt autolinks */
    /* In case for some reason more than 1 min has passed: */
    while (nowtm.tm_min != lastmin) {
      /* Timer drift, dammit */
      debug2("timer: drift (lastmin=%d, now=%d)", lastmin, nowtm.tm_min);
      i++;
      lastmin = (lastmin + 1) % 60;
      call_hook(HOOK_MINUTELY);
    }
    if (i > 1)
      putlog(LOG_MISC, "*", "(!) timer drift -- spun %d minutes", i);
    miltime = (nowtm.tm_hour * 100) + (nowtm.tm_min);
    if (((int) (nowtm.tm_min / 5) * 5) == (nowtm.tm_min)) {	/* 5 min */
      call_hook(HOOK_5MINUTELY);
      check_botnet_pings();
      if (quick_logs == 0) {
	flushlogs();
	check_logsize();
      }
      if (miltime == 0) {	/* At midnight */
	char s[128];
	int j;

	s[my_strcpy(s, ctime(&now)) - 1] = 0;
	putlog(LOG_ALL, "*", "--- %.11s%s", s, s + 20);
	backup_userfile();
	for (j = 0; j < max_logs; j++) {
	  if (logs[j].filename != NULL && logs[j].f != NULL) {
	    fclose(logs[j].f);
	    logs[j].f = NULL;
	  }
	}
      }
    }
    if (nowtm.tm_min == notify_users_at)
      call_hook(HOOK_HOURLY);
    /* These no longer need checking since they are all check vs minutely
     * settings and we only get this far on the minute.
     */
    if (miltime == switch_logfiles_at) {
      call_hook(HOOK_DAILY);
      if (!keep_all_logs) {
	putlog(LOG_MISC, "*", MISC_LOGSWITCH);
	for (i = 0; i < max_logs; i++)
	  if (logs[i].filename) {
	    char s[1024];

	    if (logs[i].f) {
	      fclose(logs[i].f);
	      logs[i].f = NULL;
	    }
	    simple_sprintf(s, "%s.yesterday", logs[i].filename);
	    unlink(s);
	    movefile(logs[i].filename, s);
	  }
      }
    }
  }
}

static void core_minutely()
{
  check_tcl_time(&nowtm);
  do_check_timers(&timer);
  if (quick_logs != 0) {
    flushlogs();
    check_logsize();
  }
}

static void core_hourly()
{
  Context;
  write_userfile(-1);
}

static void event_rehash()
{
  Context;
  check_tcl_event("rehash");
}

static void event_prerehash()
{
  Context;
  check_tcl_event("prerehash");
}

static void event_save()
{
  Context;
  check_tcl_event("save");
}

static void event_logfile()
{
  Context;
  check_tcl_event("logfile");
}

static void event_resettraffic()
{
  Context;
  otraffic_irc += otraffic_irc_today;
  itraffic_irc += itraffic_irc_today;
  otraffic_bn += otraffic_bn_today;
  itraffic_bn += itraffic_bn_today;
  otraffic_dcc += otraffic_dcc_today;
  itraffic_dcc += itraffic_dcc_today;
  otraffic_unknown += otraffic_unknown_today;
  itraffic_unknown += itraffic_unknown_today;
  otraffic_trans += otraffic_trans_today;
  itraffic_trans += itraffic_trans_today;
  otraffic_irc_today = otraffic_bn_today = 0;
  otraffic_dcc_today = otraffic_unknown_today = 0;
  itraffic_irc_today = itraffic_bn_today = 0;
  itraffic_dcc_today = itraffic_unknown_today = 0;
  itraffic_trans_today = otraffic_trans_today = 0;
}

void kill_tcl();
extern module_entry *module_list;
void restart_chons();

#ifdef STATIC
void check_static(char *, char *(*)());

#include "mod/static.h"
#endif
int init_mem(), init_dcc_max(), init_userent(), init_misc(), init_bots(),
 init_net(), init_modules(), init_tcl(int, char **),
 init_language(int);

void patch(char *str)
{
  char *p = strchr(egg_version, '+');

  if (!p)
    p = &egg_version[strlen(egg_version)];
  sprintf(p, "+%s", str);
  egg_numver++;
  sprintf(&egg_xtra[strlen(egg_xtra)], " %s", str);
}

int main(int argc, char **argv)
{
  int xx, i;
  char buf[520], s[520];
  FILE *f;
  struct sigaction sv;
  struct chanset_t *chan;

#ifdef DEBUG_MEM
  /* Make sure it can write core, if you make debug. Else it's pretty
   * useless (dw)
   */
  {
#include <sys/resource.h>
    struct rlimit cdlim;

    cdlim.rlim_cur = RLIM_INFINITY;
    cdlim.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &cdlim);
  }
#endif

  /* Initialise context list */
  for (i = 0; i < 16; i++)
    Context;

#include "patch.h"
  /* Version info! */
  sprintf(ver, "eggdrop v%s", egg_version);
  sprintf(version, "Eggdrop v%s  (c)1997 Robey Pointer (c)1999, 2000  Eggheads",
     egg_version);
  /* Now add on the patchlevel (for Tcl) */
  sprintf(&egg_version[strlen(egg_version)], " %u", egg_numver);
  strcat(egg_version, egg_xtra);
  Context;
#ifdef STOP_UAC
  {
    int nvpair[2];

    nvpair[0] = SSIN_UACPROC;
    nvpair[1] = UAC_NOPRINT;
    setsysinfo(SSI_NVPAIRS, (char *) nvpair, 1, NULL, 0);
  }
#endif

  /* Set up error traps: */
  sv.sa_handler = got_bus;
  sigemptyset(&sv.sa_mask);
#ifdef SA_RESETHAND
  sv.sa_flags = SA_RESETHAND;
#else
  sv.sa_flags = 0;
#endif
  sigaction(SIGBUS, &sv, NULL);
  sv.sa_handler = got_segv;
  sigaction(SIGSEGV, &sv, NULL);
#ifdef SA_RESETHAND
  sv.sa_flags = 0;
#endif
  sv.sa_handler = got_fpe;
  sigaction(SIGFPE, &sv, NULL);
  sv.sa_handler = got_term;
  sigaction(SIGTERM, &sv, NULL);
  sv.sa_handler = got_hup;
  sigaction(SIGHUP, &sv, NULL);
  sv.sa_handler = got_quit;
  sigaction(SIGQUIT, &sv, NULL);
  sv.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sv, NULL);
  sv.sa_handler = got_ill;
  sigaction(SIGILL, &sv, NULL);
  sv.sa_handler = got_alarm;
  sigaction(SIGALRM, &sv, NULL);

  /* Initialize variables and stuff */
  now = time(NULL);
  chanset = NULL;
  egg_memcpy(&nowtm, localtime(&now), sizeof(struct tm));
  lastmin = nowtm.tm_min;
  srandom(now);
  init_mem();
  init_language(1);
  if (argc > 1)
    for (i = 1; i < argc; i++)
      do_arg(argv[i]);
  printf("\n%s\n", version);
  init_dcc_max();
  init_userent();
  init_misc();
  init_bots();
  init_net();
  init_modules();
  init_tcl(argc, argv);
  init_language(0);
#ifdef STATIC
  link_statics();
#endif
  Context;
  strcpy(s, ctime(&now));
  s[strlen(s) - 1] = 0;
  strcpy(&s[11], &s[20]);
  putlog(LOG_ALL, "*", "--- Loading %s (%s)", ver, s);
  chanprog();
  Context;
  if (encrypt_pass == 0) {
    printf(MOD_NOCRYPT);
    exit(1);
  }
  i = 0;
  for (chan = chanset; chan; chan = chan->next)
    i++;
  putlog(LOG_MISC, "*", "=== %s: %d channels, %d users.",
	 botnetnick, i, count_users(userlist));
  cache_miss = 0;
  cache_hit = 0;
  sprintf(pid_file, "pid.%s", botnetnick);
  Context;

  /* Check for pre-existing eggdrop! */
  f = fopen(pid_file, "r");
  if (f != NULL) {
    fgets(s, 10, f);
    xx = atoi(s);
    kill(xx, SIGCHLD);		/* Meaningless kill to determine if pid
				   is used */
    if (errno != ESRCH) {
      printf(EGG_RUNNING1, origbotname);
      printf(EGG_RUNNING2, pid_file);
      exit(1);
    }
  }
  Context;

  /* Move into background? */
  if (backgrd) {
#ifndef CYGWIN_HACKS
    xx = fork();
    if (xx == -1)
      fatal("CANNOT FORK PROCESS.", 0);
    if (xx != 0) {
      FILE *fp;

      /* Need to attempt to write pid now, not later */
      unlink(pid_file);
      fp = fopen(pid_file, "w");
      if (fp != NULL) {
	fprintf(fp, "%u\n", xx);
	if (fflush(fp)) {
	  /* Kill bot incase a botchk is run from crond */
	  printf(EGG_NOWRITE, pid_file);
	  printf("  Try freeing some disk space\n");
	  fclose(fp);
	  unlink(pid_file);
	  exit(1);
	}
	fclose(fp);
      } else
	printf(EGG_NOWRITE, pid_file);
      printf("Launched into the background  (pid: %d)\n\n", xx);
#if HAVE_SETPGID
      setpgid(xx, xx);
#endif
      exit(0);
    }
  } else {			/* !backgrd */
#endif
    xx = getpid();
    if (xx != 0) {
      FILE *fp;

      /* Write pid to file */
      unlink(pid_file);
      fp = fopen(pid_file, "w");
      if (fp != NULL) {
        fprintf(fp, "%u\n", xx);
        if (fflush(fp)) {
	  /* Let the bot live since this doesn't appear to be a botchk */
	  printf(EGG_NOWRITE, pid_file);
	  fclose(fp);
	  unlink(pid_file);
        } else
 	  fclose(fp);
      } else
        printf(EGG_NOWRITE, pid_file);
#ifdef CYGWIN_HACKS
      printf("Launched  (pid: %d)\n\n", xx);
#endif
    }
  }

  use_stderr = 0;		/* Stop writing to stderr now */
  if (backgrd) {
    /* Ok, try to disassociate from controlling terminal (finger cross) */
#if HAVE_SETPGID && !defined(CYGWIN_HACKS)
    setpgid(0, 0);
#endif
    /* Tcl wants the stdin, stdout and stderr file handles kept open. */
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
#ifdef CYGWIN_HACKS
    FreeConsole();
#endif
  }

  /* Terminal emulating dcc chat */
  if ((!backgrd) && (term_z)) {
    int n = new_dcc(&DCC_CHAT, sizeof(struct chat_info));

    dcc[n].addr = iptolong(getmyip());
    dcc[n].sock = STDOUT;
    dcc[n].timeval = now;
    dcc[n].u.chat->con_flags = conmask;
    dcc[n].u.chat->strip_flags = STRIP_ALL;
    dcc[n].status = STAT_ECHO;
    strcpy(dcc[n].nick, "HQ");
    strcpy(dcc[n].host, "llama@console");
    dcc[n].user = get_user_by_handle(userlist, "HQ");
    /* Make sure there's an innocuous HQ user if needed */
    if (!dcc[n].user) {
      userlist = adduser(userlist, "HQ", "none", "-", USER_PARTY);
      dcc[n].user = get_user_by_handle(userlist, "HQ");
    }
    setsock(STDOUT, 0);		/* Entry in net table */
    dprintf(n, "\n### ENTERING DCC CHAT SIMULATION ###\n\n");
    dcc_chatter(n);
  }

  then = now;
  online_since = now;
  autolink_cycle(NULL);		/* Hurry and connect to tandem bots */
  add_help_reference("cmds1.help");
  add_help_reference("cmds2.help");
  add_help_reference("core.help");
  add_hook(HOOK_SECONDLY, (Function) core_secondly);
  add_hook(HOOK_MINUTELY, (Function) core_minutely);
  add_hook(HOOK_HOURLY, (Function) core_hourly);
  add_hook(HOOK_REHASH, (Function) event_rehash);
  add_hook(HOOK_PRE_REHASH, (Function) event_prerehash);
  add_hook(HOOK_USERFILE, (Function) event_save);
  add_hook(HOOK_DAILY, (Function) event_logfile);
  add_hook(HOOK_DAILY, (Function) event_resettraffic);

  debug0("main: entering loop");
  while (1) {
    int socket_cleanup = 0;

    Context;
#if !defined(HAVE_PRE7_5_TCL) && !defined(HAVE_TCL_THREADS)
    /* Process a single tcl event */
    Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT);
#endif
    /* Lets move some of this here, reducing the numer of actual
     * calls to periodic_timers
     */
    now = time(NULL);
    random();			/* Woop, lets really jumble things */
    if (now != then) {		/* Once a second */
      call_hook(HOOK_SECONDLY);
      then = now;
    }
    Context;
    /* Only do this every so often */
    if (!socket_cleanup) {
      dcc_remove_lost();
      /* Check for server or dcc activity */
      dequeue_sockets();
      socket_cleanup = 5;
    } else
      socket_cleanup--;
    xx = sockgets(buf, &i);
    if (xx >= 0) {		/* Non-error */
      int idx;

      Context;
      for (idx = 0; idx < dcc_total; idx++)
	if (dcc[idx].sock == xx) {
	  if (dcc[idx].type && dcc[idx].type->activity) {
	    /* Traffic stats */
	    if (dcc[idx].type->name) {
	      if (!strncmp(dcc[idx].type->name, "BOT", 3))
		itraffic_bn_today += strlen(buf) + 1;
	      else if (!strcmp(dcc[idx].type->name, "SERVER"))
		itraffic_irc_today += strlen(buf) + 1;
	      else if (!strncmp(dcc[idx].type->name, "CHAT", 4))
		itraffic_dcc_today += strlen(buf) + 1;
	      else if (!strncmp(dcc[idx].type->name, "FILES", 5))
		itraffic_dcc_today += strlen(buf) + 1;
	      else if (!strcmp(dcc[idx].type->name, "SEND"))
		itraffic_trans_today += strlen(buf) + 1;
	      else if (!strncmp(dcc[idx].type->name, "GET", 3))
		itraffic_trans_today += strlen(buf) + 1;
	      else
		itraffic_unknown_today += strlen(buf) + 1;
	    }
	    dcc[idx].type->activity(idx, buf, i);      
	  } else
	    putlog(LOG_MISC, "*",
		   "!!! untrapped dcc activity: type %s, sock %d",
		   dcc[idx].type->name, dcc[idx].sock);
	  break;
	}
    } else if (xx == -1) {	/* EOF from someone */
      int idx;

      if ((i == STDOUT) && !backgrd)
	fatal("END OF FILE ON TERMINAL", 0);
      Context;
      for (idx = 0; idx < dcc_total; idx++)
	if (dcc[idx].sock == i) {
	  if (dcc[idx].type && dcc[idx].type->eof)
	    dcc[idx].type->eof(idx);
	  else {
	    putlog(LOG_MISC, "*",
		   "*** ATTENTION: DEAD SOCKET (%d) OF TYPE %s UNTRAPPED",
		   i, dcc[idx].type ? dcc[idx].type->name : "*UNKNOWN*");
	    killsock(i);
	    lostdcc(idx);
	  }
	  idx = dcc_total + 1;
	}
      if (idx == dcc_total) {
	putlog(LOG_MISC, "*",
	       "(@) EOF socket %d, not a dcc socket, not anything.", i);
	close(i);
	killsock(i);
      }
    } else if ((xx == -2) && (errno != EINTR)) {	/* select() error */
      Context;
      putlog(LOG_MISC, "*", "* Socket error #%d; recovering.", errno);
      for (i = 0; i < dcc_total; i++) {
	if ((fcntl(dcc[i].sock, F_GETFD, 0) == -1) && (errno = EBADF)) {
	  putlog(LOG_MISC, "*",
		 "DCC socket %d (type %d, name '%s') expired -- pfft",
		 dcc[i].sock, dcc[i].type, dcc[i].nick);
	  killsock(dcc[i].sock);
	  lostdcc(i);
	  i--;
	}
      }
    } else if (xx == (-3)) {
      call_hook(HOOK_IDLE);
      socket_cleanup = 0;	/* If we've been idle, cleanup & flush */
    }

    if (do_restart) {
      if (do_restart == -2)
	rehash();
      else {
	/* Unload as many modules as possible */
	int f = 1;
	module_entry *p;
	Function x;
	char xx[256];

	while (f) {
	  f = 0;
	  for (p = module_list; p != NULL; p = p->next) {
	    dependancy *d = dependancy_list;
	    int ok = 1;

	    while (ok && d) {
	      if (d->needed == p)
		ok = 0;
	      d = d->next;
	    }
	    if (ok) {
	      strcpy(xx, p->name);
	      if (module_unload(xx, origbotname) == NULL) {
		f = 1;
		break;
	      }
	    }
	  }
	}
	p = module_list;
	if (p && p->next && p->next->next)
	  /* Should be only 2 modules now -
	   * blowfish & eggdrop
	   */
	  putlog(LOG_MISC, "*", MOD_STAGNANT);
	Context;
	flushlogs();
	Context;
	kill_tcl();
	init_tcl(argc, argv);
	init_language(0);
	/* We expect the encryption module as the current module pointed
	 * to by `module_list'.
	 */
	x = p->funcs[MODCALL_START];
	/* `NULL' indicates that we just recently restarted. The module
	 * is expected to re-initialise as needed.
	 */
	x(NULL);
	rehash();
	restart_chons();
      }
      do_restart = 0;
    }
  }
}
