/* 
 * server.c -- part of server.mod
 *   basic irc server support
 * 
 * $Id: server.c,v 1.48 2000/08/11 22:42:21 fabian Exp $
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

#define MODULE_NAME "server"
#define MAKING_SERVER
#include "src/mod/module.h"
#include "server.h"
#include <netdb.h>

static Function *global = NULL;

static int ctcp_mode;
static int serv;		/* sock # of server currently */
static int strict_host;		/* strict masking of hosts ? */
static char newserver[121];	/* new server? */
static int newserverport;	/* new server port? */
static char newserverpass[121];	/* new server password? */
static time_t trying_server;	/* trying to connect to a server right now? */
static int server_lag;		/* how lagged (in seconds) is the server? */
static char altnick[NICKLEN];	/* possible alternate nickname to use */
static char raltnick[NICKLEN];	/* random nick created from altnick */
static int curserv;		/* current position in server list: */
static int flud_thr;		/* msg flood threshold */
static int flud_time;		/* msg flood time */
static int flud_ctcp_thr;	/* ctcp flood threshold */
static int flud_ctcp_time;	/* ctcp flood time */
static char initserver[121];	/* what, if anything, to send to the
				   server on connection */
static char botuserhost[121];	/* bot's user@host (refreshed whenever the
				   bot joins a channel) */
				/* may not be correct user@host BUT it's
				   how the server sees it */
static int keepnick;		/* keep trying to regain my intended
				   nickname? */
static int nick_juped = 0;	/* True if origbotname is juped(RPL437) (dw) */
static int check_stoned;	/* Check for a stoned server? */
static int serverror_quit;	/* Disconnect from server if ERROR
				   messages received? */
static int quiet_reject;	/* Quietly reject dcc chat or sends from
				   users without access? */
static int waiting_for_awake;	/* set when i unidle myself, cleared when
				   i get the response */
static time_t server_online;	/* server connection time */
static time_t server_cycle_wait;	/* seconds to wait before
					   re-beginning the server list */
static char botrealname[121];	/* realname of bot */
static int min_servs;		/* minimum number of servers to be around */
static int server_timeout;	/* server timeout for connecting */
static int never_give_up;	/* never give up when connecting to servers? */
static int strict_servernames;	/* don't update server list */
static struct server_list *serverlist;	/* old-style queue, still used by
					   server list */
static int cycle_time;		/* cycle time till next server connect */
static int default_port;	/* default IRC port */
static char oldnick[NICKLEN];	/* previous nickname *before* rehash */
static int trigger_on_ignore;	/* trigger bindings if user is ignored ? */
static int answer_ctcp;		/* answer how many stacked ctcp's ? */
static int lowercase_ctcp;	/* answer lowercase CTCP's (non-standard) */
static char bothost[81];	/* dont mind me, Im stupid */
static int check_mode_r;	/* check for IRCNET +r modes */
static int use_ison;		/* arthur2 static */
static int net_type;
static char connectserver[121];	/* what, if anything, to do before connect
				   to the server */
static int resolvserv;		/* in the process of resolving a server host */
static int double_mode;		/* allow a msgs to be twice in a queue? */
static int double_server;
static int double_help;
static int double_warned;
static int lastpingtime;	/* IRCNet LAGmeter support -- drummer */
static char stackablecmds[511];
static time_t last_time;
static int use_penalties;
static int use_fastdeq;
static int nick_len;		/* Maximal nick length allowed on the
				   network. */
static int kick_method;
static int optimize_kicks;
static int lagged;
static int lagchecktype;
static char *lagcheckstring;
static char *lagcheckstring2;
static int use_lagcheck;


static p_tcl_bind_list H_wall, H_raw, H_notc, H_msgm, H_msg, H_flud,
		       H_ctcr, H_ctcp;

static void empty_msgq(void);
static void next_server(int *, char *, unsigned int *, char *);
static void disconnect_server(int);
static char *get_altbotnick(void);
static int calc_penalty(char *);
static int fast_deq(int);
static char *splitnicks(char **);
static void check_queues(char *, char *);
static void parse_q(struct msgq_head *, char *, char *);
static void purge_kicks(struct msgq_head *);
static int deq_kick(int);
static void check_lag(char *);
static void check_notlagged(char *);
static void msgq_clear(struct msgq_head *qh);

#include "servmsg.c"

#define MAXPENALTY 10

/* Number of seconds to wait between transmitting queued lines to the server
 * lower this value at your own risk.  ircd is known to start flood control
 * at 512 bytes/2 seconds.
 */
#define msgrate 2

/* Maximum messages to store in each queue. */
static int maxqmsg;
static struct msgq_head mq, hq, modeq;
static int burst;

#include "cmdsserv.c"
#include "tclserv.c"


/*
 *     Bot server queues
 */

/* Called periodically to shove out another queued item.
 * 
 * 'mode' queue gets priority now.
 * 
 * Most servers will allow 'busts' of upto 5 msgs, so let's put something
 * in to support flushing modeq a little faster if possible.
 * Will send upto 4 msgs from modeq, and then send 1 msg every time
 * it will *not* send anything from hq until the 'burst' value drops
 * down to 0 again (allowing a sudden mq flood to sneak through).
 */
static void deq_msg()
{
  struct msgq *q;
  int ok = 0;

  /* now < last_time tested 'cause clock adjustments could mess it up */
  if ((now - last_time) >= msgrate || now < (last_time - 90)) {
    last_time = now;
    if (burst > 0)
      burst--;
    ok = 1;
  }
  if (serv < 0)
    return;
  /* Send upto 4 msgs to server if the *critical queue* has anything in it */
  if (modeq.head) {
    while (modeq.head && (burst < 4) && ((last_time - now) < MAXPENALTY)) {
      if (lagged && burst >= 3)
        return;
      if (deq_kick(DP_MODE)) {
        burst++;
        continue;
      }
      if (!modeq.head)
        break;
      if (fast_deq(DP_MODE)) {
        burst++;
        continue;
      }
      tputs(serv, modeq.head->msg, modeq.head->len);
      if (debug_output)
        putlog(LOG_SRVOUT, "*", "[m->] %s", modeq.head->msg);
      modeq.tot--;
      check_lag(modeq.head->msg);
      last_time += calc_penalty(modeq.head->msg);
      q = modeq.head->next;
      nfree(modeq.head->msg);
      nfree(modeq.head);
      modeq.head = q;
      burst++;
    }
    if (!modeq.head)
      modeq.last = 0;
    return;
  }
  /* Send something from the normal msg q even if we're slightly bursting */
  if (burst > 1 || lagged)
    return;
  if (mq.head) {
    burst++;
    if (deq_kick(DP_SERVER))
      return;
    if (fast_deq(DP_SERVER))
      return;
    tputs(serv, mq.head->msg, mq.head->len);
    if (debug_output)
      putlog(LOG_SRVOUT, "*", "[s->] %s", mq.head->msg);
    mq.tot--;
    check_lag(mq.head->msg);
    last_time += calc_penalty(mq.head->msg);
    q = mq.head->next;
    nfree(mq.head->msg);
    nfree(mq.head);
    mq.head = q;
    if (!mq.head)
      mq.last = NULL;
    return;
  }
  /* Never send anything from the help queue unless everything else is
   * finished.
   */
  if (!hq.head || burst || !ok)
    return;
  if (deq_kick(DP_HELP))
    return;
  if (fast_deq(DP_HELP))
    return;
  tputs(serv, hq.head->msg, hq.head->len);
  if (debug_output)
    putlog(LOG_SRVOUT, "*", "[h->] %s", hq.head->msg);
  hq.tot--;
  check_lag(hq.head->msg);
  last_time += calc_penalty(hq.head->msg);
  q = hq.head->next;
  nfree(hq.head->msg);
  nfree(hq.head);
  hq.head = q;
  if (!hq.head)
    hq.last = NULL;
}

