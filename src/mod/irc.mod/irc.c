/* 
 * irc.c -- part of irc.mod
 *   support for channels within the bot 
 * 
 * $Id: irc.c,v 1.31 2000/08/21 18:28:11 guppy Exp $
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

#define MODULE_NAME "irc"
#define MAKING_IRC
#include "src/mod/module.h"
#include "irc.h"
#include "server.mod/server.h"
#include "channels.mod/channels.h"
#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

static p_tcl_bind_list H_topc, H_splt, H_sign, H_rejn, H_part, H_pub, H_pubm;
static p_tcl_bind_list H_nick, H_mode, H_kick, H_join, H_need;
static Function *global = NULL, *channels_funcs = NULL, *server_funcs = NULL;

static int ctcp_mode;
static int net_type;
static int strict_host;
static int wait_split = 300;		/* Time to wait for user to return from
					 * net-split. */
static int max_bans = 20;
static int max_exempts = 20;
static int max_invites = 20;
static int max_modes = 30;
static int bounce_bans = 1;
static int bounce_exempts = 0;
static int bounce_invites = 0;
static int bounce_modes = 0;
static int learn_users = 0;
static int wait_info = 15;
static int invite_key = 1;
static int no_chanrec_info = 0;
static int modesperline = 3;		/* Number of modes per line to send. */
static int mode_buf_len = 200;		/* Maximum bytes to send in 1 mode. */
static int use_354 = 0;			/* Use ircu's short 354 /who
					 * responses */
static int kick_method = 1;		/* How many kicks does the irc network
					 * support at once?
					 * 0 = as many as possible. 
					 *     (Ernst 18/3/1998) */
static int kick_fun = 0;
static int ban_fun = 0;
static int keepnick = 1;		/* Keep nick */
static int prevent_mixing = 1;		/* To prevent mixing old/new modes */
static int revenge_mode = 1;		/* 0 = deop, 1 = and +d, 2 = and kick,
					 * 3 = and ban */
static int rfc_compliant = 1;		/* net-type changing modifies this */

#include "chan.c"
#include "mode.c"
#include "cmdsirc.c"
#include "msgcmds.c"
#include "tclirc.c"


/* Contains the logic to decide wether we want to punish someone. Returns
 * true if we want to, false if not.
 */
static int want_to_revenge(struct chanset_t *chan, struct userrec *u,
			   struct userrec *u2, char *badnick, char *victim,
			   int mevictim)
{
  struct flag_record fr = { FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0 };
  struct flag_record fr2 = { FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0 };

  Context;
  get_user_flagrec(u, &fr, chan->dname);
  get_user_flagrec(u2, &fr2, chan->dname);

  /* If we don't even know the user we'll probably not want to protect
   * it.
   */
  if (!u2)
    return 0;

  /* Kickee is not a friend? */
  if (!chan_friend(fr) && !glob_friend(fr) &&
      /* ... and I didn't kick them? */
      !match_my_nick(badnick) &&
      /* ... and they didn't kick themself? */
      rfc_casecmp(badnick, victim)) {
    /* They kicked ME? and I'm revenging?... muahaHAHAHA! */
    if (mevictim) {
      if (channel_revengebot(chan))
        return 1;
    }
    /* Revenge for others? */
    else if (channel_revenge(chan) &&
              /* ... and protecting friends, and kicked is a valid friend? */
             ((channel_protectfriends(chan) &&
               (chan_friend(fr2) || (glob_friend(fr2) && !chan_deop(fr2)))) ||
              /* ... or protecting ops and kicked is valid op? */
              (channel_protectops(chan) &&
               (chan_op(fr2) || (glob_op(fr2) && !chan_deop(fr2))))))
      return 1;
  }
  return 0;
}

/* Dependant on revenge_mode, punish the offender.
 */
