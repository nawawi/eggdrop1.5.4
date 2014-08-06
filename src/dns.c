/* 
 * dns.c -- handles:
 *   DNS resolve calls and events
 *   provides the code used by the bot if the DNS module is not loaded
 *   DNS Tcl commands
 * 
 * $Id: dns.c,v 1.17 2000/05/06 22:00:31 fabian Exp $
 */
/* 
 * Written by Fabian Knittel <fknittel@gmx.de>
 * 
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
#include <netdb.h>
#include <setjmp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dns.h"

extern struct dcc_t	*dcc;
extern int		 dcc_total;
extern int		 resolve_timeout;
extern time_t		 now;
extern jmp_buf		 alarmret;
extern Tcl_Interp	*interp;

devent_t	*dns_events = NULL;


/*
 *   DCC functions
 */

void dcc_dnswait(int idx, char *buf, int len)
{
  /* Ignore anything now. */
  Context;
}

void eof_dcc_dnswait(int idx)
{
  Context;
  putlog(LOG_MISC, "*", "Lost connection while resolving hostname [%s/%d]",
	 iptostr(dcc[idx].addr), dcc[idx].port);
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

static void display_dcc_dnswait(int idx, char *buf)
{
  sprintf(buf, "dns   waited %lus", now - dcc[idx].timeval);
}

static int expmem_dcc_dnswait(void *x)
{
  register struct dns_info *p = (struct dns_info *) x;
  int size = 0;

  Context;
  if (p) {
    size = sizeof(struct dns_info);
    if (p->host)
      size += strlen(p->host) + 1;
    if (p->cbuf)
      size += strlen(p->cbuf) + 1;
  }
  return size;
}

static void kill_dcc_dnswait(int idx, void *x)
{
  register struct dns_info *p = (struct dns_info *) x;

  Context;
  if (p) {
    if (p->host)
      nfree(p->host);
    if (p->cbuf)
      nfree(p->cbuf);
    nfree(p);
  }
}

struct dcc_table DCC_DNSWAIT =
{
  "DNSWAIT",
  DCT_VALIDIDX,
  eof_dcc_dnswait,
  dcc_dnswait,
  0,
  0,
  display_dcc_dnswait,
  expmem_dcc_dnswait,
  kill_dcc_dnswait,
  0
};


/*
 *   DCC events
 */

/* Walk through every dcc entry and look for waiting DNS requests
 * of RES_HOSTBYIP for our IP address.
 */
static void dns_dcchostbyip(IP ip, char *hostn, int ok, void *other)
{
  int idx;

  Context;
  for (idx = 0; idx < dcc_total; idx++) {
    if ((dcc[idx].type == &DCC_DNSWAIT) &&
        (dcc[idx].u.dns->dns_type == RES_HOSTBYIP) &&
        (dcc[idx].u.dns->ip == ip)) {
      if (dcc[idx].u.dns->host)
        nfree(dcc[idx].u.dns->host);
      dcc[idx].u.dns->host = get_data_ptr(strlen(hostn) + 1);
      strcpy(dcc[idx].u.dns->host, hostn);
      if (ok)
        dcc[idx].u.dns->dns_success(idx);
      else
        dcc[idx].u.dns->dns_failure(idx);
    }
  }
}

/* Walk through every dcc entry and look for waiting DNS requests
 * of RES_IPBYHOST for our hostname.
 */
static void dns_dccipbyhost(IP ip, char *hostn, int ok, void *other)
{
  int idx;

  Context;
  for (idx = 0; idx < dcc_total; idx++) {
    if ((dcc[idx].type == &DCC_DNSWAIT) &&
        (dcc[idx].u.dns->dns_type == RES_IPBYHOST) &&
        !egg_strcasecmp(dcc[idx].u.dns->host, hostn)) {
      dcc[idx].u.dns->ip = ip;
      if (ok)
        dcc[idx].u.dns->dns_success(idx);
      else
        dcc[idx].u.dns->dns_failure(idx);
    }
  }
}

static int dns_dccexpmem(void *other)
{
  return 0;
}

devent_type DNS_DCCEVENT_HOSTBYIP = {
  "DCCEVENT_HOSTBYIP",
  dns_dccexpmem,
  dns_dcchostbyip
};

devent_type DNS_DCCEVENT_IPBYHOST = {
  "DCCEVENT_IPBYHOST",
  dns_dccexpmem,
  dns_dccipbyhost
};

void dcc_dnsipbyhost(char *hostn)
{
  devent_t *de = dns_events;

  Context;
  while (de) {
    if (de->type && (de->type == &DNS_DCCEVENT_IPBYHOST) &&
	(de->lookup == RES_IPBYHOST)) {
      if (de->res_data.hostname &&
	  !egg_strcasecmp(de->res_data.hostname, hostn))
	/* No need to add anymore. */
	return;
    }
    de = de->next;
  }

  de = nmalloc(sizeof(devent_t));
  egg_bzero(de, sizeof(devent_t));

  /* Link into list. */
  de->next = dns_events;
  dns_events = de;

  de->type = &DNS_DCCEVENT_IPBYHOST;
  de->lookup = RES_IPBYHOST;
  de->res_data.hostname = nmalloc(strlen(hostn) + 1);
  strcpy(de->res_data.hostname, hostn);

  /* Send request. */
  dns_ipbyhost(hostn);
}

void dcc_dnshostbyip(IP ip)
{
  devent_t *de = dns_events;

  Context;
  while (de) {
    if (de->type && (de->type == &DNS_DCCEVENT_HOSTBYIP) &&
	(de->lookup == RES_HOSTBYIP)) {
      if (de->res_data.ip_addr == ip)
	/* No need to add anymore. */
	return;
    }
    de = de->next;
  }

  de = nmalloc(sizeof(devent_t));
  egg_bzero(de, sizeof(devent_t));

  /* Link into list. */
  de->next = dns_events;
  dns_events = de;

  de->type = &DNS_DCCEVENT_HOSTBYIP;
  de->lookup = RES_HOSTBYIP;
  de->res_data.ip_addr = ip;

  /* Send request. */
  dns_hostbyip(ip);
}


/*
 *   Tcl events
 */

static void dns_tcl_iporhostres(IP ip, char *hostn, int ok, void *other)
{
  devent_tclinfo_t *tclinfo = (devent_tclinfo_t *) other;
  
  Context;
  if (Tcl_VarEval(interp, tclinfo->proc, " ", iptostr(my_htonl(ip)), " ",
		  hostn, ok ? " 1" : " 0", tclinfo->paras, NULL) == TCL_ERROR)
    putlog(LOG_MISC, "*", DCC_TCLERROR, tclinfo->proc, interp->result);

  /* Free the memory. It will be unused after this event call. */
  nfree(tclinfo->proc);
  if (tclinfo->paras)
    nfree(tclinfo->paras);
  nfree(tclinfo);
}

static int dns_tclexpmem(void *other)
{
  devent_tclinfo_t *tclinfo = (devent_tclinfo_t *) other;
  int l = 0;

  if (tclinfo) {
    l = sizeof(devent_tclinfo_t);
    if (tclinfo->proc)
      l += strlen(tclinfo->proc) + 1;
    if (tclinfo->paras)
      l += strlen(tclinfo->paras) + 1;
  }
  return l;
}

devent_type DNS_TCLEVENT_HOSTBYIP = {
  "TCLEVENT_HOSTBYIP",
  dns_tclexpmem,
  dns_tcl_iporhostres
};

devent_type DNS_TCLEVENT_IPBYHOST = {
  "TCLEVENT_IPBYHOST",
  dns_tclexpmem,
  dns_tcl_iporhostres
};

static void tcl_dnsipbyhost(char *hostn, char *proc, char *paras)
{
  devent_t *de;
  devent_tclinfo_t *tclinfo;

  Context;
  de = nmalloc(sizeof(devent_t));
  egg_bzero(de, sizeof(devent_t));

  /* Link into list. */
  de->next = dns_events;
  dns_events = de;

  de->type = &DNS_TCLEVENT_IPBYHOST;
  de->lookup = RES_IPBYHOST;
  de->res_data.hostname = nmalloc(strlen(hostn) + 1);
  strcpy(de->res_data.hostname, hostn);

  /* Store additional data. */
  tclinfo = nmalloc(sizeof(devent_tclinfo_t));
  tclinfo->proc = nmalloc(strlen(proc) + 1);
  strcpy(tclinfo->proc, proc);
  if (paras) {
    tclinfo->paras = nmalloc(strlen(paras) + 1);
    strcpy(tclinfo->paras, paras);
  } else
    tclinfo->paras = NULL;
  de->other = tclinfo;

  /* Send request. */
  dns_ipbyhost(hostn);
}

static void tcl_dnshostbyip(IP ip, char *proc, char *paras)
{
  devent_t *de;
  devent_tclinfo_t *tclinfo;

  Context;
  de = nmalloc(sizeof(devent_t));
  egg_bzero(de, sizeof(devent_t));

  /* Link into list. */
  de->next = dns_events;
  dns_events = de;

  de->type = &DNS_TCLEVENT_HOSTBYIP;
  de->lookup = RES_HOSTBYIP;
  de->res_data.ip_addr = ip;

  /* Store additional data. */
  tclinfo = nmalloc(sizeof(devent_tclinfo_t));
  tclinfo->proc = nmalloc(strlen(proc) + 1);
  strcpy(tclinfo->proc, proc);
  if (paras) {
    tclinfo->paras = nmalloc(strlen(paras) + 1);
    strcpy(tclinfo->paras, paras);
  } else
    tclinfo->paras = NULL;
  de->other = tclinfo;

  /* Send request. */
  dns_hostbyip(ip);
}


/*
 *    Event functions
 */

inline static int dnsevent_expmem(void)
{
  devent_t *de = dns_events;
  int tot = 0;

  Context;
  while (de) {
    tot += sizeof(devent_t);
    if ((de->lookup == RES_IPBYHOST) && de->res_data.hostname)
      tot += strlen(de->res_data.hostname) + 1;
    if (de->type && de->type->expmem)
      tot += de->type->expmem(de->other);
    de = de->next;
  }
  return tot;
}

void call_hostbyip(IP ip, char *hostn, int ok)
{
  devent_t *de = dns_events, *ode = NULL, *nde = NULL;

  Context;
  while (de) {
    nde = de->next;
    if ((de->lookup == RES_HOSTBYIP) &&
	(!de->res_data.ip_addr || (de->res_data.ip_addr == ip))) {
      /* Remove the event from the list here, to avoid conflicts if one of
       * the event handlers re-adds another event. */
      if (ode)
	ode->next = de->next;
      else
	dns_events = de->next;

      if (de->type && de->type->event)
	de->type->event(ip, hostn, ok, de->other);
      else
	putlog(LOG_MISC, "*", "(!) Unknown DNS event type found: %s",
	       (de->type && de->type->name) ? de->type->name : "<empty>");
      nfree(de);
      de = ode;
    }
    ode = de;
    de = nde;
  }
}

void call_ipbyhost(char *hostn, IP ip, int ok)
{
  devent_t *de = dns_events, *ode = NULL, *nde = NULL;

  Context;
  while (de) {
    nde = de->next;
    if ((de->lookup == RES_IPBYHOST) &&
	(!de->res_data.hostname ||
	 !egg_strcasecmp(de->res_data.hostname, hostn))) {
      /* Remove the event from the list here, to avoid conflicts if one of
       * the event handlers re-adds another event. */
      if (ode)
	ode->next = de->next;
      else
	dns_events = de->next;

      if (de->type && de->type->event)
	de->type->event(ip, hostn, ok, de->other);
      else
	putlog(LOG_MISC, "*", "(!) Unknown DNS event type found: %s",
	       (de->type && de->type->name) ? de->type->name : "<empty>");

      if (de->res_data.hostname)
	nfree(de->res_data.hostname);
      nfree(de);
      de = ode;
    }
    ode = de;
    de = nde;
  }
}


/* 
 *    Async DNS emulation functions
 */

void block_dns_hostbyip(IP ip)
{
  struct hostent *hp;
  unsigned long addr = my_htonl(ip);
  static char s[UHOSTLEN];

  Context;
  if (!setjmp(alarmret)) {
    alarm(resolve_timeout);
    hp = gethostbyaddr((char *) &addr, sizeof(addr), AF_INET);
    alarm(0);
    if (hp) {
      strncpy(s, hp->h_name, UHOSTLEN - 1);
      s[UHOSTLEN - 1] = 0;
    } else
      strcpy(s, iptostr(addr));
  } else {
    hp = NULL;
    strcpy(s, iptostr(addr));
  }
  /* Call hooks. */
  call_hostbyip(ip, s, hp ? 1 : 0);
  Context;
}

void block_dns_ipbyhost(char *host)
{
  struct in_addr inaddr;

  Context;
  /* Check if someone passed us an IP address as hostname 
   * and return it straight away */
  if (egg_inet_aton(host, &inaddr)) {
    call_ipbyhost(host, my_ntohl(inaddr.s_addr), 1);
    return;
  }
  if (!setjmp(alarmret)) {
    struct hostent *hp;
    struct in_addr *in;
    IP ip = 0;

    alarm(resolve_timeout);
    hp = gethostbyname(host);
    alarm(0);

    if (hp) {
      in = (struct in_addr *) (hp->h_addr_list[0]);
      ip = (IP) (in->s_addr);
      call_ipbyhost(host, my_ntohl(ip), 1);
      return;
    }
    /* Fall through. */
  }
  call_ipbyhost(host, 0, 0);
  Context;
}


/*
 *   Misc functions
 */

int expmem_dns(void)
{
  return dnsevent_expmem();
}


/*
 *   Tcl functions
 */

/* dnslookup <ip-address> <proc> */
static int tcl_dnslookup STDVAR
{
  struct in_addr inaddr;
  char *paras = NULL;
 
  Context;
  if (argc < 3) {
    Tcl_AppendResult(irp, "wrong # args: should be \"", argv[0],
		     " ip-address/hostname proc ?args...?\"", NULL);
    return TCL_ERROR;
  }

  if (argc > 3) {
    int l = 0, p;

    /* Create a string with a leading space out of all provided
     * additional parameters.
     */
    paras = nmalloc(1);
    paras[0] = 0;
    for (p = 3; p < argc; p++) {
      l += strlen(argv[p]) + 1;
      paras = nrealloc(paras, l + 1);
      strcat(paras, " ");
      strcat(paras, argv[p]);
    }
  }

  if (egg_inet_aton(argv[1], &inaddr))
    tcl_dnshostbyip(ntohl(inaddr.s_addr), argv[2], paras);
  else
    tcl_dnsipbyhost(argv[1], argv[2], paras);
  if (paras)
    nfree(paras);
  return TCL_OK;
}

tcl_cmds tcldns_cmds[] =
{
  {"dnslookup",	tcl_dnslookup},
  {NULL,	NULL}
};