static void check_lag(char *buf)
{
  char msgstr[511], *msg, *cmd, *chans, *nicks, *nick, *ch, *par, pm, mode,
       *modes;
  struct chanset_t *cs;
  memberlist *m;

  if (lagged || !use_lagcheck)
    return;
  if (lagcheckstring)
    free_null(lagcheckstring);
  if (lagcheckstring2)
    free_null(lagcheckstring2);
  lagchecktype = 0;
  strncpyz(msgstr, buf, sizeof msgstr);
  msg = msgstr;
  if (msg[strlen(msg) - 1] == '\n')
    msg[strlen(msg) - 1] = 0;
  cmd = newsplit(&msg);
  if (!egg_strcasecmp(cmd, "KICK")) {
    chans = newsplit(&msg);
    nicks = newsplit(&msg);
    nick = nicks;
    while (strlen(nicks) > 0)
      nick = splitnicks(&nicks);
    lagcheckstring = nmalloc(strlen(nick) + 1);
    lagcheckstring2 = nmalloc(strlen(nick) + 1);
    strcpy(lagcheckstring2, nick);
    strcpy(lagcheckstring, nick);
    lagged = 1;
    lagchecktype = LC_KICK;
    debug2("Starting lagcheck using KICK %s (%s)", nick, buf);
  } else if (!egg_strcasecmp(cmd, "MODE")) {
    if (net_type != NETT_IRCNET)
      return; /* MODE-lagcheck is only usable on IRCNet */
    chans = newsplit(&msg);
    modes = newsplit(&msg);
    par = newsplit(&msg);
    ch = splitnicks(&chans);
    cs = findchan(ch);
    if (!cs) {
      debug0("I'm not on the target channel, aborting lagcheck");
      lagged = 0;
      return;
    }
    pm = modes[0];
    if ((pm != '+' && pm != '-') || (strlen(par) < 1)) {
      lagged = 0;
      return;
    }
    mode = modes[1];
    if (strchr("ov", mode)) {
      if (match_my_nick(par)) {
        debug0("I'm the target, aborting lagcheck.");
        lagged = 0;
        return;
      }
      m = ismember(cs, par);
      if (!m) {
        debug0("Target for o/v mode not on channel, aborting lagcheck.");
        lagged = 0;
        return;
      }
      if (m->split > 0) {
        debug0("Target for o/v mode is netsplitted, aborting lagcheck.");
        lagged = 0;
        return;
      }
      lagcheckstring = nmalloc(strlen(par) + 4);
      sprintf(lagcheckstring, "%c%c %s", pm, mode, par);
      lagged = 1;
      lagchecktype = LC_OVMODE;
      debug2("Starting lagcheck using MODE %s (%s)", lagcheckstring, buf);
    } else if (strchr("beI", mode)) {
      lagcheckstring = nmalloc(strlen(par) + 4);
      sprintf(lagcheckstring, "%c%c %s", pm, mode, par);
      lagged = 1;
      lagchecktype = LC_BEIMODE;
      debug2("Starting lagcheck using MODE %s (%s)", lagcheckstring, buf);
    } else {
      lagged = 0;
    }
  }
}

static void check_notlagged(char *buf)
{
  if (!lagged || lagchecktype == LC_KICK)
    return;
  debug1("check_notlagged: %s", buf);
  if (!lagcheckstring) {
    lagged = 0;
    return;
  }
  if (buf[0] == '+' || buf[0] == '-') {
    if (!egg_strcasecmp(lagcheckstring, buf)) {
      debug1("MODE %s processed, I guess I'm not lagged", lagcheckstring);
      free_null(lagcheckstring);
      lagged = 0;
    }
  } else {
    if (!egg_strcasecmp(lagcheckstring + 3, buf)) {
      debug2("%s left, stopping lagcheck for %s", buf, lagcheckstring);
      free_null(lagcheckstring);
      lagged = 0;
    }
  }
}

static int calc_penalty(char * msg)
{
  char *cmd, *par1, *par2, *par3;
  register int penalty, i, ii;

  Context;
  if (!use_penalties &&
      net_type != NETT_UNDERNET && net_type != NETT_HYBRID_EFNET)
    return 0;
  if (msg[strlen(msg) - 1] == '\n')
    msg[strlen(msg) - 1] = '\0';
  cmd = newsplit(&msg);
  if (msg)
    i = strlen(msg);
  else
    i = strlen(cmd);
  last_time -= 2; /* undo eggdrop standard flood prot */
  if (net_type == NETT_UNDERNET || net_type == NETT_HYBRID_EFNET) {
    last_time += (2 + i / 120);
    return 0;
  }
  penalty = (1 + i / 100);
  if (!egg_strcasecmp(cmd, "KICK")) {
    par1 = newsplit(&msg); /* channel */
    par2 = newsplit(&msg); /* victim(s) */
    par3 = splitnicks(&par2);
    penalty++;
    while (strlen(par3) > 0) {
      par3 = splitnicks(&par2);
      penalty++;
    }
    ii = penalty;
    par3 = splitnicks(&par1);
    while (strlen(par1) > 0) {
      par3 = splitnicks(&par1);
      penalty += ii;
    }
  } else if (!egg_strcasecmp(cmd, "MODE")) {
    i = 0;
    par1 = newsplit(&msg); /* channel */
    par2 = newsplit(&msg); /* mode(s) */
    if (!strlen(par2))
      i++;
    while (strlen(par2) > 0) {
      if (strchr("ntimps", par2[0]))
        i += 3;
      else if (!strchr("+-", par2[0]))
        i += 1;
      par2++;
    }
    while (strlen(msg) > 0) {
      newsplit(&msg);
      i += 2;
    }
    ii = 0;
    while (strlen(par1) > 0) {
      splitnicks(&par1);
      ii++;
    }
    penalty += (ii * i);
  } else if (!egg_strcasecmp(cmd, "TOPIC")) {
    penalty++;
    par1 = newsplit(&msg); /* channel */
    par2 = newsplit(&msg); /* topic */
    if (strlen(par2) > 0) {  /* topic manipulation => 2 penalty points */
      penalty += 2;
      par3 = splitnicks(&par1);
      while (strlen(par1) > 0) {
        par3 = splitnicks(&par1);
        penalty += 2;
      }
    }
  } else if (!egg_strcasecmp(cmd, "PRIVMSG") ||
	     !egg_strcasecmp(cmd, "NOTICE")) {
    par1 = newsplit(&msg); /* channel(s)/nick(s) */
    /* Add one sec penalty for each recipient */
    while (strlen(par1) > 0) {
      splitnicks(&par1);
      penalty++;
    }
  } else if (!egg_strcasecmp(cmd, "WHO")) {
    par1 = newsplit(&msg); /* masks */
    par2 = par1;
    while (strlen(par1) > 0) {
      par2 = splitnicks(&par1);
      if (strlen(par2) > 4)   /* long WHO-masks receive less penalty */
        penalty += 3;
      else
        penalty += 5;
    }
  } else if (!egg_strcasecmp(cmd, "AWAY")) {
    if (strlen(msg) > 0)
      penalty += 2;
    else
      penalty += 1;
  } else if (!egg_strcasecmp(cmd, "INVITE")) {
    /* Successful invite receives 2 or 3 penalty points. Let's go
     * with the maximum.
     */
    penalty += 3;
  } else if (!egg_strcasecmp(cmd, "JOIN")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "PART")) {
    penalty += 4;
  } else if (!egg_strcasecmp(cmd, "VERSION")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "TIME")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "TRACE")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "NICK")) {
    penalty += 3;
  } else if (!egg_strcasecmp(cmd, "ISON")) {
    penalty += 1;
  } else if (!egg_strcasecmp(cmd, "WHOIS")) {
    penalty += 2;
  } else if (!egg_strcasecmp(cmd, "DNS")) {
    penalty += 2;
  } else {
    debug1("Unknown command %s, adding 1sec standard-penalty", cmd);
    penalty++;
  }
  /* Shouldn't happen, but you never know... */
  if (penalty > 99)
    penalty = 99;
  if (penalty < 2) {
    putlog(LOG_SRVOUT, "*", "Penalty < 2sec, that's impossible!");
    penalty = 2;
  }
  if (debug_output && penalty != 0)
    putlog(LOG_SRVOUT, "*", "Adding penalty: %i", penalty);
  return penalty;
}

char *splitnicks(char **rest)
{
  register char *o, *r;

  if (!rest)
    return *rest = "";
  o = *rest;
  while (*o == ' ')
    o++;
  r = o;
  while (*o && *o != ',')
    o++;
  if (*o)
    *o++ = 0;
  *rest = o;
  return r;
}