static void punish_badguy(struct chanset_t *chan, char *whobad,
			  struct userrec *u, char *badnick, char *victim,
			  int mevictim, int type)
{
  char reason[1024], ct[81], *kick_msg;
  memberlist *m;
  struct flag_record fr = { FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0 };

  m = ismember(chan, badnick);
  if (!m)
    return;
  get_user_flagrec(u, &fr, chan->dname);

  Context;
  /* Get current time into a string */
  strftime(ct, 7, "%d %b", localtime(&now));

  /* Put together log and kick messages */
  reason[0] = 0;
  switch (type) {
  case REVENGE_KICK:
    kick_msg = IRC_KICK_PROTECT;
    simple_sprintf(reason, "kicked %s off %s", victim, chan->dname);
    break;
  case REVENGE_DEOP:
    simple_sprintf(reason, "deopped %s on %s", victim, chan->dname);
    kick_msg = IRC_DEOP_PROTECT;
    break;
  default:
    kick_msg = "revenge!";
  }
  putlog(LOG_MISC, chan->dname, "Punishing %s (%s)", badnick, reason);
 
  Context;
  /* Set the offender +d */
  if ((revenge_mode > 0) &&
      /* ... unless there's no more to do */
      !(chan_deop(fr) || glob_deop(fr))) {
    char s[UHOSTLEN], s1[UHOSTLEN];
    memberlist *mx = NULL;

    /* Removing op */
    if (chan_op(fr) || (glob_op(fr) && !chan_deop(fr))) {
      fr.match = FR_CHAN;
      if (chan_op(fr)) {
        fr.chan &= ~USER_OP;
      } else {
        fr.chan |= USER_DEOP;
      }
      set_user_flagrec(u, &fr, chan->dname);
      putlog(LOG_MISC, "*", "No longer opping %s[%s] (%s)", u->handle, whobad,
	     reason);
    }
    /* ... or just setting to deop */
    else if (u) {
      /* In the user list already, cool :) */
      fr.match = FR_CHAN;
      fr.chan |= USER_DEOP;
      set_user_flagrec(u, &fr, chan->dname);
      simple_sprintf(s, "(%s) %s", ct, reason);
      putlog(LOG_MISC, "*", "Now deopping %s[%s] (%s)", u->handle, whobad, s);
    }
    /* ... or creating new user and setting that to deop */
    else {
      Context;
      strcpy(s1, whobad);
      maskhost(s1, s);
      strcpy(s1, badnick);
      /* If that handle exists use "badX" (where X is an increasing number)
       * instead.
       */
      while (get_user_by_handle(userlist, s1)) {
        if (!strncmp(s1, "bad", 3)) {
          int i;

          i = atoi(s1 + 3);
          simple_sprintf(s1 + 3, "%d", i + 1);
        } else
          strcpy(s1, "bad1");		/* Start with '1' */
      }
      userlist = adduser(userlist, s1, s, "-", 0);
      fr.match = FR_CHAN;
      fr.chan = USER_DEOP;
      fr.udef_chan = 0;
      u = get_user_by_handle(userlist, s1);
      if ((mx = ismember(chan, badnick)))
        mx->user = u;
      set_user_flagrec(u, &fr, chan->dname);
      simple_sprintf(s, "(%s) %s (%s)", ct, reason, whobad);
      set_user(&USERENTRY_COMMENT, u, (void *) s);
      putlog(LOG_MISC, "*", "Now deopping %s (%s)", whobad, reason);
    }
  }
  
  /* Always try to deop the offender */
  if (!mevictim)
    add_mode(chan, '-', 'o', badnick);
  /* Ban. Should be done before kicking. */
  if (revenge_mode > 2) {
    char s[UHOSTLEN], s1[UHOSTLEN];

    Context;
    splitnick(&whobad);
    maskhost(whobad, s1);
    simple_sprintf(s, "(%s) %s", ct, reason);
    u_addban(chan, s1, origbotname, s, now + (60 * ban_time), 0);
    if (!mevictim && me_op(chan)) {
      add_mode(chan, '+', 'b', s1);
      flush_mode(chan, QUICK);
    }
  }
  /* Kick the offender */
  if ((revenge_mode > 1) &&
      /* ... or don't we kick ops? */
      (channel_dontkickops(chan) &&
        !(chan_op(fr) || (glob_op(fr) && !chan_deop(fr)))) &&
      /* ... or have we sent the kick already? */
      !chan_sentkick(m) &&
      /* ... and can I actually do anything about it? */
      me_op(chan) && !mevictim) {
    dprintf(DP_MODE, "KICK %s %s :%s\n", chan->name, badnick, kick_msg);
    m->flags |= SENTKICK;
  }
  Context;
}

/* Punishes bad guys under certain circumstances using methods as defined
 * by the revenge_mode flag.
 */
static void maybe_revenge(struct chanset_t *chan, char *whobad,
			  char *whovictim, int type)
{
  char *badnick, *victim;
  int mevictim;
  struct userrec *u, *u2;

  Context;
  if (!chan || (type < 0))
    return;

  /* Get info about offender */
  u = get_user_by_host(whobad);
  badnick = splitnick(&whobad);

  /* Get info about victim */
  u2 = get_user_by_host(whovictim);
  victim = splitnick(&whovictim);
  mevictim = match_my_nick(victim);

  /* Do we want to revenge? */
  if (!want_to_revenge(chan, u, u2, badnick, victim, mevictim))
    return;	/* No, leave them alone ... */

  /* Haha! Do the vengeful thing ... */
  punish_badguy(chan, whobad, u, badnick, victim, mevictim, type);
}

/* Set the key.
 */
static void set_key(struct chanset_t *chan, char *k)
{
  nfree(chan->channel.key);
  if (k == NULL) {
    chan->channel.key = (char *) channel_malloc(1);
    chan->channel.key[0] = 0;
    return;
  }
  chan->channel.key = (char *) channel_malloc(strlen(k) + 1);
  strcpy(chan->channel.key, k);
}

static int hand_on_chan(struct chanset_t *chan, struct userrec *u)
{
  char s[UHOSTLEN];
  memberlist *m = chan->channel.member;

  while (m && m->nick[0]) {
    sprintf(s, "%s!%s", m->nick, m->userhost);
    if (u == get_user_by_host(s))
      return 1;
    m = m->next;
  }
  return 0;
}

/* Adds a ban, exempt or invite mask to the list
 * m should be chan->channel.(exempt|invite|ban)
 */
static void newmask(masklist *m, char *s, char *who)
{
  while (m->mask[0] && rfc_casecmp(m->mask, s))
    m = m->next;
  if (m->mask[0])
    return;			/* Already existent mask */

  m->next = (masklist *)channel_malloc(sizeof(masklist));
  m->next->next = NULL;
  m->next->mask = (char *)channel_malloc(1);
  m->next->mask[0] = 0;
  nfree(m->mask);
  m->mask = (char *) channel_malloc(strlen(s) + 1);
  strcpy(m->mask, s);
  m->who = (char *)channel_malloc(strlen(who) + 1);
  strcpy(m->who, who);
  m->timer = now;
}

/* Removes a nick from the channel member list (returns 1 if successful)
 */