static int fast_deq(int which)
{
  struct msgq_head *h;
  struct msgq *m, *nm;
  char msgstr[511], nextmsgstr[511], tosend[511], victims[511], stackable[511],
       *msg, *nextmsg, *cmd, *nextcmd, *to, *nextto, *stckbl;
  int len, doit = 0, found = 0;

  Context;
  if (!use_fastdeq)
    return 0;
  switch (which) {
    case DP_MODE:
      h = &modeq;
      break;
    case DP_SERVER:
      h = &mq;
      break;
    case DP_HELP:
      h = &hq;
      break;
    default:
      return 0;
  }
  m = h->head;
  strncpyz(msgstr, m->msg, sizeof msgstr);
  msg = msgstr;
  cmd = newsplit(&msg);
  if (use_fastdeq > 1) {
    strncpyz(stackable, stackablecmds, sizeof stackable);
    stckbl = stackable;
    while (strlen(stckbl) > 0)
      if (!egg_strcasecmp(newsplit(&stckbl), cmd)) {
        found = 1;
        break;
      }
    /* If use_fastdeq is 2, only commands in the list should be stacked. */
    if (use_fastdeq == 2 && !found)
      return 0;
    /* If use_fastdeq is 3, only commands that are _not_ in the list
     * should be stacked.
     */
    if (use_fastdeq == 3 && found)
      return 0;
  }
  to = newsplit(&msg);
  len = strlen(to);
  if (to[len - 1] == '\n')
    to[len -1] = 0;
  simple_sprintf(victims, "%s", to);
  while (m) {
    nm = m->next;
    if (!nm)
      break;
    strncpyz(nextmsgstr, nm->msg, sizeof nextmsgstr);
    nextmsg = nextmsgstr;
    nextcmd = newsplit(&nextmsg);
    nextto = newsplit(&nextmsg);
    len = strlen(nextto);
    if (nextto[len - 1] == '\n')
      nextto[len - 1] = 0;
    if (!strcmp(cmd, nextcmd) && !strcmp(msg, nextmsg)
        && ((strlen(cmd) + strlen(victims) + strlen(nextto) + strlen(msg) + 2) < 510)) {
      simple_sprintf(victims, "%s,%s", victims, nextto);
      doit = 1;
      m->next = nm->next;
      if (!nm->next)
        h->last = m;
      nfree(nm->msg);
      nfree(nm);
      h->tot--;
    } else
      m = m->next;
  }
  if (doit) {
    simple_sprintf(tosend, "%s %s %s", cmd, victims, msg);
    len = strlen(tosend);
    tosend[len - 1] = '\n';
    tputs(serv, tosend, len);
    m = h->head->next;
    nfree(h->head->msg);
    nfree(h->head);
    h->head = m;
    if (!h->head)
      h->last = 0;
    h->tot--;
    if (debug_output) {
      tosend[len - 1] = 0;
      switch (which) {
        case DP_MODE:
          putlog(LOG_SRVOUT, "*", "[m=>] %s", tosend);
          break;
        case DP_SERVER:
          putlog(LOG_SRVOUT, "*", "[s=>] %s", tosend);
          break;
        case DP_HELP:
          putlog(LOG_SRVOUT, "*", "[h=>] %s", tosend);
          break;
      }
    }
    check_lag(tosend);
    last_time += calc_penalty(tosend);
    return 1;
  }
  Context;
  return 0;
}

static void check_queues(char *oldnick, char *newnick)
{
  char pm, mode;

  Context;
  if (optimize_kicks == 2 || use_lagcheck == 2) {
    if (modeq.head)
      parse_q(&modeq, oldnick, newnick);
    if (mq.head)
      parse_q(&mq, oldnick, newnick);
    if (hq.head)
      parse_q(&hq, oldnick, newnick);
  }
  if (lagged) {
    if (lagchecktype == LC_KICK) {
      if (!egg_strcasecmp(lagcheckstring, oldnick)) {
        nfree(lagcheckstring);
        lagcheckstring = nmalloc(strlen(newnick) + 1);
        strcpy(lagcheckstring, newnick);
      }
    } else if (!egg_strcasecmp(lagcheckstring + 3, oldnick)) {
      pm = lagcheckstring[0];
      mode = lagcheckstring[1];
      nfree(lagcheckstring);
      lagcheckstring = nmalloc(strlen(newnick) + 4);
      lagcheckstring[0] = pm;
      lagcheckstring[1] = mode;
      strcpy(lagcheckstring + 3, newnick);
    }
  }
  Context;
}

static void parse_q(struct msgq_head *q, char *oldnick, char *newnick)
{
  struct msgq *m, *lm = NULL;
  char buf[511], *msg, *nicks, *nick, *chan, newnicks[511], newmsg[511];
  int changed;

  Context;
  for (m = q->head; m;) {
    changed = 0;
    if (optimize_kicks == 2 && !egg_strncasecmp(m->msg, "KICK ", 5)) {
      newnicks[0] = 0;
      strncpyz(buf, m->msg, sizeof buf);
      if (buf[0] && (buf[strlen(buf)-1] == '\n'))
        buf[strlen(buf)-1] = '\0';
      msg = buf;
      newsplit(&msg);
      chan = newsplit(&msg);
      nicks = newsplit(&msg);
      while (strlen(nicks) > 0) {
        nick = splitnicks(&nicks);
        if (!egg_strcasecmp(nick, oldnick) &&
            ((9 + strlen(chan) + strlen(newnicks) + strlen(newnick) +
              strlen(nicks) + strlen(msg)) < 510)) {
          if (newnick)
            egg_snprintf(newnicks, sizeof newnicks, "%s,%s", newnicks, newnick);
          changed = 1;
        } else
          egg_snprintf(newnicks, sizeof newnicks, ",%s", nick);
      }
      egg_snprintf(newmsg, sizeof newmsg, "KICK %s %s %s\n", chan,
		   newnicks + 1, msg);
    } else if (use_lagcheck == 2 && !egg_strncasecmp(m->msg, "MODE ", 5)) {
      newnicks[0] = 0;
      strncpyz(buf, m->msg, sizeof buf);
      msg = buf;
      newsplit(&msg);
      /* XXX Why don't we do that in _one_ statement instead of two?  */
      egg_snprintf(newnicks, sizeof newnicks, "%s", newsplit(&msg));
      egg_snprintf(newnicks, sizeof newnicks, "%s %s", newnicks,
		   newsplit(&msg));

      while (strlen(msg) > 0) {
        nick = newsplit(&msg);
        if (!egg_strcasecmp(nick, oldnick) &&
            ((9 + strlen(newnicks) + strlen(newnick) +
              strlen(nicks) + strlen(msg)) < 510)) {
          if (newnick)
            egg_snprintf(newnicks, sizeof newnicks, "%s %s", newnicks, newnick);
          changed = 1;
        } else
          egg_snprintf(newnicks, sizeof newnicks, "%s %s", newnicks, nick);
      }
      egg_snprintf(newmsg, sizeof newmsg, "MODE %s", newnicks);
    }
    if (changed) {
      if (newnicks[0] == 0) {
        if (!lm)
          q->head = m->next;
        else
          lm->next = m->next;
        nfree(m->msg);
        nfree(m);
        m = lm;
        q->tot--;
        if (!q->head)
          q->last = 0;
      } else {
        nfree(m->msg);
        m->msg = nmalloc(strlen(newmsg) + 1);
        m->len = strlen(newmsg);
        strcpy(m->msg, newmsg);
      }
    }
    lm = m;
    if (m)
      m = m->next;
    else
      m = q->head;
  }
  Context;
}

static void purge_kicks(struct msgq_head *q)
{
  struct msgq *m, *lm = NULL;
  char buf[511], *reason, *nicks, *nick, *chan, newnicks[511],
       newmsg[511], chans[511], *chns, *ch;
  int changed, found;
  struct chanset_t *cs;

  Context;
  for (m = q->head; m;) {
    if (!egg_strncasecmp(m->msg, "KICK", 4)) {
      newnicks[0] = 0;
      changed = 0;
      strncpyz(buf, m->msg, sizeof buf);
      if (buf[0] && (buf[strlen(buf)-1] == '\n'))
        buf[strlen(buf)-1] = '\0';
      reason = buf;
      newsplit(&reason);
      chan = newsplit(&reason);
      nicks = newsplit(&reason);
      while (strlen(nicks) > 0) {
        found = 0;
        nick = splitnicks(&nicks);
        egg_snprintf(chans, sizeof chans, chan);
        chns = chans;
        while (strlen(chns) > 0) {
          ch = newsplit(&chns);
          cs = findchan(ch);
          if (!cs)
            continue;
          if (ismember(cs, nick))
            found = 1;
        }
        if (found)
          egg_snprintf(newnicks, sizeof newnicks, "%s,%s", newnicks, nick);
        else {
          putlog(LOG_SRVOUT, "*", "%s isn't on any target channel, removing "
		 "kick...", nick);
          changed = 1;
        }
      }
      if (changed) {
        if (newnicks[0] == 0) {
          if (!lm)
            q->head = m->next;
          else
            lm->next = m->next;
          nfree(m->msg);
          nfree(m);
          m = lm;
          q->tot--;
          if (!q->head)
            q->last = 0;
        } else {
          nfree(m->msg);
          egg_snprintf(newmsg, sizeof newmsg, "KICK %s %s %s\n", chan,
		       newnicks + 1, reason);
          m->msg = nmalloc(strlen(newmsg) + 1);
          m->len = strlen(newmsg);
          strcpy(m->msg, newmsg);
        }
      }
    }
    lm = m;
    if (m)
      m = m->next;
    else
      m = q->head;
  }
  Context;
}

static int deq_kick(int which)
{
  struct msgq_head *h;
  struct msgq *msg, *m, *lm;
  char buf[511], buf2[511], *reason2, *nicks, *chan, *chan2, *reason, *nick,
       newnicks[511], newnicks2[511], newmsg[511];
  int changed = 0, nr = 0;

  Context;
  if (!optimize_kicks)
    return 0;
  newnicks[0] = 0;
  switch (which) {
    case DP_MODE:
      h = &modeq;
      break;
    case DP_SERVER:
      h = &mq;
      break;
    case DP_HELP:
      h = &hq;
      break;
    default:
      return 0;
  }
  if (egg_strncasecmp(h->head->msg, "KICK", 4))
    return 0;
  if (optimize_kicks == 2) {
    purge_kicks(h);
    if (!h->head)
      return 1;
  }
  if (egg_strncasecmp(h->head->msg, "KICK", 4))
    return 0;
  msg = h->head;
  strncpyz(buf, msg->msg, sizeof buf);
  reason = buf;
  newsplit(&reason);
  chan = newsplit(&reason);
  nicks = newsplit(&reason);
  while (strlen(nicks) > 0) {
    egg_snprintf(newnicks, sizeof newnicks, "%s,%s", newnicks,
		 newsplit(&nicks));
    nr++;
  }
  for (m = msg->next, lm = NULL; m && (nr < kick_method);) {
    if (!egg_strncasecmp(m->msg, "KICK", 4)) {
      changed = 0;
      newnicks2[0] = 0;
      strncpyz(buf2, m->msg, sizeof buf2);
      if (buf2[0] && (buf2[strlen(buf2)-1] == '\n'))
        buf2[strlen(buf2)-1] = '\0';
      reason2 = buf2;
      newsplit(&reason2);
      chan2 = newsplit(&reason2);
      nicks = newsplit(&reason2);
      if (!egg_strcasecmp(chan, chan2) && !egg_strcasecmp(reason, reason2)) {
        while (strlen(nicks) > 0) {
          nick = splitnicks(&nicks);
          if ((nr < kick_method) &&
             ((9 + strlen(chan) + strlen(newnicks) + strlen(nick) +
             strlen(reason)) < 510)) {
            egg_snprintf(newnicks, sizeof newnicks, "%s,%s", newnicks, nick);
            nr++;
            changed = 1;
          } else
            egg_snprintf(newnicks2, sizeof newnicks2, "%s,%s", newnicks2, nick);
        }
      }
      if (changed) {
        if (newnicks2[0] == 0) {
          if (!lm)
            h->head->next = m->next;
          else
            lm->next = m->next;
          nfree(m->msg);
          nfree(m);
          m = lm;
          h->tot--;
          if (!h->head)
            h->last = 0;
        } else {
          nfree(m->msg);
          egg_snprintf(newmsg, sizeof newmsg, "KICK %s %s %s\n", chan2,
		       newnicks2 + 1, reason);
          m->msg = nmalloc(strlen(newmsg) + 1);
          m->len = strlen(newmsg);
          strcpy(m->msg, newmsg);
        }
      }
    }
    lm = m;
    if (m)
      m = m->next;
    else
      m = h->head->next;
  }
  egg_snprintf(newmsg, sizeof newmsg, "KICK %s %s %s\n", chan, newnicks + 1,
	       reason);
  tputs(serv, newmsg, strlen(newmsg));
  if (debug_output) {
    newmsg[strlen(newmsg) - 1] = 0;
    switch (which) {
      case DP_MODE:
        putlog(LOG_SRVOUT, "*", "[m->] %s", newmsg);
        break;
      case DP_SERVER:
        putlog(LOG_SRVOUT, "*", "[s->] %s", newmsg);
        break;
      case DP_HELP:
        putlog(LOG_SRVOUT, "*", "[h->] %s", newmsg);
        break;
    }
    debug3("Changed: %d, kick-method: %d, nr: %d", changed, kick_method, nr);
  }
  h->tot--;
  check_lag(newmsg);
  last_time += calc_penalty(newmsg);
  m = h->head->next;
  nfree(h->head->msg);
  nfree(h->head);
  h->head = m;
  if (!h->head)
    h->last = 0;
  return 1;
}

/* Clean out the msg queues (like when changing servers).
 */
static void empty_msgq()
{
  msgq_clear(&modeq);
  msgq_clear(&mq);
  msgq_clear(&hq);
  burst = 0;
  lagged = 0;
}

/* Use when sending msgs... will spread them out so there's no flooding.
 */
static void queue_server(int which, char *buf, int len)
{
  struct msgq_head *h = NULL,
  		    tempq;
  struct msgq	   *q, *tq, *tqq;
  int		    doublemsg = 0,
  		    qnext = 0;

  /* Don't even BOTHER if there's no server online. */
  if (serv < 0)
    return;
  /* No queue for PING and PONG - drummer */
  if (!egg_strncasecmp(buf, "PING", 4) || !egg_strncasecmp(buf, "PONG", 4)) {
    if (buf[1] == 'I' || buf[1] == 'i')
      lastpingtime = now;	/* lagmeter */
    tputs(serv, buf, len);
    return;
  }

  switch (which) {
  case DP_MODE_NEXT:
    qnext = 1;
    /* Fallthrough */
  case DP_MODE:
    h = &modeq;
    tempq = modeq;
    if (double_mode)
      doublemsg = 1;
    break;

  case DP_SERVER_NEXT:
    qnext = 1;
    /* Fallthrough */
  case DP_SERVER:
    h = &mq;
    tempq = mq;
    if (double_server)
      doublemsg = 1;
    break;

  case DP_HELP_NEXT:
    qnext = 1;
    /* Fallthrough */
  case DP_HELP:
    h = &hq;
    tempq = hq;
    if (double_help)
      doublemsg = 1;
    break;

  default:
    putlog(LOG_MISC, "*", "!!! queueing unknown type to server!!");
    return;
  }

  if (h->tot < maxqmsg) {
    /* Don't queue msg if it's already queued?  */
    if (!doublemsg)
      for (tq = tempq.head; tq; tq = tqq) {
	tqq = tq->next;
	if (!egg_strcasecmp(tq->msg, buf)) {
	  if (!double_warned) {
	    if (buf[len - 1] == '\n')
	      buf[len - 1] = 0;
	    debug1("msg already queued. skipping: %s", buf);
	    double_warned = 1;
	  }
	  return;
	}
      }

    q = nmalloc(sizeof(struct msgq));
    if (qnext)
      q->next = h->head;
    else
      q->next = NULL;
    if (h->head) {
      if (!qnext)
        h->last->next = q;
    } else
      h->head = q;
    if (qnext)
       h->head = q;
    h->last = q;
    q->len = len;
    q->msg = nmalloc(len + 1);
    strncpyz(q->msg, buf, len + 1);
    h->tot++;
    h->warned = 0;
    double_warned = 0;
  } else {
    if (!h->warned)
      putlog(LOG_MISC, "*", "!!! OVER MAXIMUM MODE QUEUE");
    h->warned = 1;
  }

  if (debug_output) {
    if (buf[len - 1] == '\n')
      buf[len - 1] = 0;
    switch (which) {
    case DP_MODE:
      putlog(LOG_SRVOUT, "*", "[!m] %s", buf);
      break;
    case DP_SERVER:
      putlog(LOG_SRVOUT, "*", "[!s] %s", buf);
      break;
    case DP_HELP:
      putlog(LOG_SRVOUT, "*", "[!h] %s", buf);
      break;
    case DP_MODE_NEXT:
      putlog(LOG_SRVOUT, "*", "[!!m] %s", buf);
      break;
    case DP_SERVER_NEXT:
      putlog(LOG_SRVOUT, "*", "[!!s] %s", buf);
      break;
    case DP_HELP_NEXT:
      putlog(LOG_SRVOUT, "*", "[!!h] %s", buf);
      break;
    }
  }

  if (which == DP_MODE || which == DP_MODE_NEXT)
    deq_msg();		/* DP_MODE needs to be sent ASAP, flush if
			   possible. */
}

/* Add a new server to the server_list.
 */
static void add_server(char *ss)
{
  struct server_list *x, *z = serverlist;
  char *p, *q;

  Context;
  while (z && z->next)
    z = z->next;
  while (ss) {
    p = strchr(ss, ',');
    if (p)
      *p++ = 0;
    x = nmalloc(sizeof(struct server_list));

    x->next = 0;
    x->realname = 0;
    if (z)
      z->next = x;
    else
      serverlist = x;
    z = x;
    q = strchr(ss, ':');
    if (!q) {
      x->port = default_port;
      x->pass = 0;
      x->name = nmalloc(strlen(ss) + 1);
      strcpy(x->name, ss);
    } else {
      *q++ = 0;
      x->name = nmalloc(q - ss);
      strcpy(x->name, ss);
      ss = q;
      q = strchr(ss, ':');
      if (!q) {
	x->pass = 0;
      } else {
	*q++ = 0;
	x->pass = nmalloc(strlen(q) + 1);
	strcpy(x->pass, q);
      }
      x->port = atoi(ss);
    }
    ss = p;
  }
}