static int killmember(struct chanset_t *chan, char *nick)
{
  memberlist *x, *old;

  x = chan->channel.member;
  old = NULL;
  while (x && x->nick[0] && rfc_casecmp(x->nick, nick)) {
    old = x;
    x = x->next;
  }
  if (!x || !x->nick[0]) {
    if (!channel_pending(chan))
      putlog(LOG_MISC, "*", "(!) killmember(%s) -> nonexistent", nick);
    return 0;
  }
  if (old)
    old->next = x->next;
  else
    chan->channel.member = x->next;
  nfree(x);
  chan->channel.members--;
  /* The following two errors should NEVER happen. We will try to correct
   * them though, to keep the bot from crashing.
   */
  if (chan->channel.members < 0) {
     putlog(LOG_MISC, "*", "(!) BUG: number of members is negative: %d",
	    chan->channel.members);
     chan->channel.members = 0;
     x = chan->channel.member;
     while (x && x->nick[0]) {
       chan->channel.members++;
       x = x->next;
     }
     putlog(LOG_MISC, "*", "(!) actually I know of %d members.",
	    chan->channel.members);
  }
  if (!chan->channel.member) {
    putlog(LOG_MISC, "*", "(!) BUG: memberlist is NULL");
    chan->channel.member = (memberlist *) channel_malloc(sizeof(memberlist));
    chan->channel.member->nick[0] = 0;
    chan->channel.member->next = NULL;
  }
  return 1;
}

/* Check if I am a chanop. Returns boolean 1 or 0.
 */
static int me_op(struct chanset_t *chan)
{
  memberlist *mx = NULL;

  mx = ismember(chan, botname);
  if (!mx)
    return 0;
  if (chan_hasop(mx))
    return 1;
  else
    return 0;
}

/* Check if there are any ops on the channel. Returns boolean 1 or 0.
 */
static int any_ops(struct chanset_t *chan)
{
  memberlist *x = chan->channel.member;

  while (x && x->nick[0] && !chan_hasop(x))
    x = x->next;
  if (!x || !x->nick[0])
    return 0;
  return 1;
}

/* Check whether I'm voice. Returns boolean 1 or 0.
 */
static int me_voice(struct chanset_t *chan)
{
  memberlist	*mx;

  mx = ismember(chan, botname);
  if (!mx)
    return 0;
  if (chan_hasvoice(mx))
    return 1;
  else
    return 0;
}

/* Reset the channel information.
 */
static void reset_chan_info(struct chanset_t *chan)
{
  /* Don't reset the channel if we're already resetting it */
  Context;
  if (channel_inactive(chan)) {
    dprintf(DP_MODE,"PART %s\n", chan->name);
    return;
  }
  if (!channel_pending(chan)) {
    nfree(chan->channel.key);
    chan->channel.key = (char *) channel_malloc(1);
    chan->channel.key[0] = 0;
    clear_channel(chan, 1);
    chan->status |= CHAN_PEND;
    chan->status &= ~(CHAN_ACTIVE | CHAN_ASKEDMODES);
    if (!(chan->status & CHAN_ASKEDBANS)) {
      chan->status |= CHAN_ASKEDBANS;
      dprintf(DP_MODE, "MODE %s +b\n", chan->name);
    }
    if (!(chan->ircnet_status & CHAN_ASKED_EXEMPTS) &&
	use_exempts == 1) {
      chan->ircnet_status |= CHAN_ASKED_EXEMPTS;
      dprintf(DP_MODE, "MODE %s +e\n", chan->name);
    }
    if (!(chan->ircnet_status & CHAN_ASKED_INVITED) &&
	use_invites == 1) {
      chan->ircnet_status |= CHAN_ASKED_INVITED;
      dprintf(DP_MODE, "MODE %s +I\n", chan->name);
    }
    /* These 2 need to get out asap, so into the mode queue */
    dprintf(DP_MODE, "MODE %s\n", chan->name);
    if (use_354)
      dprintf(DP_MODE, "WHO %s %%c%%h%%n%%u%%f\n", chan->name);
    else
      dprintf(DP_MODE, "WHO %s\n", chan->name);
    /* This is not so critical, so slide it into the standard q */
    dprintf(DP_SERVER, "TOPIC %s\n", chan->name);
    /* clear_channel nuked the data...so */
  }
}

/* Report the channel status of every active channel to dcc chat every
 * 5 minutes.
 */
static void status_log()
{
  masklist *b;
  memberlist *m;
  struct chanset_t *chan;
  char s[20], s2[20];
  int chops, voice, nonops, bans, invites, exempts;
  
  for (chan = chanset; chan != NULL; chan = chan->next) {
    if (channel_active(chan) && channel_logstatus(chan) &&
        !channel_inactive(chan)) {

      chops = 0;
      voice = 0;
      for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
	if (chan_hasop(m))
	  chops++;
	else if (chan_hasvoice(m))
	  voice++;
      }
      nonops = (chan->channel.members - (chops + voice));

      for (bans = 0, b = chan->channel.ban; b->mask[0]; b = b->next)
	bans++;
      for (exempts = 0, b = chan->channel.exempt; b->mask[0]; b = b->next)
	exempts++;
      for (invites = 0, b = chan->channel.invite; b->mask[0]; b = b->next)
	invites++;

      sprintf(s, "%d", exempts);
      sprintf(s2, "%d", invites);

      putlog(LOG_MISC, chan->dname,
	     "%s%-10s (%s) : [m/%d o/%d v/%d n/%d b/%d e/%s I/%s]",
             me_op(chan) ? "@" : me_voice(chan) ? "+" : " ", chan->dname,
             getchanmode(chan), chan->channel.members, chops, voice, nonops,
	     bans, use_exempts ? s : "-", use_invites ? s2 : "-");
    }
  }
}

/* If i'm the only person on the channel, and i'm not op'd,
 * might as well leave and rejoin. If i'm NOT the only person
 * on the channel, but i'm still not op'd, demand ops.
 */