/* Clear out the given server_list.
 */
static void clearq(struct server_list *xx)
{
  struct server_list *x;

  while (xx) {
    x = xx->next;
    if (xx->name)
      nfree(xx->name);
    if (xx->pass)
      nfree(xx->pass);
    if (xx->realname)
      nfree(xx->realname);
    nfree(xx);
    xx = x;
  }
}

/* Set botserver to the next available server.
 * 
 * -> if (*ptr == -1) then jump to that particular server
 */
static void next_server(int *ptr, char *serv, unsigned int *port, char *pass)
{
  struct server_list *x = serverlist;
  int i = 0;

  if (x == NULL)
    return;
  /* -1  -->  Go to specified server */
  if (*ptr == (-1)) {
    while (x) {
      if (x->port == *port) {
	if (!egg_strcasecmp(x->name, serv)) {
	  *ptr = i;
	  return;
	} else if (x->realname && !egg_strcasecmp(x->realname, serv)) {
	  *ptr = i;
	  strncpyz(serv, x->realname, sizeof serv);
	  return;
	}
      }
      x = x->next;
      i++;
    }
    /* Gotta add it: */
    x = nmalloc(sizeof(struct server_list));

    x->next = 0;
    x->realname = 0;
    x->name = nmalloc(strlen(serv) + 1);
    strcpy(x->name, serv);
    x->port = *port ? *port : default_port;
    if (pass && pass[0]) {
      x->pass = nmalloc(strlen(pass) + 1);
      strcpy(x->pass, pass);
    } else
      x->pass = NULL;
    list_append((struct list_type **) (&serverlist), (struct list_type *) x);
    *ptr = i;
    return;
  }
  /* Find where i am and boogie */
  i = (*ptr);
  while (i > 0 && x != NULL) {
    x = x->next;
    i--;
  }
  if (x != NULL) {
    x = x->next;
    (*ptr)++;
  }				/* Go to next server */
  if (x == NULL) {
    x = serverlist;
    *ptr = 0;
  }				/* Start over at the beginning */
  strcpy(serv, x->name);
  *port = x->port ? x->port : default_port;
  if (x->pass)
    strcpy(pass, x->pass);
  else
    pass[0] = 0;
}

static int server_6char STDVAR
{
  Function F = (Function) cd;
  char x[20];

  BADARGS(7, 7, " nick user@host handle desto/chan keyword/nick text");
  CHECKVALIDITY(server_6char);
  egg_snprintf(x, sizeof x, "%d",
	       F(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]));
  Tcl_AppendResult(irp, x, NULL);
  return TCL_OK;
}

static int server_5char STDVAR
{
  Function F = (Function) cd;

  BADARGS(6, 6, " nick user@host handle channel text");
  CHECKVALIDITY(server_5char);
  F(argv[1], argv[2], argv[3], argv[4], argv[5]);
  return TCL_OK;
}

static int server_2char STDVAR
{
  Function F = (Function) cd;

  BADARGS(3, 3, " nick msg");
  CHECKVALIDITY(server_2char);
  F(argv[1], argv[2]);
  return TCL_OK;
}

static int server_msg STDVAR
{
  Function F = (Function) cd;

  BADARGS(5, 5, " nick uhost hand buffer");
  CHECKVALIDITY(server_msg);
  F(argv[1], argv[2], get_user_by_handle(userlist, argv[3]), argv[4]);
  return TCL_OK;
}

static int server_raw STDVAR
{
  Function F = (Function) cd;

  BADARGS(4, 4, " from code args");
  CHECKVALIDITY(server_raw);
  Tcl_AppendResult(irp, int_to_base10(F(argv[1], argv[3])), NULL);
  return TCL_OK;
}

/* Read/write normal string variable.
 */
static char *nick_change(ClientData cdata, Tcl_Interp *irp, char *name1,
			 char *name2, int flags)
{
  char *new;

  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    Tcl_SetVar2(interp, name1, name2, origbotname, TCL_GLOBAL_ONLY);
    if (flags & TCL_TRACE_UNSETS)
      Tcl_TraceVar(irp, name1, TCL_TRACE_READS | TCL_TRACE_WRITES |
        	   TCL_TRACE_UNSETS, nick_change, cdata);
  } else {			/* writes */
    new = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    if (rfc_casecmp(origbotname, new)) {
      if (origbotname[0]) {
	putlog(LOG_MISC, "*", "* IRC NICK CHANGE: %s -> %s",
	       origbotname, new);
        nick_juped = 0;
      }
      strncpyz(origbotname, new, NICKLEN);
      if (server_online)
	dprintf(DP_SERVER, "NICK %s\n", origbotname);
    }
  }
  return NULL;
}

/* Replace all '?'s in s with a random number.
 */
static void rand_nick(char *nick)
{
  register char *p = nick;

  while ((p = strchr(p, '?')) != NULL) {
    *p = '0' + random() % 10;
    p++;
  }
}

/* Return the alternative bot nick.
 */
static char *get_altbotnick(void)
{
  Context;
  /* A random-number nick? */
  if (strchr(altnick, '?')) {
    if (!raltnick[0]) {
      strncpyz(raltnick, altnick, NICKLEN);
      rand_nick(raltnick);
    }
    return raltnick;
  } else
    return altnick;
}

static char *altnick_change(ClientData cdata, Tcl_Interp *irp, char *name1,
			    char *name2, int flags)
{
  Context;
  /* Always unset raltnick. Will be regenerated when needed. */
  raltnick[0] = 0;
  return NULL;
}

static char *traced_server(ClientData cdata, Tcl_Interp *irp, char *name1,
			   char *name2, int flags)
{
  char s[1024];

  if (server_online) {
    int servidx = findanyidx(serv);

    simple_sprintf(s, "%s:%u", dcc[servidx].host, dcc[servidx].port);
  } else
    s[0] = 0;
  Tcl_SetVar2(interp, name1, name2, s, TCL_GLOBAL_ONLY);
  if (flags & TCL_TRACE_UNSETS)
    Tcl_TraceVar(irp, name1, TCL_TRACE_READS | TCL_TRACE_WRITES |
		 TCL_TRACE_UNSETS, traced_server, cdata);
  return NULL;
}

static char *traced_botname(ClientData cdata, Tcl_Interp *irp, char *name1,
			    char *name2, int flags)
{
  char s[1024];

  simple_sprintf(s, "%s!%s", botname, botuserhost);
  Tcl_SetVar2(interp, name1, name2, s, TCL_GLOBAL_ONLY);
  if (flags & TCL_TRACE_UNSETS)
    Tcl_TraceVar(irp, name1, TCL_TRACE_READS | TCL_TRACE_WRITES |
		 TCL_TRACE_UNSETS, traced_botname, cdata);
  return NULL;
}

static void do_nettype(void)
{
  switch (net_type) {
  case NETT_EFNET:
    check_mode_r = 0;
    nick_len = 9;
    break;
  case NETT_IRCNET:
    check_mode_r = 1;
    use_penalties = 1;
    use_fastdeq = 3;
    nick_len = 9;
    simple_sprintf(stackablecmds, "INVITE AWAY VERSION NICK ISON");
    kick_method = 4;
    break;
  case NETT_UNDERNET:
    check_mode_r = 0;
    use_fastdeq = 2;
    nick_len = 9;
    simple_sprintf(stackablecmds, "PRIVMSG NOTICE TOPIC PART WHOIS");
    break;
  case NETT_DALNET:
    check_mode_r = 0;
    nick_len = 32;
    break;
  case NETT_HYBRID_EFNET:
    check_mode_r = 0;
    nick_len = 9;
    break;
  }
}

static char *traced_nettype(ClientData cdata, Tcl_Interp *irp, char *name1,
			    char *name2, int flags)
{
  do_nettype();
  return NULL;
}

static char *traced_nicklen(ClientData cdata, Tcl_Interp *irp, char *name1,
			    char *name2, int flags)
{
  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    char s[40];

    egg_snprintf(s, sizeof s, "%d", nick_len);
    Tcl_SetVar2(interp, name1, name2, s, TCL_GLOBAL_ONLY);
    if (flags & TCL_TRACE_UNSETS)
      Tcl_TraceVar(irp, name1, TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   traced_nicklen, cdata);
  } else {
    char *cval = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    long lval = 0;

    if (cval && Tcl_ExprLong(interp, cval, &lval) != TCL_ERROR) {
      if (lval > NICKMAX)
	lval = NICKMAX;
      nick_len = (int) lval;
    }
  }
  return NULL;
}