static void check_lonely_channel(struct chanset_t *chan)
{
  memberlist *m;
  char s[UHOSTLEN];
  int i = 0;
  static int whined = 0;

  Context;
  if (channel_pending(chan) || !channel_active(chan) || me_op(chan) ||
      channel_inactive(chan))
    return;
  m = chan->channel.member;
  /* Count non-split channel members */
  while (m && m->nick[0]) {
    if (!chan_issplit(m))
      i++;
    m = m->next;
  }
  if ((i == 1) && channel_cycle(chan)) {
    if (chan->name[0] != '+') {	/* Its pointless to cycle + chans for ops */
      putlog(LOG_MISC, "*", "Trying to cycle %s to regain ops.", chan->dname);
      dprintf(DP_MODE, "PART %s\n", chan->name);
      /* If it's a !chan, we need to recreate the channel with !!chan <cybah> */
      dprintf(DP_MODE, "JOIN %s%s %s\n", (chan->dname[0] == '!') ? "!" : "",
	      chan->dname, chan->key_prot);
      whined = 0;
    }
  } else if (any_ops(chan)) {
    whined = 0;
    check_tcl_need(chan->dname, "op");
    if (chan->need_op[0])
      do_tcl("need-op", chan->need_op);
  } else {
    /* Other people here, but none are ops. If there are other bots make
     * them LEAVE!
     */
    int ok = 1;

    if (!whined) {
      /* + is opless. Complaining about no ops when without special
       * help(services), we cant get them - Raist
       */
      if (chan->name[0] != '+')	
	putlog(LOG_MISC, "*", "%s is active but has no ops :(", chan->dname);
      whined = 1;
    }
    m = chan->channel.member;
    while (m && m->nick[0]) {
      struct userrec *u;

      sprintf(s, "%s!%s", m->nick, m->userhost);
      u = get_user_by_host(s);
      if (!match_my_nick(m->nick) && (!u || !(u->flags & USER_BOT)))
	ok = 0;
      m = m->next;
    }
    if (ok) {
      /* ALL bots!  make them LEAVE!!! */
      m = chan->channel.member;
      while (m && m->nick[0]) {
	if (!match_my_nick(m->nick))
	  dprintf(DP_SERVER, "PRIVMSG %s :go %s\n", m->nick, chan->dname);
	m = m->next;
      }
    } else {
      /* Some humans on channel, but still op-less */
      check_tcl_need(chan->dname, "op");
      if (chan->need_op[0])
	do_tcl("need-op", chan->need_op);
    }
  }
}

static void check_expired_chanstuff()
{
  masklist *b, *e;
  memberlist *m, *n;
  char s[UHOSTLEN], *snick, *sfrom;
  struct chanset_t *chan;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  static int count = 4;
  int ok = 0;

  if (!server_online)
    return;
  for (chan = chanset; chan; chan = chan->next) {
    if (!(chan->status & (CHAN_ACTIVE | CHAN_PEND)) &&
	!channel_inactive(chan) &&
	server_online)
      dprintf(DP_MODE, "JOIN %s %s\n",
              (chan->name[0]) ? chan->name : chan->dname,
              chan->channel.key[0] ? chan->channel.key : chan->key_prot);      
    if ((chan->status & (CHAN_ACTIVE | CHAN_PEND)) &&
	channel_inactive(chan))
      dprintf(DP_MODE, "PART %s\n", chan->name);
    if (channel_dynamicbans(chan) &&
	me_op(chan) &&
	!channel_inactive(chan) &&
	ismember(chan, botname)) {
      for (b = chan->channel.ban; b->mask[0]; b = b->next) {
	if ((ban_time != 0) && (((now - b->timer) > (60 * ban_time)) &&
				!u_sticky_mask(chan->bans, b->mask) &&
				!u_sticky_mask(global_bans, b->mask))) {
	  strcpy(s, b->who);
	  sfrom = s;
	  snick = splitnick(&sfrom);
	  
	  if (force_expire || channel_clearbans(chan) ||
	      !(snick[0] && egg_strcasecmp(sfrom, botuserhost) &&
		(m = ismember(chan, snick)) &&
		m->user && (m->user->flags & USER_BOT) && chan_hasop(m))) {
	    putlog(LOG_MODES, chan->dname,
		   "(%s) Channel ban on %s expired.",
		   chan->dname, b->mask);
	    add_mode(chan, '-', 'b', b->mask);
	    b->timer = now;
	  }
	}
      }
    }
    if (use_exempts == 1) {
      if (channel_dynamicexempts(chan) && me_op(chan)) {
	for (e = chan->channel.exempt; e->mask[0]; e = e->next) {
	  if ((exempt_time != 0) && 
	      (((now - e->timer) > (60 * exempt_time)) &&
	       !u_sticky_mask(chan->exempts, e->mask) && 
	       !u_sticky_mask(global_exempts, e->mask))) {
	    strcpy(s, e->who);
	    sfrom = s;
	    snick = splitnick(&sfrom);
	    if (force_expire || channel_clearbans(chan) ||
		!(snick[0] && egg_strcasecmp(sfrom, botuserhost) &&
		  (m = ismember(chan, snick)) &&
		  m->user && (m->user->flags & USER_BOT) && chan_hasop(m))) {
	      /* Check to see if it matches a ban */
	      int match = 0;
	      b = chan->channel.ban;
	      while (b->mask[0] && !match) {
	       if (wild_match(b->mask, e->mask)
		   || wild_match(e->mask, b->mask))
                 match = 1;
	       else
		 b = b->next;
	      }
	      /* Leave this extra logging in for now. Can be removed later
	       * Jason
	       */
	      if (match) {
	        putlog(LOG_MODES, chan->dname,
		       "(%s) Channel exemption %s NOT expired. Exempt still set!",
		       chan->dname, e->mask);
	      } else {
	        putlog(LOG_MODES, chan->dname,
		       "(%s) Channel exemption on %s expired.",
		       chan->dname, e->mask);
	        add_mode(chan, '-', 'e', e->mask);
	      }
	      e->timer = now;
	    }
	  }
	}
      }
    }

    if (use_invites == 1) {
      if (channel_dynamicinvites(chan) && me_op(chan)) {
	for (b = chan->channel.invite; b->mask[0]; b = b->next) {
	  if ((invite_time != 0) &&
	      (((now - b->timer) > (60 * invite_time)) &&
	       !u_sticky_mask(chan->invites, b->mask) && 
	       !u_sticky_mask(global_invites, b->mask))) {
 	    strcpy(s, b->who);
	    sfrom = s;
	    snick = splitnick(&sfrom);

	    if (force_expire || channel_clearbans(chan) ||
		!(snick[0] && egg_strcasecmp(sfrom, botuserhost) &&
		  (m = ismember(chan, snick)) &&
		  m->user && (m->user->flags & USER_BOT) && chan_hasop(m))) {
	      if ((chan->channel.mode & CHANINV) &&
		  isinvited(chan, b->mask)) {
	        /* Leave this extra logging in for now. Can be removed later
	         * Jason
		 */
	        putlog(LOG_MODES, chan->dname,
                   "(%s) Channel invitation %s NOT expired. Invite still set!",
		       chan->dname, b->mask);
	      } else {
	        putlog(LOG_MODES, chan->dname,
		       "(%s) Channel invitation on %s expired.",
		       chan->dname, b->mask);
	        add_mode(chan, '-', 'I', b->mask);
	      }
	      b->timer = now;
	    }
	  }
	}
      }
    }
    m = chan->channel.member;
    while (m && m->nick[0]) {
      if (m->split) {
	n = m->next;
	if (!channel_active(chan))
	  killmember(chan, m->nick);
	else if ((now - m->split) > wait_split) {
	  sprintf(s, "%s!%s", m->nick, m->userhost);
	  m->user = get_user_by_host(s);
	  check_tcl_sign(m->nick, m->userhost, m->user, chan->dname,
			 "lost in the netsplit");
	  putlog(LOG_JOIN, chan->dname,
		 "%s (%s) got lost in the net-split.",
		 m->nick, m->userhost);
	  killmember(chan, m->nick);
	}
	m = n;
      } else
	m = m->next;
    }
    if (channel_active(chan) && me_op(chan) && (chan->idle_kick)) {
      m = chan->channel.member;
      while (m && m->nick[0]) {
	if ((now - m->last) >= (chan->idle_kick * 60) &&
	    !match_my_nick(m->nick) && !chan_issplit(m)) {
	  sprintf(s, "%s!%s", m->nick, m->userhost);
	  m->user = get_user_by_host(s);
	  get_user_flagrec(m->user, &fr, chan->dname);
	  if (!(glob_bot(fr) || glob_friend(fr) ||
		(glob_op(fr) && !glob_deop(fr)) ||
		chan_friend(fr) || chan_op(fr))) {
	    dprintf(DP_SERVER, "KICK %s %s :idle %d min\n", chan->name,
		    m->nick, chan->idle_kick);
	    m->flags |= SENTKICK;
	  }
	}
	m = m->next;
      }
    }
    check_lonely_channel(chan);
  }
  if (min_servs > 0) {
    for (chan = chanset; chan; chan = chan->next)
      if (channel_active(chan) && (chan->channel.members == 1))
	ok = 1;
    if (ok) {
      count++;
      if (count >= 5) {
	dprintf(DP_SERVER, "LUSERS\n");
	count = 0;
      }
    }
  }
}

static int channels_6char STDVAR
{
  Function F = (Function) cd;
  char x[20];

  BADARGS(7, 7, " nick user@host handle desto/chan keyword/nick text");
  CHECKVALIDITY(channels_6char);
  sprintf(x, "%d", F(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]));
  Tcl_AppendResult(irp, x, NULL);
  return TCL_OK;
}

static int channels_5char STDVAR
{
  Function F = (Function) cd;

  BADARGS(6, 6, " nick user@host handle channel text");
  CHECKVALIDITY(channels_5char);
  F(argv[1], argv[2], argv[3], argv[4], argv[5]);
  return TCL_OK;
}

static int channels_4char STDVAR
{
  Function F = (Function) cd;

  BADARGS(5, 5, " nick uhost hand chan/param");
  CHECKVALIDITY(channels_4char);
  F(argv[1], argv[2], argv[3], argv[4]);
  return TCL_OK;
}

static int channels_2char STDVAR
{
  Function F = (Function) cd;

  BADARGS(3, 3, " channel type");
  CHECKVALIDITY(channels_2char);
  F(argv[1], argv[2]);
  return TCL_OK;
}

static void check_tcl_joinspltrejn(char *nick, char *uhost, struct userrec *u,
			       char *chname, p_tcl_bind_list table)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  char args[1024];

  Context;
  simple_sprintf(args, "%s %s!%s", chname, nick, uhost);
  get_user_flagrec(u, &fr, chname);
  Tcl_SetVar(interp, "_jp1", nick, 0);
  Tcl_SetVar(interp, "_jp2", uhost, 0);
  Tcl_SetVar(interp, "_jp3", u ? u->handle : "*", 0);
  Tcl_SetVar(interp, "_jp4", chname, 0);
  Context;
  check_tcl_bind(table, args, &fr, " $_jp1 $_jp2 $_jp3 $_jp4",
		 MATCH_MASK | BIND_USE_ATTR | BIND_STACKABLE);
  Context;
}