static tcl_strings my_tcl_strings[] =
{
  {"botnick",			NULL,		0,		STR_PROTECT},
  {"altnick",			altnick,	NICKMAX,	0},
  {"realname",			botrealname,	80,		0},
  {"init-server",		initserver,	120,		0},
  {"connect-server",		connectserver,	120,		0},
  {"stackable-commands",	stackablecmds,	510,		0},
  {NULL,			NULL,		0,		0}
};

static tcl_coups my_tcl_coups[] =
{
  {"flood-msg",		&flud_thr,		&flud_time},
  {"flood-ctcp",	&flud_ctcp_thr, 	&flud_ctcp_time},
  {NULL,		NULL,			NULL}
};

static tcl_ints my_tcl_ints[] =
{
  {"use-console-r",		NULL,				1},
  {"servlimit",			&min_servs,			0},
  {"server-timeout",		&server_timeout,		0},
  {"lowercase-ctcp",		&lowercase_ctcp,		0},
  {"server-online",		(int *) &server_online,		2},
  {"never-give-up",		&never_give_up,			0},
  {"keep-nick",			&keepnick,			0},
  {"strict-servernames",	&strict_servernames,		0},
  {"check-stoned",		&check_stoned,			0},
  {"serverror-quit",		&serverror_quit,		0},
  {"quiet-reject",		&quiet_reject,			0},
  {"max-queue-msg",		&maxqmsg,			0},
  {"trigger-on-ignore",		&trigger_on_ignore,		0},
  {"answer-ctcp",		&answer_ctcp,			0},
  {"server-cycle-wait",		(int *) &server_cycle_wait,	0},
  {"default-port",		&default_port,			0},
  {"check-mode-r",		&check_mode_r,			0},
  {"use-ison",			&use_ison,			0},
  {"net-type",			&net_type,			0},
  {"ctcp-mode",			&ctcp_mode,			0},
  {"double-mode",		&double_mode,			0},/* G`Quann */
  {"double-server",		&double_server,			0},
  {"double-help",		&double_help,			0},
  {"use-penalties",		&use_penalties,			0},
  {"use-fastdeq",		&use_fastdeq,			0},
  {"nick-len",			&nick_len,			0},
  {"optimize-kicks",		&optimize_kicks,		0},
  {"isjuped",			&nick_juped,			0},
  {"use-lagcheck",		&use_lagcheck,			0},
  {"bot-is-lagged",		&lagged,			0},
  {NULL,			NULL,				0}
};


/*
 *     Tcl variable trace functions
 */

/* Read or write the server list.
 */
static char *tcl_eggserver(ClientData cdata, Tcl_Interp *irp, char *name1,
			   char *name2, int flags)
{
  Tcl_DString ds;
  char *slist, **list, x[1024];
  struct server_list *q;
  int lc, code, i;

  Context;
  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    /* Create server list */
    Tcl_DStringInit(&ds);
    for (q = serverlist; q; q = q->next) {
      egg_snprintf(x, sizeof x, "%s:%d%s%s %s", q->name,
		   q->port ? q->port : default_port, q->pass ? ":" : "",
		   q->pass ? q->pass : "", q->realname ? q->realname : "");
      Tcl_DStringAppendElement(&ds, x);
    }
    slist = Tcl_DStringValue(&ds);
    Tcl_SetVar2(interp, name1, name2, slist, TCL_GLOBAL_ONLY);
    Tcl_DStringFree(&ds);
  } else {		/* TCL_TRACE_WRITES */
    if (serverlist) {
      clearq(serverlist);
      serverlist = NULL;
    }
    slist = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    if (slist != NULL) {
      code = Tcl_SplitList(interp, slist, &lc, &list);
      if (code == TCL_ERROR)
	return interp->result;
      for (i = 0; i < lc && i < 50; i++)
	add_server(list[i]);

      /* Tricky way to make the bot reset its server pointers
       * perform part of a '.jump <current-server>':
       */
      if (server_online) {
	int servidx = findanyidx(serv);

	curserv = (-1);
	next_server(&curserv, dcc[servidx].host, &dcc[servidx].port, "");
      }
      Tcl_Free((char *) list);
    }
  }
  Context;
  return NULL;
}

/* Trace the servers */
#define tcl_traceserver(name, ptr) \
  Tcl_TraceVar(interp, name, TCL_TRACE_READS | TCL_TRACE_WRITES |	\
	       TCL_TRACE_UNSETS, tcl_eggserver, (ClientData) ptr)

#define tcl_untraceserver(name, ptr) \
  Tcl_UntraceVar(interp, name, TCL_TRACE_READS | TCL_TRACE_WRITES |	\
		 TCL_TRACE_UNSETS, tcl_eggserver, (ClientData) ptr)


/*
 *     CTCP DCC CHAT functions
 */

static void dcc_chat_hostresolved(int);

/* This only handles CHAT requests, otherwise it's handled in filesys.
 */
static int ctcp_DCC_CHAT(char *nick, char *from, char *handle,
			 char *object, char *keyword, char *text)
{
  char *action, *param, *ip, *prt, buf[512], *msg = buf;
  int i;
  struct userrec *u = get_user_by_handle(userlist, handle);
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0, 0, 0};

  strcpy(msg, text);
  action = newsplit(&msg);
  param = newsplit(&msg);
  ip = newsplit(&msg);
  prt = newsplit(&msg);
  Context;
  if (egg_strcasecmp(action, "CHAT") || !u)
    return 0;
  get_user_flagrec(u, &fr, 0);
  if (dcc_total == max_dcc) {
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s\n", nick, DCC_TOOMANYDCCS1);
    putlog(LOG_MISC, "*", DCC_TOOMANYDCCS2, "CHAT", param, nick, from);
  } else if (!(glob_party(fr) || (!require_p && chan_op(fr)))) {
    if (glob_xfer(fr))
      return 0;			/* Allow filesys to pick up the chat */
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s\n", nick, DCC_REFUSED2);
    putlog(LOG_MISC, "*", "%s: %s!%s", DCC_REFUSED, nick, from);
  } else if (u_pass_match(u, "-")) {
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s\n", nick, DCC_REFUSED3);
    putlog(LOG_MISC, "*", "%s: %s!%s", DCC_REFUSED4, nick, from);
  } else if ((atoi(prt) < min_dcc_port) || (atoi(prt) > max_dcc_port)) {
    /* Invalid port range. */
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s (invalid port)\n", nick,
	      DCC_CONNECTFAILED1);
    putlog(LOG_MISC, "*", "%s: CHAT (%s!%s)", DCC_CONNECTFAILED3,
	   nick, from);
  } else {
    if (!sanitycheck_dcc(nick, from, ip, prt))
      return 1;
    i = new_dcc(&DCC_DNSWAIT, sizeof(struct dns_info));

    if (i < 0) {
      putlog(LOG_MISC, "*", "DCC connection: CHAT (%s!%s)", dcc[i].nick, ip);
      return 1;
    }
    dcc[i].addr = my_atoul(ip);
    dcc[i].port = atoi(prt);
    dcc[i].sock = -1;
    strcpy(dcc[i].nick, u->handle);
    strcpy(dcc[i].host, from);
    dcc[i].timeval = now;
    dcc[i].user = u;
    dcc[i].u.dns->ip = dcc[i].addr;
    dcc[i].u.dns->dns_type = RES_HOSTBYIP;
    dcc[i].u.dns->dns_success = (Function) dcc_chat_hostresolved;
    dcc[i].u.dns->dns_failure = (Function) dcc_chat_hostresolved;
    dcc[i].u.dns->type = &DCC_CHAT_PASS;
    dcc_dnshostbyip(dcc[i].addr);
  }
  return 1;
}