/* we handle part messages now *sigh* (guppy 27Jan2000) */

static void check_tcl_part(char *nick, char *uhost, struct userrec *u,
			       char *chname, char *text)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  char args[1024];

  Context;
  simple_sprintf(args, "%s %s!%s", chname, nick, uhost);
  get_user_flagrec(u, &fr, chname);
  Tcl_SetVar(interp, "_p1", nick, 0);
  Tcl_SetVar(interp, "_p2", uhost, 0);
  Tcl_SetVar(interp, "_p3", u ? u->handle : "*", 0);
  Tcl_SetVar(interp, "_p4", chname, 0);
  Tcl_SetVar(interp, "_p5", text ? text : "", 0);
  Context;
  check_tcl_bind(H_part, args, &fr, " $_p1 $_p2 $_p3 $_p4 $_p5",
		 MATCH_MASK | BIND_USE_ATTR | BIND_STACKABLE);
  Context;
}

static void check_tcl_signtopcnick(char *nick, char *uhost, struct userrec *u,
				   char *chname, char *reason,
				   p_tcl_bind_list table)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  char args[1024];

  Context;
  if (table == H_sign)
    simple_sprintf(args, "%s %s!%s", chname, nick, uhost);
  else
    simple_sprintf(args, "%s %s", chname, reason);
  get_user_flagrec(u, &fr, chname);
  Tcl_SetVar(interp, "_stnm1", nick, 0);
  Tcl_SetVar(interp, "_stnm2", uhost, 0);
  Tcl_SetVar(interp, "_stnm3", u ? u->handle : "*", 0);
  Tcl_SetVar(interp, "_stnm4", chname, 0);
  Tcl_SetVar(interp, "_stnm5", reason, 0);
  Context;
  check_tcl_bind(table, args, &fr, " $_stnm1 $_stnm2 $_stnm3 $_stnm4 $_stnm5",
		 MATCH_MASK | BIND_USE_ATTR | BIND_STACKABLE);
  Context;
}

static void check_tcl_kickmode(char *nick, char *uhost, struct userrec *u,
			       char *chname, char *dest, char *reason,
			       p_tcl_bind_list table)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  char args[512];

  Context;
  get_user_flagrec(u, &fr, chname);
  if (table == H_mode)
    simple_sprintf(args, "%s %s", chname, dest);
  else
    simple_sprintf(args, "%s %s %s", chname, dest, reason);
  Tcl_SetVar(interp, "_kick1", nick, 0);
  Tcl_SetVar(interp, "_kick2", uhost, 0);
  Tcl_SetVar(interp, "_kick3", u ? u->handle : "*", 0);
  Tcl_SetVar(interp, "_kick4", chname, 0);
  Tcl_SetVar(interp, "_kick5", dest, 0);
  Tcl_SetVar(interp, "_kick6", reason, 0);
  Context;
  check_tcl_bind(table, args, &fr, " $_kick1 $_kick2 $_kick3 $_kick4 $_kick5 $_kick6",
		 MATCH_MASK | BIND_USE_ATTR | BIND_STACKABLE);
  Context;
}

static int check_tcl_pub(char *nick, char *from, char *chname, char *msg)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  int x;
  char buf[512], *args = buf, *cmd, host[161], *hand;
  struct userrec *u;

  Context;
  strcpy(args, msg);
  cmd = newsplit(&args);
  simple_sprintf(host, "%s!%s", nick, from);
  u = get_user_by_host(host);
  hand = u ? u->handle : "*";
  get_user_flagrec(u, &fr, chname);
  Tcl_SetVar(interp, "_pub1", nick, 0);
  Tcl_SetVar(interp, "_pub2", from, 0);
  Tcl_SetVar(interp, "_pub3", hand, 0);
  Tcl_SetVar(interp, "_pub4", chname, 0);
  Tcl_SetVar(interp, "_pub5", args, 0);
  Context;
  x = check_tcl_bind(H_pub, cmd, &fr, " $_pub1 $_pub2 $_pub3 $_pub4 $_pub5",
		     MATCH_EXACT | BIND_USE_ATTR | BIND_HAS_BUILTINS);
  Context;
  if (x == BIND_NOMATCH)
    return 0;
  if (x == BIND_EXEC_LOG)
    putlog(LOG_CMDS, chname, "<<%s>> !%s! %s %s", nick, hand, cmd, args);
  return 1;
}

static void check_tcl_pubm(char *nick, char *from, char *chname, char *msg)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  char buf[1024], host[161];
  struct userrec *u;

  Context;
  simple_sprintf(buf, "%s %s", chname, msg);
  simple_sprintf(host, "%s!%s", nick, from);
  u = get_user_by_host(host);
  get_user_flagrec(u, &fr, chname);
  Tcl_SetVar(interp, "_pubm1", nick, 0);
  Tcl_SetVar(interp, "_pubm2", from, 0);
  Tcl_SetVar(interp, "_pubm3", u ? u->handle : "*", 0);
  Tcl_SetVar(interp, "_pubm4", chname, 0);
  Tcl_SetVar(interp, "_pubm5", msg, 0);
  Context;
  check_tcl_bind(H_pubm, buf, &fr, " $_pubm1 $_pubm2 $_pubm3 $_pubm4 $_pubm5",
		 MATCH_MASK | BIND_USE_ATTR | BIND_STACKABLE);
  Context;
}

static void check_tcl_need(char *chname, char *type)
{
  char buf[1024];

  simple_sprintf(buf, "%s %s", chname, type);
  Tcl_SetVar(interp, "_need1", chname, 0);
  Tcl_SetVar(interp, "_need2", type, 0);
  check_tcl_bind(H_need, buf, 0, " $_need1 $_need2", MATCH_MASK | BIND_STACKABLE);
}

static tcl_ints myints[] =
{
  {"learn-users",		&learn_users,		0},	/* arthur2 */
  {"wait-split",		&wait_split,		0},
  {"wait-info",			&wait_info,		0},
  {"bounce-bans",		&bounce_bans,		0},
  {"bounce-exempts",		&bounce_exempts,	0},
  {"bounce-invites",		&bounce_invites,	0},
  {"bounce-modes",		&bounce_modes,		0},
  {"modes-per-line",		&modesperline,		0},
  {"mode-buf-length",		&mode_buf_len,		0},
  {"use-354",			&use_354,		0},
  {"kick-method",		&kick_method,		0},
  {"kick-fun",			&kick_fun,		0},
  {"ban-fun",			&ban_fun,		0},
  {"invite-key",		&invite_key,		0},
  {"no-chanrec-info",		&no_chanrec_info,	0},
  {"max-bans",			&max_bans,		0},
  {"max-exempts",		&max_exempts,		0},
  {"max-invites",		&max_invites,		0},
  {"max-modes",			&max_modes,		0},
  {"net-type",			&net_type,		0},
  {"strict-host",		&strict_host,		0},	/* arthur2 */
  {"ctcp-mode",			&ctcp_mode,		0},	/* arthur2 */
  {"keep-nick",			&keepnick,		0},	/* guppy */
  {"prevent-mixing",		&prevent_mixing,	0},
  {"revenge-mode",		&revenge_mode,		0},
  {"rfc-compliant",		&rfc_compliant,		0},
  {NULL,			NULL,			0}	/* arthur2 */
};

/* Flush the modes for EVERY channel.
 */
static void flush_modes()
{
  struct chanset_t *chan;
  memberlist *m;

  chan = chanset;
  while (chan != NULL) {
    m = chan->channel.member;
    while (m && m->nick[0]) {
      if ((m->delay) && (now - m->delay) > 4) {
        add_mode(chan, '+', 'o', m->nick);
        m->delay = 0L;
      }
      m = m->next;
    }
    flush_mode(chan, NORMAL);
    chan = chan->next;
  }
}

static void irc_report(int idx, int details)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  char ch[1024], q[160], *p;
  int k, l;
  struct chanset_t *chan;

  strcpy(q, "Channels: ");
  k = 10;
  for (chan = chanset; chan; chan = chan->next) {
    if (idx != DP_STDOUT)
      get_user_flagrec(dcc[idx].user, &fr, chan->dname);
    if ((idx == DP_STDOUT) || glob_master(fr) || chan_master(fr)) {
      p = NULL;
      if (!channel_inactive(chan)) {
	if (chan->status & CHAN_JUPED)
	  p = MISC_JUPED;
	else if (!(chan->status & CHAN_ACTIVE))
	  p = MISC_TRYING;
	else if (chan->status & CHAN_PEND)
	  p = MISC_PENDING;
	else if (!me_op(chan))
	  p = MISC_WANTOPS;
      }
      l = simple_sprintf(ch, "%s%s%s%s, ", chan->dname, p ? "(" : "",
			 p ? p : "", p ? ")" : "");
      if ((k + l) > 70) {
	dprintf(idx, "   %s\n", q);
	strcpy(q, "          ");
	k = 10;
      }
      k += my_strcpy(q + k, ch);
    }
  }
  if (k > 10) {
    q[k - 2] = 0;
    dprintf(idx, "    %s\n", q);
  }
}

static void do_nettype()
{
  switch (net_type) {
  case 0:		/* Efnet */
    kick_method = 1;
    modesperline = 4;
    use_354 = 0;
    use_exempts = 0;
    use_invites = 0;
    rfc_compliant = 1;
    break;
  case 1:		/* Ircnet */
    kick_method = 4;
    modesperline = 3;
    use_354 = 0;
    use_exempts = 1;
    use_invites = 1;
    rfc_compliant = 1;
    break;
  case 2:		/* Undernet */
    kick_method = 1;
    modesperline = 6;
    use_354 = 1;
    use_exempts = 0;
    use_invites = 0;
    rfc_compliant = 1;
    break;
  case 3:		/* Dalnet */
    kick_method = 1;
    modesperline = 6;
    use_354 = 0;
    use_exempts = 0;
    use_invites = 0;
    rfc_compliant = 0;
    break;
  case 4:		/* hybrid-6+ */
    kick_method = 1;
    modesperline = 4;
    use_354 = 0;
    use_exempts = 1;
    use_invites = 0;
    rfc_compliant = 1;
    break;
  default:
    break;
  }
  /* Update all rfc_ function pointers */
  add_hook(HOOK_RFC_CASECMP, (Function) rfc_compliant);
}

static char *traced_nettype(ClientData cdata, Tcl_Interp * irp, char *name1,
			    char *name2, int flags)
{
  do_nettype();
  return NULL;
}

static char *traced_rfccompliant(ClientData cdata, Tcl_Interp * irp,
				 char *name1, char *name2, int flags)
{
  /* This hook forces eggdrop core to change the rfc_ match function
   * links to point to the rfc compliant versions if rfc_compliant
   * is 1, or to the normal version if it's 0.
   */
  add_hook(HOOK_RFC_CASECMP, (Function) rfc_compliant);
  return NULL;
}

static int irc_expmem()
{
  return 0;
}