static void dcc_chat_hostresolved(int i)
{
  char buf[512], ip[512];
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0, 0, 0};

  egg_snprintf(buf, sizeof buf, "%d", dcc[i].port);
  if (!hostsanitycheck_dcc(dcc[i].nick, dcc[i].host, dcc[i].addr,
			   dcc[i].u.dns->host, buf)) {
    lostdcc(i);
    return;
  }
  dcc[i].sock = getsock(0);
  egg_snprintf(ip, sizeof ip, "%lu", iptolong(my_htonl(dcc[i].addr)));
  if (open_telnet_dcc(dcc[i].sock, ip, buf) < 0) {
    neterror(buf);
    if(!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s (%s)\n", dcc[i].nick,
	      DCC_CONNECTFAILED1, buf);
    putlog(LOG_MISC, "*", "%s: CHAT (%s!%s)", DCC_CONNECTFAILED2,
	   dcc[i].nick, dcc[i].host);
    putlog(LOG_MISC, "*", "    (%s)", buf);
    killsock(dcc[i].sock);
    lostdcc(i);
  } else {
    changeover_dcc(i, &DCC_CHAT_PASS, sizeof(struct chat_info));
    dcc[i].status = STAT_ECHO;
    get_user_flagrec(dcc[i].user, &fr, 0);
    if (glob_party(fr))
      dcc[i].status |= STAT_PARTY;
    strcpy(dcc[i].u.chat->con_chan, (chanset) ? chanset->dname : "*");
    dcc[i].timeval = now;
    /* Ok, we're satisfied with them now: attempt the connect */
    putlog(LOG_MISC, "*", "DCC connection: CHAT (%s!%s)", dcc[i].nick,
	   dcc[i].host);
    dprintf(i, "%s\n", DCC_ENTERPASS);
  }
  return;
}


/*
 *     Server timer functions
 */

static void server_secondly()
{
  if (cycle_time)
    cycle_time--;
  deq_msg();
  if (!resolvserv && serv < 0)
    connect_server();
}

static void server_5minutely()
{
  if (check_stoned) {
    if (waiting_for_awake) {
      /* Uh oh!  Never got pong from last time, five minutes ago!
       * Server is probably stoned.
       */
      int servidx = findanyidx(serv);

      disconnect_server(servidx);
      lostdcc(servidx);
      putlog(LOG_SERV, "*", IRC_SERVERSTONED);
    } else if (!trying_server) {
      /* Check for server being stoned. */
      dprintf(DP_MODE, "PING :%lu\n", (unsigned long) now);
      waiting_for_awake = 1;
    }
  }
}

static void server_prerehash()
{
  strcpy(oldnick, botname);
}

static void server_postrehash()
{
  strncpyz(botname, origbotname, NICKLEN);
  if (!botname[0])
    fatal("NO BOT NAME.", 0);
  if (serverlist == NULL)
    fatal("NO SERVER.", 0);
    if (oldnick[0] && !rfc_casecmp(oldnick, botname)
       && !rfc_casecmp(oldnick, get_altbotnick())) {
    /* Change botname back, don't be premature. */
    strcpy(botname, oldnick);
    dprintf(DP_SERVER, "NICK %s\n", origbotname);
  }
  /* Change botname back incase we were using altnick previous to rehash. */
  else if (oldnick[0])
    strcpy(botname, oldnick);
  if (initserver[0])
    do_tcl("init-server", initserver);
}

/* A report on the module status.
 */
static void server_report(int idx, int details)
{
  char s1[128], s[128];

  if (nick_juped)
    dprintf(idx, "    NICK IS JUPED: %s %s\n", origbotname,
            keepnick ? "(trying)" : "");
  dprintf(idx, "    Online as: %s!%s (%s)\n", botname, botuserhost,
	  botrealname);
  if (!trying_server) {
    daysdur(now, server_online, s1);
    egg_snprintf(s, sizeof s, "(connected %s)", s1);
    if ((server_lag) && !(waiting_for_awake)) {
      egg_snprintf(s1, sizeof s1, " (lag: %ds)", server_lag);
      if (server_lag == (-1))
	egg_snprintf(s1, sizeof s1, " (bad pong replies)");
      strcat(s, s1);
    }
  }
  if (server_online) {
    int servidx = findanyidx(serv);

    nick_juped = 0;
    dprintf(idx, "    Server %s:%d %s\n", dcc[servidx].host, dcc[servidx].port,
	    trying_server ? "(trying)" : s);
  } else
    dprintf(idx, "    %s\n", IRC_NOSERVER);
  if (modeq.tot)
    dprintf(idx, "    %s %d%%, %d msgs\n", IRC_MODEQUEUE,
            (int) ((float) (modeq.tot * 100.0) / (float) maxqmsg),
	    (int) modeq.tot);
  if (mq.tot)
    dprintf(idx, "    %s %d%%, %d msgs\n", IRC_SERVERQUEUE,
           (int) ((float) (mq.tot * 100.0) / (float) maxqmsg), (int) mq.tot);
  if (hq.tot)
    dprintf(idx, "    %s %d%%, %d msgs\n", IRC_HELPQUEUE,
           (int) ((float) (hq.tot * 100.0) / (float) maxqmsg), (int) hq.tot);
  if (details) {
    if (min_servs)
      dprintf(idx, "    Requiring a net of at least %d server(s)\n", min_servs);
    if (initserver[0])
      dprintf(idx, "    On connect, I do: %s\n", initserver);
    if (connectserver[0])
      dprintf(idx, "    Before connect, I do: %s\n", connectserver);
    dprintf(idx, "    Flood is: %d msg/%ds, %d ctcp/%ds\n",
	    flud_thr, flud_time, flud_ctcp_thr, flud_ctcp_time);
  }
}

static void msgq_clear(struct msgq_head *qh)
{
  register struct msgq	*q, *qq;
  
  for (q = qh->head; q; q = qq) {
    qq = q->next;
    nfree(q->msg);
    nfree(q);
  }
  qh->head = qh->last = NULL;
  qh->tot = qh->warned = 0;
}

static int msgq_expmem(struct msgq_head *qh)
{
  register int		 tot = 0;
  register struct msgq	*m;

  for (m = qh->head; m; m = m->next) {
    tot += m->len + 1;
    tot += sizeof(struct msgq);
  }
  return tot;
}

static int server_expmem()
{
  int			 tot = 0;
  struct server_list	*s = serverlist;

  Context;
  for (; s; s = s->next) {
    if (s->name)
      tot += strlen(s->name) + 1;
    if (s->pass)
      tot += strlen(s->pass) + 1;
    if (s->realname)
      tot += strlen(s->realname) + 1;
    tot += sizeof(struct server_list);
  }

  tot += msgq_expmem(&mq) + msgq_expmem(&hq) + msgq_expmem(&modeq);

  if (lagcheckstring)
    tot += strlen(lagcheckstring) + 1;
  if (lagcheckstring2)
    tot += strlen(lagcheckstring2) + 1;

  return tot;
}

/* Put the full hostname in s.
 */
static void getmyhostname(char *s)
{
  struct hostent *hp;
  char *p;

  if (hostname[0]) {
    strcpy(s, hostname);
    return;
  }
  p = getenv("HOSTNAME");
  if (p != NULL) {
    strncpyz(s, p, 81);
    if (strchr(s, '.') != NULL)
      return;
  }
  gethostname(s, 80);
  if (strchr(s, '.') != NULL)
    return;
  hp = gethostbyname(s);
  if (hp == NULL)
    fatal("Hostname self-lookup failed.", 0);
  strcpy(s, hp->h_name);
  if (strchr(s, '.') != NULL)
    return;
  if (hp->h_aliases[0] == NULL)
    fatal("Can't determine your hostname!", 0);
  strcpy(s, hp->h_aliases[0]);
  if (strchr(s, '.') == NULL)
    fatal("Can't determine your hostname!", 0);
}

static cmd_t my_ctcps[] =
{
  {"DCC",	"",	ctcp_DCC_CHAT,		"server:DCC"},
  {NULL,	NULL,	NULL,			NULL}
};

static char *server_close()
{
  /* FIXME - I'm an ugly hack. */
  cmd_t C_t[] =
  {
    {"die",	"m",	NULL /* Inserted below. */,	NULL},
    {NULL,	NULL,	NULL,				NULL}
  };
  C_t[0].func = (Function) cmd_die;

  Context;
  cycle_time = 100;
  nuke_server("Connection reset by phear");
  clearq(serverlist);
  Context;
  rem_builtins(H_dcc, C_dcc_serv);
  rem_builtins(H_raw, my_raw_binds);
  rem_builtins(H_ctcp, my_ctcps);
  Context;
  /* Restore original commands. */
  add_builtins(H_dcc, C_t);
  Context;
  del_bind_table(H_wall);
  del_bind_table(H_raw);
  del_bind_table(H_notc);
  del_bind_table(H_msgm);
  del_bind_table(H_msg);
  del_bind_table(H_flud);
  del_bind_table(H_ctcr);
  del_bind_table(H_ctcp);
  Context;
  rem_tcl_coups(my_tcl_coups);
  rem_tcl_strings(my_tcl_strings);
  rem_tcl_ints(my_tcl_ints);
  rem_help_reference("server.help");
  rem_tcl_commands(my_tcl_cmds);
  Context;
  Tcl_UntraceVar(interp, "nick",
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 nick_change, NULL);
  Tcl_UntraceVar(interp, "altnick",
		 TCL_TRACE_WRITES | TCL_TRACE_UNSETS, altnick_change, NULL);
  Tcl_UntraceVar(interp, "botname",
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 traced_botname, NULL);
  Tcl_UntraceVar(interp, "server",
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 traced_server, NULL);
  Tcl_UntraceVar(interp, "net-type",
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 traced_nettype, NULL);
  Tcl_UntraceVar(interp, "nick-len",
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 traced_nicklen, NULL);
  tcl_untraceserver("servers", NULL);
  Context;
  empty_msgq();
  Context;
  del_hook(HOOK_SECONDLY, (Function) server_secondly);
  del_hook(HOOK_5MINUTELY, (Function) server_5minutely);
  del_hook(HOOK_QSERV, (Function) queue_server);
  del_hook(HOOK_MINUTELY, (Function) minutely_checks);
  del_hook(HOOK_PRE_REHASH, (Function) server_prerehash);
  del_hook(HOOK_REHASH, (Function) server_postrehash);
  Context;
  if (lagcheckstring)
    free_null(lagcheckstring);
  if (lagcheckstring2)
    free_null(lagcheckstring2);
  module_undepend(MODULE_NAME);
  return NULL;
}