static char *irc_close()
{
  struct chanset_t *chan;

  Context;
  for (chan = chanset; chan; chan = chan->next) {
    dprintf(DP_MODE, "PART %s\n", chan->name);
    clear_channel(chan, 1);
  }
  Context;
  del_bind_table(H_topc);
  del_bind_table(H_splt);
  del_bind_table(H_sign);
  del_bind_table(H_rejn);
  del_bind_table(H_part);
  del_bind_table(H_nick);
  del_bind_table(H_mode);
  del_bind_table(H_kick);
  del_bind_table(H_join);
  del_bind_table(H_pubm);
  del_bind_table(H_pub);
  del_bind_table(H_need);
  Context;
  rem_tcl_ints(myints);
  rem_builtins(H_dcc, irc_dcc);
  rem_builtins(H_msg, C_msg);
  rem_builtins(H_raw, irc_raw);
  rem_tcl_commands(tclchan_cmds);
  rem_help_reference("irc.help");
  Context;
  del_hook(HOOK_MINUTELY, (Function) check_expired_chanstuff);
  del_hook(HOOK_5MINUTELY, (Function) status_log);
  del_hook(HOOK_ADD_MODE, (Function) real_add_mode);
  del_hook(HOOK_IDLE, (Function) flush_modes);
  Tcl_UntraceVar(interp, "rfc-compliant",
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 traced_rfccompliant, NULL);
  Tcl_UntraceVar(interp, "net-type",
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 traced_nettype, NULL);
  Context;
  module_undepend(MODULE_NAME);
  Context;
  return NULL;
}

char *irc_start();

static Function irc_table[] =
{
  /* 0 - 3 */
  (Function) irc_start,
  (Function) irc_close,
  (Function) irc_expmem,
  (Function) irc_report,
  /* 4 - 7 */
  (Function) & H_splt,		/* p_tcl_bind_list		*/
  (Function) & H_rejn,		/* p_tcl_bind_list		*/
  (Function) & H_nick,		/* p_tcl_bind_list		*/
  (Function) & H_sign,		/* p_tcl_bind_list		*/
  /* 8 - 11 */
  (Function) & H_join,		/* p_tcl_bind_list		*/
  (Function) & H_part,		/* p_tcl_bind_list		*/
  (Function) & H_mode,		/* p_tcl_bind_list		*/
  (Function) & H_kick,		/* p_tcl_bind_list		*/
  /* 12 - 15 */
  (Function) & H_pubm,		/* p_tcl_bind_list		*/
  (Function) & H_pub,		/* p_tcl_bind_list		*/
  (Function) & H_topc,		/* p_tcl_bind_list		*/
  (Function) recheck_channel,
  /* 16 - 19 */
  (Function) me_op,
  (Function) recheck_channel_modes,
  (Function) & H_need,		/* p_tcl_bind_list		*/
};

char *irc_start(Function * global_funcs)
{
  struct chanset_t *chan;

  global = global_funcs;

  Context;
  module_register(MODULE_NAME, irc_table, 1, 2);
  if (!module_depend(MODULE_NAME, "eggdrop", 105, 3)) {
    module_undepend(MODULE_NAME);
    return "This module needs eggdrop1.5.3 or later";
  }
  if (!(server_funcs = module_depend(MODULE_NAME, "server", 1, 0))) {
    module_undepend(MODULE_NAME);
    return "You need the server module to use the irc module.";
  }
  if (!(channels_funcs = module_depend(MODULE_NAME, "channels", 1, 0))) {
    module_undepend(MODULE_NAME);
    return "You need the channels module to use the irc module.";
  }
  Context;
  for (chan = chanset; chan; chan = chan->next) {
    if (!channel_inactive(chan))
      dprintf(DP_MODE, "JOIN %s %s\n",
              (chan->name[0]) ? chan->name : chan->dname, chan->key_prot);      
    chan->status &= ~(CHAN_ACTIVE | CHAN_PEND | CHAN_ASKEDBANS);
    chan->ircnet_status &= ~(CHAN_ASKED_INVITED | CHAN_ASKED_EXEMPTS);
  }
  Context;
  add_hook(HOOK_MINUTELY, (Function) check_expired_chanstuff);
  add_hook(HOOK_5MINUTELY, (Function) status_log);
  add_hook(HOOK_ADD_MODE, (Function) real_add_mode);
  add_hook(HOOK_IDLE, (Function) flush_modes);
  Tcl_TraceVar(interp, "net-type",
	       TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
	       traced_nettype, NULL);
  Tcl_TraceVar(interp, "rfc-compliant",
	       TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
	       traced_rfccompliant, NULL);
  Context;
  add_tcl_ints(myints);
  add_builtins(H_dcc, irc_dcc);
  add_builtins(H_msg, C_msg);
  add_builtins(H_raw, irc_raw);
  add_tcl_commands(tclchan_cmds);
  add_help_reference("irc.help");
  Context;
  H_topc = add_bind_table("topc", HT_STACKABLE, channels_5char);
  H_splt = add_bind_table("splt", HT_STACKABLE, channels_4char);
  H_sign = add_bind_table("sign", HT_STACKABLE, channels_5char);
  H_rejn = add_bind_table("rejn", HT_STACKABLE, channels_4char);
  H_part = add_bind_table("part", HT_STACKABLE, channels_5char);
  H_nick = add_bind_table("nick", HT_STACKABLE, channels_5char);
  H_mode = add_bind_table("mode", HT_STACKABLE, channels_6char);
  H_kick = add_bind_table("kick", HT_STACKABLE, channels_6char);
  H_join = add_bind_table("join", HT_STACKABLE, channels_4char);
  H_pubm = add_bind_table("pubm", HT_STACKABLE, channels_5char);
  H_pub = add_bind_table("pub", 0, channels_5char);
  H_need = add_bind_table("need", HT_STACKABLE, channels_2char);
  Context;
  do_nettype();
  Context;
  return NULL;
}