char *server_start();

static Function server_table[] =
{
  (Function) server_start,
  (Function) server_close,
  (Function) server_expmem,
  (Function) server_report,
  /* 4 - 7 */
  (Function) NULL,		/* char * (points to botname later on)	*/
  (Function) botuserhost,	/* char *				*/
  (Function) & quiet_reject,	/* int					*/
  (Function) & serv,		/* int					*/
  /* 8 - 11 */
  (Function) & flud_thr,	/* int					*/
  (Function) & flud_time,	/* int					*/
  (Function) & flud_ctcp_thr,	/* int					*/
  (Function) & flud_ctcp_time,	/* int					*/
  /* 12 - 15 */
  (Function) match_my_nick,
  (Function) check_tcl_flud,
  (Function) NULL,		/* fixfrom - moved to the core (drummer) */
  (Function) & answer_ctcp,	/* int					*/
  /* 16 - 19 */
  (Function) & trigger_on_ignore, /* int				*/
  (Function) check_tcl_ctcpr,
  (Function) detect_avalanche,
  (Function) nuke_server,
  /* 20 - 23 */
  (Function) newserver,		/* char *				*/
  (Function) & newserverport,	/* int					*/
  (Function) newserverpass,	/* char *				*/
  /* 24 - 27 */
  (Function) & cycle_time,	/* int					*/
  (Function) & default_port,	/* int					*/
  (Function) & server_online,	/* int					*/
  (Function) & min_servs,	/* int					*/
  /* 28 - 31 */
  (Function) & H_raw,		/* p_tcl_bind_list			*/
  (Function) & H_wall,		/* p_tcl_bind_list			*/
  (Function) & H_msg,		/* p_tcl_bind_list			*/
  (Function) & H_msgm,		/* p_tcl_bind_list			*/
  /* 32 - 35 */
  (Function) & H_notc,		/* p_tcl_bind_list			*/
  (Function) & H_flud,		/* p_tcl_bind_list			*/
  (Function) & H_ctcp,		/* p_tcl_bind_list			*/
  (Function) & H_ctcr,		/* p_tcl_bind_list			*/
  /* 36 - 39 */
  (Function) ctcp_reply,
  (Function) get_altbotnick,	/* char *				*/
  (Function) & nick_len,	/* int					*/
  (Function) check_tcl_notc
};

char *server_start(Function *global_funcs)
{
  char *s;

  global = global_funcs;

  /*
   * Init of all the variables *must* be done in _start rather than
   * globally.
   */
  serv = -1;
  strict_host = 1;
  botname[0] = 0;
  trying_server = 0L;
  server_lag = 0;
  altnick[0] = 0;
  raltnick[0] = 0;
  curserv = 0;
  flud_thr = 5;
  flud_time = 60;
  flud_ctcp_thr = 3;
  flud_ctcp_time = 60;
  initserver[0] = 0;
  connectserver[0] = 0;		/* drummer */
  botuserhost[0] = 0;
  keepnick = 1;
  check_stoned = 1;
  serverror_quit = 1;
  quiet_reject = 1;
  waiting_for_awake = 0;
  server_online = 0;
  server_cycle_wait = 60;
  strcpy(botrealname, "A deranged product of evil coders");
  min_servs = 0;
  server_timeout = 60;
  never_give_up = 0;
  strict_servernames = 0;
  serverlist = NULL;
  cycle_time = 0;
  default_port = 6667;
  oldnick[0] = 0;
  trigger_on_ignore = 0;
  answer_ctcp = 1;
  lowercase_ctcp = 0;
  bothost[0] = 0;
  check_mode_r = 0;
  maxqmsg = 300;
  burst = 0;
  use_ison = 1;
  net_type = NETT_EFNET;
  double_mode = 0;
  double_server = 0;
  double_help = 0;
  use_penalties = 0;
  use_fastdeq = 0;
  stackablecmds[0] = 0;
  resolvserv = 0;
  lastpingtime = 0;
  last_time = 0;
  nick_len = 9;
  lagchecktype = 0;
  lagcheckstring = NULL;
  lagcheckstring2 = NULL;
  use_lagcheck = 0;
  kick_method = 1;
  optimize_kicks = 0;
  lagged = 0;

  Context;
  server_table[4] = (Function) botname;
  module_register(MODULE_NAME, server_table, 1, 1);
  if (!module_depend(MODULE_NAME, "eggdrop", 105, 3)) {
    module_undepend(MODULE_NAME);
    return "This module requires eggdrop1.5.3 or later";
  }

  Context;
  /* Fool bot in reading the values. */
  tcl_eggserver(NULL, interp, "servers", NULL, 0);
  tcl_traceserver("servers", NULL);
  s = Tcl_GetVar(interp, "nick", TCL_GLOBAL_ONLY);
  if (s)
    strncpyz(origbotname, s, NICKLEN);
  Tcl_TraceVar(interp, "nick",
	       TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
	       nick_change, NULL);
  Tcl_TraceVar(interp, "altnick",
	       TCL_TRACE_WRITES | TCL_TRACE_UNSETS, altnick_change, NULL);
  Tcl_TraceVar(interp, "botname",
	       TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
	       traced_botname, NULL);
  Tcl_TraceVar(interp, "server",
	       TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
	       traced_server, NULL);
  Tcl_TraceVar(interp, "net-type",
	       TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
	       traced_nettype, NULL);
  Tcl_TraceVar(interp, "nick-len",
	       TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
	       traced_nicklen, NULL);

  Context;
  H_wall = add_bind_table("wall", HT_STACKABLE, server_2char);
  H_raw = add_bind_table("raw", HT_STACKABLE, server_raw);
  H_notc = add_bind_table("notc", HT_STACKABLE, server_6char);
  H_msgm = add_bind_table("msgm", HT_STACKABLE, server_5char);
  H_msg = add_bind_table("msg", 0, server_msg);
  H_flud = add_bind_table("flud", HT_STACKABLE, server_5char);
  H_ctcr = add_bind_table("ctcr", HT_STACKABLE, server_6char);
  H_ctcp = add_bind_table("ctcp", HT_STACKABLE, server_6char);
  Context;
  add_builtins(H_raw, my_raw_binds);
  add_builtins(H_dcc, C_dcc_serv);
  add_builtins(H_ctcp, my_ctcps);
  add_help_reference("server.help");
  Context;
  my_tcl_strings[0].buf = botname;
  add_tcl_strings(my_tcl_strings);
  my_tcl_ints[0].val = &use_console_r;
  add_tcl_ints(my_tcl_ints);
  add_tcl_commands(my_tcl_cmds);
  add_tcl_coups(my_tcl_coups);
  Context;
  add_hook(HOOK_SECONDLY, (Function) server_secondly);
  add_hook(HOOK_5MINUTELY, (Function) server_5minutely);
  add_hook(HOOK_MINUTELY, (Function) minutely_checks);
  add_hook(HOOK_QSERV, (Function) queue_server);
  add_hook(HOOK_PRE_REHASH, (Function) server_prerehash);
  add_hook(HOOK_REHASH, (Function) server_postrehash);
  Context;
  mq.head = hq.head = modeq.head = NULL;
  mq.last = hq.last = modeq.last = NULL;
  mq.tot = hq.tot = modeq.tot = 0;
  mq.warned = hq.warned = modeq.warned = 0;
  double_warned = 0;
  Context;
  newserver[0] = 0;
  newserverport = 0;
  getmyhostname(bothost);
  Context;
  /* Wishful thinking ... */
  egg_snprintf(botuserhost, sizeof botuserhost, "%s@%s", botuser, bothost);
  curserv = 999;
  do_nettype();
  putlog(LOG_MISC, "*", "=== SERVER SUPPORT LOADED");
  return NULL;
}
