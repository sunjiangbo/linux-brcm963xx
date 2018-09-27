/* SIP extension for IP connection tracking.
 *
 * (C) 2005 by Christian Hentschel <chentschel@arnet.com.ar>
 * based on RR's ip_conntrack_ftp.c and other modules.
 * (C) 2007 United Security Providers
 * (C) 2007, 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/skbuff.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/netfilter.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <linux/netfilter/nf_conntrack_sip.h>

#if defined(CONFIG_BCM_KF_NETFILTER) && !defined(CONFIG_ZYXEL_USE_LINUX_SIP_ALG)
#include <net/netfilter/nf_conntrack_tuple.h>
#include <linux/iqos.h>
#endif


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hentschel <chentschel@arnet.com.ar>");
MODULE_DESCRIPTION("SIP connection tracking helper");
MODULE_ALIAS("ip_conntrack_sip");
MODULE_ALIAS_NFCT_HELPER("sip");

#define MAX_PORTS	8
static unsigned short ports[MAX_PORTS];
static unsigned int ports_c;
module_param_array(ports, ushort, &ports_c, 0400);
MODULE_PARM_DESC(ports, "port numbers of SIP servers");

static unsigned int sip_timeout __read_mostly = SIP_TIMEOUT;
module_param(sip_timeout, uint, 0600);
MODULE_PARM_DESC(sip_timeout, "timeout for the master SIP session");

#if defined(CONFIG_BCM_KF_NETFILTER) && !defined(CONFIG_ZYXEL_USE_LINUX_SIP_ALG)
int (*nf_nat_addr_hook)(struct sk_buff *skb, unsigned int protoff,
			struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			char **dptr, int *dlen, char **addr_begin,
			int *addr_len, struct nf_conntrack_man *addr);
EXPORT_SYMBOL_GPL(nf_nat_addr_hook);

int (*nf_nat_rtp_hook)(struct sk_buff *skb, unsigned int protoff,
		       struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		       char **dptr, int *dlen, struct nf_conntrack_expect *exp,
		       char **port_begin, int *port_len);
EXPORT_SYMBOL_GPL(nf_nat_rtp_hook);

int (*nf_nat_snat_hook)(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			struct nf_conntrack_expect *exp);
EXPORT_SYMBOL_GPL(nf_nat_snat_hook);

int (*nf_nat_sip_hook)(struct sk_buff *skb, unsigned int protoff,
		       struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		       char **dptr, int *dlen, struct nf_conntrack_expect *exp,
		       char **addr_begin, int *addr_len);
EXPORT_SYMBOL_GPL(nf_nat_sip_hook);

struct sip_header_nfo {
	const char	*lname;
	const char	*sname;
	const char	*ln_str;
	size_t		lnlen;
	size_t		snlen;
	size_t		ln_strlen;
	int		case_sensitive;
	int		(*match_len)(struct nf_conn *, const char *,
				     const char *, int *);
};

static const struct sip_header_nfo ct_sip_hdrs[] = {
	[POS_VIA] = { 		/* SIP Via header */
		.lname		= "Via:",
		.lnlen		= sizeof("Via:") - 1,
		.sname		= "\r\nv:",
		.snlen		= sizeof("\r\nv:") - 1, /* rfc3261 "\r\n" */
		.ln_str		= "UDP ",
		.ln_strlen	= sizeof("UDP ") - 1,
	},
	[POS_CONTACT] = { 	/* SIP Contact header */
		.lname		= "Contact:",
		.lnlen		= sizeof("Contact:") - 1,
		.sname		= "\r\nm:",
		.snlen		= sizeof("\r\nm:") - 1,
		.ln_str		= "sip:",
		.ln_strlen	= sizeof("sip:") - 1,
	},
	[POS_CONTENT] = { 	/* SIP Content length header */
		.lname		= "Content-Length:",
		.lnlen		= sizeof("Content-Length:") - 1,
		.sname		= "\r\nl:",
		.snlen		= sizeof("\r\nl:") - 1,
		.ln_str		= NULL,
		.ln_strlen	= 0,
	},
	[POS_OWNER_IP4] = {	/* SDP owner address*/
		.case_sensitive	= 1,
		.lname		= "\no=",
		.lnlen		= sizeof("\no=") - 1,
		.sname		= "\ro=",
		.snlen		= sizeof("\ro=") - 1,
		.ln_str		= "IN IP4 ",
		.ln_strlen	= sizeof("IN IP4 ") - 1,
	},
	[POS_CONNECTION_IP4] = {/* SDP connection info */
		.case_sensitive	= 1,
		.lname		= "\nc=",
		.lnlen		= sizeof("\nc=") - 1,
		.sname		= "\rc=",
		.snlen		= sizeof("\rc=") - 1,
		.ln_str		= "IN IP4 ",
		.ln_strlen	= sizeof("IN IP4 ") - 1,
	},
	[POS_ANAT] = {		/* SDP Alternative Network Address Types */
		.case_sensitive	= 1,
		.lname		= "\na=",
		.lnlen		= sizeof("\na=") - 1,
		.sname		= "\ra=",
		.snlen		= sizeof("\ra=") - 1,
		.ln_str		= "alt:",
		.ln_strlen	= sizeof("alt:") - 1,
	},
	[POS_MEDIA_AUDIO] = {		/* SDP media audio info */
		.case_sensitive	= 1,
		.lname		= "\nm=audio ",
		.lnlen		= sizeof("\nm=audio ") - 1,
		.sname		= "\rm=audio ",
		.snlen		= sizeof("\rm=audio ") - 1,
		.ln_str		= NULL,
		.ln_strlen	= 0,
	},
	[POS_MEDIA_VIDEO] = {		/* SDP media video info */
		.case_sensitive	= 1,
		.lname		= "\nm=video ",
		.lnlen		= sizeof("\nm=video ") - 1,
		.sname		= "\rm=video ",
		.snlen		= sizeof("\rm=video ") - 1,
		.ln_str		= NULL,
		.ln_strlen	= 0,
	},
	[POS_ARTCP_IP4] = {		/* SDP RTCP attribute */
		.case_sensitive	= 1,
		.lname		= "\na=rtcp:",
		.lnlen		= sizeof("\na=rtcp:") - 1,
		.sname		= "\ra=rtcp:",
		.snlen		= sizeof("\ra=rtcp:") - 1,
		.ln_str		= " IN IP4 ",
		.ln_strlen	= sizeof(" IN IP4 ") - 1,
	},
};

//BRCM: move these vars here to allow sip_help() use it
static struct nf_conntrack_helper sip[MAX_PORTS][2] __read_mostly;
static char sip_names[MAX_PORTS][2][sizeof("sip-65535")] __read_mostly;
static const struct nf_conntrack_expect_policy
sip_exp_policy[SIP_EXPECT_CLASS_MAX + 1] = {
	[SIP_EXPECT_CLASS_SIGNALLING] = {
		.max_expected	= 1,
		.timeout	= 3 * 60,
	},
	[SIP_EXPECT_CLASS_AUDIO] = {
		.max_expected	= 2 * IP_CT_DIR_MAX,
		.timeout	= 3 * 60,
	},
	[SIP_EXPECT_CLASS_VIDEO] = {
		.max_expected	= 2 * IP_CT_DIR_MAX,
		.timeout	= 3 * 60,
	},
	[SIP_EXPECT_CLASS_OTHER] = {
		.max_expected	= 2 * IP_CT_DIR_MAX,
		.timeout	= 3 * 60,
	},
};

#if 0 // Don't register the helper such that fc can accelerate the RTP streams.
static int rtp_help(struct sk_buff *skb, unsigned int protoff,
		    struct nf_conn *ct, enum ip_conntrack_info ctinfo)
{
	return NF_ACCEPT;
}

/* Null RTP helper to avoid flow cache bypassing it */
static struct nf_conntrack_helper nf_conntrack_helper_rtp __read_mostly = {
	.name			= "RTP",
	.me			= THIS_MODULE,
	.help			= rtp_help
};
#endif

int find_inline_str(char **begin, char *end, const char *str, int str_len,
		    int case_sensitive)
{
	char *p = *begin;
	char *q = end - str_len;

	if (!str || str_len == 0)
		return 1;

	while(p <= q && *p != '\r' && *p != '\n') {
		if (case_sensitive) {
			if (strncmp(p, str, str_len) == 0)
				goto found;
		} else {
			if (strnicmp(p, str, str_len) == 0)
				goto found;
		}
		p++;
	}
	return 0;
found:
	*begin = p + str_len;
	return 1;
}

int find_field(char **begin, char *end, int field)
{
	const struct sip_header_nfo *hnfo = &ct_sip_hdrs[field];
	char *p = *begin;
	char *q = end - hnfo->lnlen;

	while (p <= q) {
		if (hnfo->lname == NULL ||
		    (strncmp(p, hnfo->lname, hnfo->lnlen) == 0)) {
		    	p += hnfo->lnlen;
		} else {
			if (hnfo->sname != NULL &&
			    strncmp(p, hnfo->sname, hnfo->snlen) == 0) {
			    	p += hnfo->snlen;
			} else {
				p++;
				continue;
			}
		}
		if (!find_inline_str(&p, end, hnfo->ln_str, hnfo->ln_strlen,
				     hnfo->case_sensitive)) {
			pr_debug("'%s' not found in '%s'.\n", hnfo->ln_str,
			       	 hnfo->lname);
			return 0;
		}
		*begin = p;
		return 1;
	}
	return 0;
}

int parse_digits(char **begin, char *end, int *n)
{
	char *p = *begin;
	char *q;
	long num;

	/* Skip spaces */
	while (*p == ' ')
		p++;
	
	if (!isdigit((int)*p))
		return 0;

	num = simple_strtol(p, &q, 10);
	if (q == p)
		return 0;
	if (n)
		*n = (int)num;
	*begin = p;
	return q - p;
}

int parse_addr(char **begin, char *end, struct nf_conntrack_man *addr)
{
	char *p;

	memset(addr, 0, sizeof(*addr));
	if (in4_pton((const char *)*begin, end - *begin, (u8 *)&addr->u3.ip,
		     -1, (const char **)&p))
		addr->l3num = AF_INET;
	else if (in6_pton(**begin == '[' ? (const char *)(*begin + 1) : (const char *)*begin, end - *begin,
			  (u8 *)&addr->u3.ip6, -1, (const char **)&p))
		addr->l3num = AF_INET6;
	else
		return 0;

	addr->u.all = 0;

	return (*p == ']') ? (p - *begin + 1) : (p - *begin);
}

int parse_sip_uri(char **begin, char *end, struct nf_conntrack_man *addr)
{
	char *p = *begin;
	char *p0;
	int port;
	int len;

	/* Search for '@' in this line */
	while (p < end && *p != '\r' && *p != '\n' && *p != ';') {
		if (*p == '@')
			break;
		p++;
	}

	/* We found user part */
	if (*p == '@')
		p0 = ++p;
	/* No user part */
	else 
		p = p0 = *begin;

	/* Address */
	if ((len = parse_addr(&p, end, addr)) == 0)
		return 0;

	/* Port number */
	if (p[len] == ':') {
		p += len + 1;
		if ((len = parse_digits(&p, end, &port)) == 0)
			return 0;
		if (port < 1 || port > 65535)
			return 0;
		addr->u.all = htons((unsigned short)port);
	} else {
		addr->u.all = 0;
	}

	*begin = p0;
	return p + len - p0;
}

static int process_owner(struct sk_buff *skb, unsigned int protoff,
			 struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			 char **dptr, int *dlen, struct nf_conntrack_man *addr)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int ret = NF_ACCEPT;
	char *p = *dptr;
	int len;
	struct nf_conntrack_man a;
	typeof(nf_nat_addr_hook) nf_nat_addr;

	if (!find_field(&p, *dptr+*dlen, POS_OWNER_IP4))
		goto end;
	if ((len = parse_addr(&p, *dptr+*dlen, &a)) == 0)
		goto end;  // brcm: that might be an owner with SIP URL, let him go.
	pr_debug("nf_conntrack_sip: owner=%.*s\n", len, p);
	*addr = a;

	/* We care only LAN->WAN situations */
	if (!memcmp(&ct->tuplehash[dir].tuple.src.u3,
		    &ct->tuplehash[!dir].tuple.dst.u3,
		    sizeof(ct->tuplehash[dir].tuple.src.u3)))
		goto end;

	/* LAN->WAN. Change the LAN IP to WAN. */
	if (!memcmp(&a.u3, &ct->tuplehash[dir].tuple.src.u3, sizeof(a.u3)) &&
	    (nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
	    	a.u3 = ct->tuplehash[!dir].tuple.dst.u3;
	    	ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr, dlen, &p,
				  &len, &a);
	    	pr_debug("nf_conntrack_sip: owner changed to %.*s\n",
		       	 len, p);
	}
	/* LAN->WAN, with firewall's external IP address that has been set by
	 * some 'smart' UAs. We need to change the parsed IP to LAN. */ 
	else if (!memcmp(&a.u3, &ct->tuplehash[!dir].tuple.dst.u3,
			 sizeof(a.u3))) {
		addr->u3 = ct->tuplehash[dir].tuple.src.u3;
		pr_debug("nf_conntrack_sip: owner is auto-detected WAN "
		       	 "address\n");
	}
end:
	return ret;
}

static int process_connection(struct sk_buff *skb, unsigned int protoff,
			      struct nf_conn *ct,
			      enum ip_conntrack_info ctinfo, char **dptr,
			      int *dlen, struct nf_conntrack_man *addr)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int ret = NF_ACCEPT;
	char *p = *dptr;
	int len;
	struct nf_conntrack_man a;
	typeof(nf_nat_addr_hook) nf_nat_addr;

	/* We care only LAN->WAN situations */
	if (!memcmp(&ct->tuplehash[dir].tuple.src.u3,
		    &ct->tuplehash[!dir].tuple.dst.u3,
		    sizeof(ct->tuplehash[dir].tuple.src.u3)))
		goto end;

	while (find_field(&p, *dptr+*dlen, POS_CONNECTION_IP4)) {
		if ((len = parse_addr(&p, *dptr+*dlen, &a)) == 0)
			goto err;
		pr_debug("nf_conntrack_sip: connection=%.*s\n", len, p);
		*addr = a;
		
		/* LAN->WAN. Change the LAN IP to WAN. */
		if (!memcmp(&a.u3, &ct->tuplehash[dir].tuple.src.u3, sizeof(a.u3)) &&
		    (nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
			a.u3 = ct->tuplehash[!dir].tuple.dst.u3;
			ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr, dlen, &p,
					  &len, &a);
			pr_debug("nf_conntrack_sip: connection changed to %.*s\n",
				 len, p);
		}
		/* LAN->WAN, with firewall's external IP address that has been set by
		 * some 'smart' UAs. We need to change the parsed IP to LAN. */ 
		else if (!memcmp(&a.u3, &ct->tuplehash[!dir].tuple.dst.u3,
				 sizeof(a.u3))) {
			addr->u3 = ct->tuplehash[dir].tuple.src.u3;
			pr_debug("nf_conntrack_sip: connection is auto-detected WAN "
				 "address\n");
		}

		p += len;
	}
end:
	return ret;
err:
	return NF_DROP;
}

static void iqos_expect(struct nf_conn *new, struct nf_conntrack_expect *exp)
{
	/* register the SIP Data RTP/RTCP ports with ingress QoS classifier */
	pr_debug("adding iqos from %pI4:%hu->%pI4:%hu\n",
		 &new->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.ip,
		 ntohs(new->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.all),
		 &new->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3.ip,
		 ntohs(new->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.all));

	iqos_add_L4port(IPPROTO_UDP, new->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u.udp.port, 
			IQOS_ENT_DYN, IQOS_PRIO_HIGH );
	iqos_add_L4port( IPPROTO_UDP, new->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.udp.port, 
			 IQOS_ENT_DYN, IQOS_PRIO_HIGH );
}

static int expect_rtp(struct sk_buff *skb, unsigned int protoff,
		      struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		      char **dptr, int *dlen, char **port_begin, int *port_len,
		      struct nf_conntrack_man *addr, int class)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	struct nf_conntrack_expect *exp;
	typeof(nf_nat_rtp_hook) nf_nat_rtp;
	typeof(nf_nat_snat_hook) nf_nat_snat;
	int ret = NF_ACCEPT;

	if ((exp = nf_ct_expect_alloc(ct)) == NULL)
		return ret;
	nf_ct_expect_init(exp, class, addr->l3num, NULL, &addr->u3,
			  IPPROTO_UDP, NULL, &addr->u.all);
	/* Set the child connection as slave (disconnected when master
	 * disconnects */
	exp->flags |= NF_CT_EXPECT_DERIVED_TIMEOUT;
	exp->derived_timeout = 0xFFFFFFFF;
	exp->expectfn= iqos_expect;
	// Don't register the helper such that fc can accelerate the RTP streams.
	// exp->helper = &nf_conntrack_helper_rtp;
	pr_debug("nf_conntrack_sip: expect_rtp %pI4:%hu->%pI4:%hu\n",
	       	 &exp->tuple.src.u3.ip, ntohs(exp->tuple.src.u.udp.port),
	       	 &exp->tuple.dst.u3.ip, ntohs(exp->tuple.dst.u.udp.port));

	if (memcmp(&ct->tuplehash[dir].tuple.src.u3,
	    	   &ct->tuplehash[!dir].tuple.dst.u3,
		   sizeof(ct->tuplehash[dir].tuple.src.u3)) &&
	    (nf_nat_rtp = rcu_dereference(nf_nat_rtp_hook))) {
		ret = nf_nat_rtp(skb, protoff, ct, ctinfo, dptr, dlen, exp,
				 port_begin, port_len);
	} else if (!memcmp(&ct->tuplehash[dir].tuple.src.u3,
	    	   	   &ct->tuplehash[!dir].tuple.dst.u3,
		   	   sizeof(ct->tuplehash[dir].tuple.src.u3)) &&
	    	   (nf_nat_snat = rcu_dereference(nf_nat_snat_hook))) {
			ret = nf_nat_snat(ct, ctinfo, exp);
	} else {
		if (nf_ct_expect_related(exp) != 0) {
			pr_debug("nf_conntrack_sip: nf_ct_expect_related() "
				 "failed\n");
		}
	}
	nf_ct_expect_put(exp);

	return ret;
}

static int process_audio(struct sk_buff *skb, unsigned int protoff,
			 struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			 char **dptr, int *dlen, struct nf_conntrack_man *addr)
{
	char *p = *dptr;
	int port;
	int len;
#if 1 //pochao: merge from P2812 fxs to ip phone can't work
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
#endif

	if (!find_field(&p, *dptr+*dlen, POS_MEDIA_AUDIO))
		return NF_ACCEPT;
	if ((len = parse_digits(&p, *dptr+*dlen, &port)) == 0)
		return NF_DROP;
	pr_debug("nf_conntrack_sip: audio=%d\n", port);
	addr->u.all = htons((u_int16_t)port);
#if 1 //pochao: merge from P2812 fxs to ip phone can't work
	if (!memcmp(&ct->tuplehash[dir].tuple.src.u3,
		    &ct->tuplehash[!dir].tuple.dst.u3,
		    sizeof(ct->tuplehash[dir].tuple.src.u3)))
		return NF_ACCEPT;
#endif
	len = expect_rtp(skb, protoff, ct, ctinfo, dptr, dlen, &p, &len,
			 addr, SIP_EXPECT_CLASS_AUDIO);
	return len;
}

static int process_video(struct sk_buff *skb, unsigned int protoff,
			 struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			 char **dptr, int *dlen, struct nf_conntrack_man *addr)
{
	char *p = *dptr;
	int port;
	int len;

	if (!find_field(&p, *dptr+*dlen, POS_MEDIA_VIDEO))
		return NF_ACCEPT;
	if ((len = parse_digits(&p, *dptr+*dlen, &port)) == 0)
		return NF_DROP;
	pr_debug("nf_conntrack_sip: video=%d\n", port);
	addr->u.all = htons((u_int16_t)port);
	return expect_rtp(skb, protoff, ct, ctinfo, dptr, dlen, &p, &len,
			  addr, SIP_EXPECT_CLASS_VIDEO);
}

static int process_anat(struct sk_buff *skb, unsigned int protoff,
			struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			char **dptr, int *dlen)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int ret = NF_ACCEPT;
	char *p = *dptr;
	int port;
	int len;
	struct nf_conntrack_man addr;
	typeof(nf_nat_addr_hook) nf_nat_addr;

	while (find_field(&p, *dptr+*dlen, POS_ANAT)) {
		int count;

		/* There are 5 spaces in the leading parameters */
		count = 0;
		while(p < *dptr+*dlen && *p != '\r' && *p != '\n') {
			if(*p++ == ' ') {
				if (++count == 5)
					break;
			}
		}
		if (count < 5)
			continue;

		if ((len = parse_addr(&p, *dptr+*dlen, &addr)) == 0)
			continue;
		pr_debug("nf_conntrack_sip: alt ip=%.*s\n", len, p);
		if (memcmp(&addr.u3, &ct->tuplehash[dir].tuple.src.u3,
		    sizeof(addr.u3)))
	    		continue;
		if (memcmp(&ct->tuplehash[dir].tuple.src.u3,
			   &ct->tuplehash[!dir].tuple.dst.u3,
			   sizeof(ct->tuplehash[dir].tuple.src.u3)) &&
		    (nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
	    		addr.u3 = ct->tuplehash[!dir].tuple.dst.u3;
	    		ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr,
					  dlen, &p, &len, &addr);
			if (ret != NF_ACCEPT)
				break;
			pr_debug("nf_conntrack_sip: alt ip changed to %.*s\n",
			       	 len, p);
		}

		/* Port */
		p += len + 1;
		if ((len = parse_digits(&p, *dptr+*dlen, &port)) == 0)
			return NF_DROP;
		pr_debug("nf_conntrack_sip: alt port=%.*s\n", len, p);
		addr.u.all = htons((u_int16_t)port);
		ret = expect_rtp(skb, protoff, ct, ctinfo, dptr, dlen, &p,
				 &len, &addr, SIP_EXPECT_CLASS_OTHER);
		if (ret != NF_ACCEPT)
			break;
		pr_debug("nf_conntrack_sip: alt port changed to %.*s\n",
			 len, p);
	}
	return ret;
}

static int process_artcp(struct sk_buff *skb, unsigned int protoff,
			 struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			 char **dptr, int *dlen)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int ret = NF_ACCEPT;
	char *p = *dptr;
	int len;
	struct nf_conntrack_man a;
	typeof(nf_nat_addr_hook) nf_nat_addr;

	/* We care only LAN->WAN situations */
	if (!memcmp(&ct->tuplehash[dir].tuple.src.u3,
		    &ct->tuplehash[!dir].tuple.dst.u3,
		    sizeof(ct->tuplehash[dir].tuple.src.u3)))
		goto end;

	while (find_field(&p, *dptr+*dlen, POS_ARTCP_IP4)) {
		if ((len = parse_addr(&p, *dptr+*dlen, &a)) == 0)
			continue;
		pr_debug("nf_conntrack_sip: rtcp ip=%.*s\n", len, p);
		
		/* LAN->WAN. Change the LAN IP to WAN. */
		if (!memcmp(&a.u3, &ct->tuplehash[dir].tuple.src.u3, sizeof(a.u3)) &&
		    (nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
			a.u3 = ct->tuplehash[!dir].tuple.dst.u3;
			ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr, dlen, &p,
					  &len, &a);
			pr_debug("nf_conntrack_sip: rtcp ip changed to %.*s\n",
				 len, p);
		}
		/* LAN->WAN, with firewall's external IP address that has been set by
		 * some 'smart' UAs. We need to change the parsed IP to LAN. */ 
		else if (!memcmp(&a.u3, &ct->tuplehash[!dir].tuple.dst.u3,
				 sizeof(a.u3))) {
			pr_debug("nf_conntrack_sip: rtcp ip is auto-detected WAN "
				 "address\n");
		}

		p += len;
	}

end:
	return ret;
}

static int update_content_length(struct sk_buff *skb, unsigned int protoff,
				 struct nf_conn *ct,
				 enum ip_conntrack_info ctinfo, char **dptr,
				 int *dlen)
{
	int ret = NF_ACCEPT;
	int len;
	int clen;
	int real_clen;
	char *p = *dptr;
	char *clen_start;
	typeof(nf_nat_addr_hook) nf_nat_addr;

	/* Look for Content-Length field */
	if (!find_field(&p, *dptr + *dlen, POS_CONTENT))
		return NF_ACCEPT;
	if ((len = parse_digits(&p, *dptr+*dlen, &clen)) == 0)
		return NF_DROP;
	pr_debug("nf_conntrack_sip: Content-Length=%d\n", clen);
	clen_start = p;

	/* Look for the end of header fields */
	while(p < *dptr+*dlen) {
		if (*p == '\r') {
			if (memcmp(p, "\r\n\r\n", 4) == 0) {
				p += 4;
				break;
			} else if (p[1] == '\r') {
				p += 2;
				break;
			}
		} else if (*p == '\n') {
			if (p[1] == '\n') {
				p += 2;
				break;
			}
		}
		p++;
	}

	/* Calulate real content length */
	if (p > *dptr+*dlen)
		return NF_DROP;
	real_clen = *dlen - (p - *dptr);
	pr_debug("nf_conntrack_sip: Real content length=%d\n", real_clen);
	if (real_clen == clen)
		return NF_ACCEPT;
	
	/* Modify content length */
	if ((nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
		struct nf_conntrack_man addr;

		memset(&addr, 0, sizeof(addr));
		addr.l3num = AF_INET;
	    	addr.u.all = htons((u_int16_t)real_clen);
	    	ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr, dlen,
				  &clen_start, &len, &addr);
		pr_debug("nf_conntrack_sip: Content-Length changed to %.*s\n",
		       	 len, clen_start);
	}

	return ret;
}

static int expect_sip(struct sk_buff *skb, unsigned int protoff,
		      struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		      char **dptr, int *dlen, char **addr_begin, int *addr_len,
		      struct nf_conntrack_man *addr)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	struct nf_conntrack_expect *exp;
	typeof(nf_nat_sip_hook) nf_nat_sip;
	int ret = NF_ACCEPT;

	if ((exp = nf_ct_expect_alloc(ct)) == NULL)
		return ret;
	nf_ct_expect_init(exp, NF_CT_EXPECT_CLASS_DEFAULT, addr->l3num, NULL,
			  &addr->u3, IPPROTO_UDP, NULL, &addr->u.udp.port);
	exp->helper = addr->l3num == AF_INET?  &sip[0][0] : &sip[0][1];
	exp->derived_timeout = 0;

	if (memcmp(&ct->tuplehash[dir].tuple.src.u3,
	    	   &ct->tuplehash[!dir].tuple.dst.u3,
		   sizeof(ct->tuplehash[dir].tuple.src.u3)) &&
	    (nf_nat_sip = rcu_dereference(nf_nat_sip_hook))) {
		ret = nf_nat_sip(skb, protoff, ct, ctinfo, dptr, dlen, exp,
				 addr_begin, addr_len);
	} else {
		if (nf_ct_expect_related(exp) != 0) {
			pr_debug("nf_conntrack_sip: nf_ct_expect_related() "
				 "failed\n");
		}
	}
	nf_ct_expect_put(exp);
	return ret;

}
static int process_via(struct sk_buff *skb, unsigned int protoff,
		       struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		       char **dptr, int *dlen)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int ret = NF_ACCEPT;
	char *p = *dptr;
	struct nf_conntrack_man addr;
	int len;
	typeof(nf_nat_addr_hook) nf_nat_addr;
#if 1 //pochao: merge from P2812 fxs to ip phone can't work
	bool isReq = false, isRes = false;

	if(memcmp(*dptr, "INVITE", sizeof("INVITE") - 1) == 0 ||
		memcmp(*dptr, "UPDATE", sizeof("UPDATE") - 1) == 0 ||
		memcmp(*dptr, "BYE", sizeof("BYE") - 1) == 0){
		isReq = true;
	}
	else if(memcmp(*dptr, "SIP/2.0 200", sizeof("SIP/2.0 200") - 1) == 0 ||
		memcmp(*dptr, "SIP/2.0 100", sizeof("SIP/2.0 100") - 1) == 0 ||
		memcmp(*dptr, "SIP/2.0 180", sizeof("SIP/2.0 180") - 1) == 0 ||
		memcmp(*dptr, "SIP/2.0 183", sizeof("SIP/2.0 183") - 1) == 0 ){
		isRes = true;
	}
#endif

	while (find_field(&p, *dptr + *dlen, POS_VIA)) {

		if ((len = parse_sip_uri(&p, *dptr + *dlen, &addr)) == 0)
			continue;
		pr_debug("nf_conntrack_sip: Via=%.*s\n", len, p);

		/* Different SIP port than this one */
		if (!memcmp(&addr.u3, &ct->tuplehash[dir].tuple.src.u3,
			    sizeof(addr.u3)) && addr.u.udp.port != htons(0) &&
		    addr.u.udp.port != ct->tuplehash[dir].tuple.src.u.udp.port){
		    	pr_debug("nf_conntrack_sip: different message port\n");
#if 1 //Autumn
			addr.u.udp.port = ct->tuplehash[!dir].tuple.dst.u.udp.port;
#endif
		    	ret = expect_sip(skb, protoff, ct, ctinfo, dptr, dlen,
					 &p, &len, &addr);
			break;
		}
		/* LAN->WAN. Change the LAN address to WAN address */ 
		else if (!memcmp(&addr.u3, &ct->tuplehash[dir].tuple.src.u3,
				 sizeof(addr.u3)) &&
#if 1 //Autumn
				(addr.u.all == htons(0) || addr.u.all == ct->tuplehash[dir].tuple.src.u.all )&&
#else
			 addr.u.all == ct->tuplehash[dir].tuple.src.u.all &&
#endif
			 memcmp(&ct->tuplehash[dir].tuple.src.u3,
			 	&ct->tuplehash[!dir].tuple.dst.u3,
				sizeof(ct->tuplehash[dir].tuple.dst.u3)) &&
			 (nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
#if 1 //pochao: merge from P2812 fxs to ip phone can't work
				if(isRes){
					return NF_ACCEPT;
				}
#endif
			addr.u3 = ct->tuplehash[!dir].tuple.dst.u3;
			addr.u.all = ct->tuplehash[!dir].tuple.dst.u.all;
			ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr,
					  dlen, &p, &len, &addr);
		    	pr_debug("nf_conntrack_sip: LAN address in Via "
			       	 "changed to WAN address %.*s\n", len, p);
			break;
		}
		/* LAN->WAN, with firewall's external IP address that has been
		 * set by some 'smart' UAs. We need to change the port. */ 
		else if (!memcmp(&addr.u3, &ct->tuplehash[!dir].tuple.dst.u3,
				 sizeof(addr.u3)) &&
			 memcmp(&ct->tuplehash[dir].tuple.src.u3,
			 	&ct->tuplehash[!dir].tuple.dst.u3,
				sizeof(ct->tuplehash[dir].tuple.dst.u3)) &&
			 (nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
#if 1 //pochao: merge from P2812 fxs to ip phone can't work
				if(isRes){
					return NF_ACCEPT;
				}
#endif
			addr.u3 = ct->tuplehash[!dir].tuple.dst.u3;
			addr.u.all = ct->tuplehash[!dir].tuple.dst.u.all;
			ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr,
					  dlen, &p, &len, &addr);
		    	pr_debug("nf_conntrack_sip: Auto-detected WAN address "
			       	 "in Via changed to %.*s\n", len, p);
			break;
		}
		/* WAN->LAN. Change the WAN address to LAN address */ 
		else if (!memcmp(&addr.u3, &ct->tuplehash[dir].tuple.dst.u3,
				 sizeof(addr.u3)) &&
			 addr.u.udp.port ==
			 ct->tuplehash[dir].tuple.dst.u.udp.port &&
			 memcmp(&ct->tuplehash[dir].tuple.dst.u3,
			 	&ct->tuplehash[!dir].tuple.src.u3,
				sizeof(ct->tuplehash[dir].tuple.dst.u3)) &&
			 (nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
#if 1 //pochao: merge from P2812 fxs to ip phone can't work
				if(isRes){
					return NF_ACCEPT;
				}
#endif
			addr = ct->tuplehash[!dir].tuple.src;
			ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr,
					  dlen, &p, &len, &addr);
		    	pr_debug("nf_conntrack_sip: WAN address in Via "
			       	 "changed to LAN address %.*s\n", len, p);
			break;
		}
		p += len;
	}
	return ret;
}

static int process_contact(struct sk_buff *skb, unsigned int protoff,
			   struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			   char **dptr, int *dlen)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int ret = NF_ACCEPT;
	char *p = *dptr;
	int len;
	struct nf_conntrack_man addr;
	typeof(nf_nat_addr_hook) nf_nat_addr;

	if (!find_field(&p, *dptr+*dlen, POS_CONTACT))
		return ret;
	if ((len = parse_sip_uri(&p, *dptr+*dlen, &addr)) == 0)
		return ret;  // brcm: that might be a contact with SIP URL, let him go.
	pr_debug("nf_conntrack_sip: Contact=%.*s\n", len, p);

	/* Different SIP port than this one */
	if (!memcmp(&addr.u3, &ct->tuplehash[dir].tuple.src.u3,
		    sizeof(addr.u3)) && addr.u.udp.port != htons(0) &&
	    addr.u.udp.port != ct->tuplehash[dir].tuple.src.u.udp.port) {
		pr_debug("nf_conntrack_sip: different message port\n");
	    	ret = expect_sip(skb, protoff, ct, ctinfo, dptr, dlen, &p,
				 &len, &addr);
	}
	/* LAN->WAN. Change the LAN address to WAN address */ 
	else if (!memcmp(&addr.u3, &ct->tuplehash[dir].tuple.src.u3,
			 sizeof(addr.u3)) &&
		 addr.u.all == ct->tuplehash[dir].tuple.src.u.all &&
		 memcmp(&ct->tuplehash[dir].tuple.src.u3,
		 	&ct->tuplehash[!dir].tuple.dst.u3,
			sizeof(ct->tuplehash[dir].tuple.dst.u3)) &&
		 (nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
		addr.u3 = ct->tuplehash[!dir].tuple.dst.u3;
		addr.u.all = ct->tuplehash[!dir].tuple.dst.u.all;
		ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr, dlen, &p,
				  &len, &addr);
		pr_debug("nf_conntrack_sip: LAN address in Contact "
		       	 "changed to WAN address %.*s\n", len, p);
	}
	/* LAN->WAN, with firewall's external IP address that has been
	 * set by some 'smart' UAs. We need to change the port. */ 
	else if (!memcmp(&addr.u3, &ct->tuplehash[!dir].tuple.dst.u3,
			 sizeof(addr.u3)) &&
		 memcmp(&ct->tuplehash[dir].tuple.src.u3,
		 	&ct->tuplehash[!dir].tuple.dst.u3,
			sizeof(ct->tuplehash[dir].tuple.dst.u3)) &&
		 (nf_nat_addr = rcu_dereference(nf_nat_addr_hook))) {
		addr.u3 = ct->tuplehash[!dir].tuple.dst.u3;
		addr.u.all = ct->tuplehash[!dir].tuple.dst.u.all;
		ret = nf_nat_addr(skb, protoff, ct, ctinfo, dptr, dlen, &p,
				  &len, &addr);
		pr_debug("nf_conntrack_sip: Auto-detected WAN address in "
		       	 "Contact changed to %.*s\n", len, p);
	}
	return ret;
}

static int process_bye(struct sk_buff *skb, struct nf_conn *ct)
{

	/* Disconnect all child connections that have infinite timeout */
	pr_debug("iterate each derived connections");

	if (!list_empty(&ct->derived_connections)) {
		struct nf_conn *child, *tmp;
		pr_debug("derived connection list is not empty"); 
		list_for_each_entry_safe(child, tmp, &ct->derived_connections,
                    derived_list) {
			struct nf_conn_help * help;
			help = nfct_help(child);
			if (!help) {
				child->derived_timeout = 5 * HZ;
				nf_ct_refresh(child, skb, 5 * HZ);
			}
		}
	}

	return NF_ACCEPT;
}

static int sip_help(struct sk_buff *skb,
		    unsigned int protoff,
		    struct nf_conn *ct,
		    enum ip_conntrack_info ctinfo)
{
	unsigned int dataoff, datalen;
	char *dptr;
	int ret = NF_ACCEPT;
	struct nf_conntrack_man addr;

	/* Owned by local application (FXS), just accept it */
	if (skb->sk)
		return NF_ACCEPT;
	
	/* No Data ? */
	dataoff = protoff + sizeof(struct udphdr);
	if (dataoff >= skb->len)
		return NF_ACCEPT;

	if (ct->derived_timeout == 0)
		nf_ct_refresh(ct, skb, sip_timeout * HZ);

	if (!skb_is_nonlinear(skb))
		dptr = skb->data + dataoff;
	else {
		pr_debug("Copy of skbuff not supported yet.\n");
		goto out;
	}
	pr_debug("nf_conntrack_sip: received message \"%.14s\"\n", dptr);

	datalen = skb->len - dataoff;
	if (datalen < sizeof("SIP/2.0 200") - 1)
		goto out;

	/* Process Via field */
	pr_debug("nf_conntrack_sip: process_via\n");
	ret = process_via(skb, protoff, ct, ctinfo, &dptr, &datalen);
	if (ret != NF_ACCEPT)
		goto out;

	/* Process Contact field */
	pr_debug("nf_conntrack_sip: process_contact\n");
	ret = process_contact(skb, protoff, ct, ctinfo, &dptr, &datalen);
	if (ret != NF_ACCEPT)
		goto out;
	
	/* Process BYE and status code 400 (disconnect) */
	if (memcmp(dptr, "BYE", sizeof("BYE") - 1) == 0 ||
	    memcmp(dptr, "SIP/2.0 400", sizeof("SIP/2.0 400") - 1) == 0) {
		pr_debug("nf_conntrack_sip: process_bye\n");
		ret = process_bye(skb, ct);
		goto out;
	}

	/* RTP info only in some SDP pkts */
	if (memcmp(dptr, "INVITE", sizeof("INVITE") - 1) == 0 ||
	    memcmp(dptr, "UPDATE", sizeof("UPDATE") - 1) == 0 ||
	    memcmp(dptr, "SIP/2.0 180", sizeof("SIP/2.0 180") - 1) == 0 ||
	    memcmp(dptr, "SIP/2.0 183", sizeof("SIP/2.0 183") - 1) == 0 ||
	    memcmp(dptr, "SIP/2.0 200", sizeof("SIP/2.0 200") - 1) == 0) {
		pr_debug("nf_conntrack_sip: process_owner\n");
		ret = process_owner(skb, protoff, ct, ctinfo, &dptr,
				    &datalen, &addr);
		if (ret != NF_ACCEPT)
			goto out;
		ret = process_connection(skb, protoff, ct, ctinfo, &dptr,
					 &datalen, &addr);
		pr_debug("nf_conntrack_sip: process_connection\n");
		if (ret != NF_ACCEPT)
			goto out;
		pr_debug("nf_conntrack_sip: process_audio\n");
		ret = process_audio(skb, protoff, ct, ctinfo, &dptr,
				    &datalen, &addr);
		if (ret != NF_ACCEPT)
			goto out;
		pr_debug("nf_conntrack_sip: process_video\n");
		ret = process_video(skb, protoff, ct, ctinfo, &dptr,
				    &datalen, &addr);
		if (ret != NF_ACCEPT)
			goto out;
		pr_debug("nf_conntrack_sip: process_anat\n");
		ret = process_anat(skb, protoff, ct, ctinfo, &dptr, &datalen);
		if (ret != NF_ACCEPT)
			goto out;
		pr_debug("nf_conntrack_sip: process_artcp\n");
		ret = process_artcp(skb, protoff, ct, ctinfo, &dptr, &datalen);
		if (ret != NF_ACCEPT)
			goto out;
		pr_debug("nf_conntrack_sip: update_content_length\n");
		ret = update_content_length(skb, protoff, ct, ctinfo, &dptr,
					    &datalen);
	}

out:
	pr_debug("nf_conntrack_sip: %s\n", ret == NF_ACCEPT?
		 "accepted" : "dropped");
	return ret;
}

static void nf_conntrack_sip_fini(void)
{
	int i, j;

	for (i = 0; i < ports_c; i++) {
		for (j = 0; j < 2; j++) {
			if (sip[i][j].me == NULL)
				continue;

#if defined(CONFIG_BCM_KF_NETFILTER)
        /* unregister the SIP ports with ingress QoS classifier */
        iqos_rem_L4port( sip[i][j].tuple.dst.protonum, 
			              sip[i][j].tuple.src.u.udp.port, IQOS_ENT_STAT );
#endif
			nf_conntrack_helper_unregister(&sip[i][j]);
		}
	}
}

static int __init nf_conntrack_sip_init(void)
{
	int i, j, ret;
	char *tmpname;

	printk("Broadcom conntrack_sip init\n");

	if (ports_c == 0)
		ports[ports_c++] = SIP_PORT;

	for (i = 0; i < ports_c; i++) {
		memset(&sip[i], 0, sizeof(sip[i]));

		sip[i][0].tuple.src.l3num = AF_INET;
		sip[i][1].tuple.src.l3num = AF_INET6;
		for (j = 0; j < 2; j++) {
			sip[i][j].tuple.dst.protonum = IPPROTO_UDP;
			sip[i][j].tuple.src.u.udp.port = htons(ports[i]);
			sip[i][j].me = THIS_MODULE;
			sip[i][j].help = sip_help;
			sip[i][j].expect_policy	= &sip_exp_policy[0],
			sip[i][j].expect_class_max = SIP_EXPECT_CLASS_MAX;

			tmpname = &sip_names[i][j][0];
			if (ports[i] == SIP_PORT)
				sprintf(tmpname, "sip");
			else
				sprintf(tmpname, "sip-%u", i);
			sip[i][j].name = tmpname;

			pr_debug("port #%u: %u\n", i, ports[i]);

			ret = nf_conntrack_helper_register(&sip[i][j]);
			if (ret) {
				printk("nf_ct_sip: failed to register helper "
				       "for pf: %u port: %u\n",
				       sip[i][j].tuple.src.l3num, ports[i]);
				nf_conntrack_sip_fini();
				return ret;
			}
		}
        
        /* register the SIP ports with ingress QoS classifier */
        iqos_add_L4port( IPPROTO_UDP, ports[i], IQOS_ENT_STAT, IQOS_PRIO_HIGH );
	}
	pr_debug("nf_conntrack_sip registered\n");
	return 0;
}

module_init(nf_conntrack_sip_init);
module_exit(nf_conntrack_sip_fini);
#else /* CONFIG_BCM_KF_NETFILTER */
static int sip_direct_signalling __read_mostly = 1;
module_param(sip_direct_signalling, int, 0600);
MODULE_PARM_DESC(sip_direct_signalling, "expect incoming calls from registrar "
					"only (default 1)");

static int sip_direct_media __read_mostly = 1;
module_param(sip_direct_media, int, 0600);
MODULE_PARM_DESC(sip_direct_media, "Expect Media streams between signalling "
				   "endpoints only (default 1)");

unsigned int (*nf_nat_sip_hook)(struct sk_buff *skb, unsigned int dataoff,
				const char **dptr,
				unsigned int *datalen) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sip_hook);

void (*nf_nat_sip_seq_adjust_hook)(struct sk_buff *skb, s16 off) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sip_seq_adjust_hook);

unsigned int (*nf_nat_sip_expect_hook)(struct sk_buff *skb,
				       unsigned int dataoff,
				       const char **dptr,
				       unsigned int *datalen,
				       struct nf_conntrack_expect *exp,
				       unsigned int matchoff,
				       unsigned int matchlen) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sip_expect_hook);

unsigned int (*nf_nat_sdp_addr_hook)(struct sk_buff *skb, unsigned int dataoff,
				     const char **dptr,
				     unsigned int *datalen,
				     unsigned int sdpoff,
				     enum sdp_header_types type,
				     enum sdp_header_types term,
				     const union nf_inet_addr *addr)
				     __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sdp_addr_hook);

unsigned int (*nf_nat_sdp_port_hook)(struct sk_buff *skb, unsigned int dataoff,
				     const char **dptr,
				     unsigned int *datalen,
				     unsigned int matchoff,
				     unsigned int matchlen,
				     u_int16_t port) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sdp_port_hook);

unsigned int (*nf_nat_sdp_session_hook)(struct sk_buff *skb,
					unsigned int dataoff,
					const char **dptr,
					unsigned int *datalen,
					unsigned int sdpoff,
					const union nf_inet_addr *addr)
					__read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sdp_session_hook);

unsigned int (*nf_nat_sdp_media_hook)(struct sk_buff *skb, unsigned int dataoff,
				      const char **dptr,
				      unsigned int *datalen,
				      struct nf_conntrack_expect *rtp_exp,
				      struct nf_conntrack_expect *rtcp_exp,
				      unsigned int mediaoff,
				      unsigned int medialen,
				      union nf_inet_addr *rtp_addr)
				      __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sdp_media_hook);

static int string_len(const struct nf_conn *ct, const char *dptr,
		      const char *limit, int *shift)
{
	int len = 0;

	while (dptr < limit && isalpha(*dptr)) {
		dptr++;
		len++;
	}
	return len;
}

static int digits_len(const struct nf_conn *ct, const char *dptr,
		      const char *limit, int *shift)
{
	int len = 0;
	while (dptr < limit && isdigit(*dptr)) {
		dptr++;
		len++;
	}
	return len;
}

static int iswordc(const char c)
{
	if (isalnum(c) || c == '!' || c == '"' || c == '%' ||
	    (c >= '(' && c <= '/') || c == ':' || c == '<' || c == '>' ||
	    c == '?' || (c >= '[' && c <= ']') || c == '_' || c == '`' ||
	    c == '{' || c == '}' || c == '~')
		return 1;
	return 0;
}

static int word_len(const char *dptr, const char *limit)
{
	int len = 0;
	while (dptr < limit && iswordc(*dptr)) {
		dptr++;
		len++;
	}
	return len;
}

static int callid_len(const struct nf_conn *ct, const char *dptr,
		      const char *limit, int *shift)
{
	int len, domain_len;

	len = word_len(dptr, limit);
	dptr += len;
	if (!len || dptr == limit || *dptr != '@')
		return len;
	dptr++;
	len++;

	domain_len = word_len(dptr, limit);
	if (!domain_len)
		return 0;
	return len + domain_len;
}

/* get media type + port length */
static int media_len(const struct nf_conn *ct, const char *dptr,
		     const char *limit, int *shift)
{
	int len = string_len(ct, dptr, limit, shift);

	dptr += len;
	if (dptr >= limit || *dptr != ' ')
		return 0;
	len++;
	dptr++;

	return len + digits_len(ct, dptr, limit, shift);
}

static int parse_addr(const struct nf_conn *ct, const char *cp,
                      const char **endp, union nf_inet_addr *addr,
                      const char *limit)
{
	const char *end;
	int ret = 0;

	if (!ct)
		return 0;

	memset(addr, 0, sizeof(*addr));
	switch (nf_ct_l3num(ct)) {
	case AF_INET:
		ret = in4_pton(cp, limit - cp, (u8 *)&addr->ip, -1, &end);
		break;
	case AF_INET6:
		ret = in6_pton(cp, limit - cp, (u8 *)&addr->ip6, -1, &end);
		break;
	default:
		BUG();
	}

	if (ret == 0 || end == cp)
		return 0;
	if (endp)
		*endp = end;
	return 1;
}

/* skip ip address. returns its length. */
static int epaddr_len(const struct nf_conn *ct, const char *dptr,
		      const char *limit, int *shift)
{
	union nf_inet_addr addr;
	const char *aux = dptr;

	if (!parse_addr(ct, dptr, &dptr, &addr, limit)) {
		pr_debug("ip: %s parse failed.!\n", dptr);
		return 0;
	}

	/* Port number */
	if (*dptr == ':') {
		dptr++;
		dptr += digits_len(ct, dptr, limit, shift);
	}
	return dptr - aux;
}

/* get address length, skiping user info. */
static int skp_epaddr_len(const struct nf_conn *ct, const char *dptr,
			  const char *limit, int *shift)
{
	const char *start = dptr;
	int s = *shift;

	/* Search for @, but stop at the end of the line.
	 * We are inside a sip: URI, so we don't need to worry about
	 * continuation lines. */
	while (dptr < limit &&
	       *dptr != '@' && *dptr != '\r' && *dptr != '\n') {
		(*shift)++;
		dptr++;
	}

	if (dptr < limit && *dptr == '@') {
		dptr++;
		(*shift)++;
	} else {
		dptr = start;
		*shift = s;
	}

	return epaddr_len(ct, dptr, limit, shift);
}

/* Parse a SIP request line of the form:
 *
 * Request-Line = Method SP Request-URI SP SIP-Version CRLF
 *
 * and return the offset and length of the address contained in the Request-URI.
 */
int ct_sip_parse_request(const struct nf_conn *ct,
			 const char *dptr, unsigned int datalen,
			 unsigned int *matchoff, unsigned int *matchlen,
			 union nf_inet_addr *addr, __be16 *port)
{
	const char *start = dptr, *limit = dptr + datalen, *end;
	unsigned int mlen;
	unsigned int p;
	int shift = 0;

	/* Skip method and following whitespace */
	mlen = string_len(ct, dptr, limit, NULL);
	if (!mlen)
		return 0;
	dptr += mlen;
	if (++dptr >= limit)
		return 0;

	/* Find SIP URI */
	for (; dptr < limit - strlen("sip:"); dptr++) {
		if (*dptr == '\r' || *dptr == '\n')
			return -1;
		if (strnicmp(dptr, "sip:", strlen("sip:")) == 0) {
			dptr += strlen("sip:");
			break;
		}
	}
	if (!skp_epaddr_len(ct, dptr, limit, &shift))
		return 0;
	dptr += shift;

	if (!parse_addr(ct, dptr, &end, addr, limit))
		return -1;
	if (end < limit && *end == ':') {
		end++;
		p = simple_strtoul(end, (char **)&end, 10);
		if (p < 1024 || p > 65535)
			return -1;
		*port = htons(p);
	} else
		*port = htons(SIP_PORT);

	if (end == dptr)
		return 0;
	*matchoff = dptr - start;
	*matchlen = end - dptr;
	return 1;
}
EXPORT_SYMBOL_GPL(ct_sip_parse_request);

/* SIP header parsing: SIP headers are located at the beginning of a line, but
 * may span several lines, in which case the continuation lines begin with a
 * whitespace character. RFC 2543 allows lines to be terminated with CR, LF or
 * CRLF, RFC 3261 allows only CRLF, we support both.
 *
 * Headers are followed by (optionally) whitespace, a colon, again (optionally)
 * whitespace and the values. Whitespace in this context means any amount of
 * tabs, spaces and continuation lines, which are treated as a single whitespace
 * character.
 *
 * Some headers may appear multiple times. A comma separated list of values is
 * equivalent to multiple headers.
 */
static const struct sip_header ct_sip_hdrs[] = {
	[SIP_HDR_CSEQ]			= SIP_HDR("CSeq", NULL, NULL, digits_len),
	[SIP_HDR_FROM]			= SIP_HDR("From", "f", "sip:", skp_epaddr_len),
	[SIP_HDR_TO]			= SIP_HDR("To", "t", "sip:", skp_epaddr_len),
	[SIP_HDR_CONTACT]		= SIP_HDR("Contact", "m", "sip:", skp_epaddr_len),
	[SIP_HDR_VIA_UDP]		= SIP_HDR("Via", "v", "UDP ", epaddr_len),
	[SIP_HDR_VIA_TCP]		= SIP_HDR("Via", "v", "TCP ", epaddr_len),
	[SIP_HDR_EXPIRES]		= SIP_HDR("Expires", NULL, NULL, digits_len),
	[SIP_HDR_CONTENT_LENGTH]	= SIP_HDR("Content-Length", "l", NULL, digits_len),
	[SIP_HDR_CALL_ID]		= SIP_HDR("Call-Id", "i", NULL, callid_len),
};

static const char *sip_follow_continuation(const char *dptr, const char *limit)
{
	/* Walk past newline */
	if (++dptr >= limit)
		return NULL;

	/* Skip '\n' in CR LF */
	if (*(dptr - 1) == '\r' && *dptr == '\n') {
		if (++dptr >= limit)
			return NULL;
	}

	/* Continuation line? */
	if (*dptr != ' ' && *dptr != '\t')
		return NULL;

	/* skip leading whitespace */
	for (; dptr < limit; dptr++) {
		if (*dptr != ' ' && *dptr != '\t')
			break;
	}
	return dptr;
}

static const char *sip_skip_whitespace(const char *dptr, const char *limit)
{
	for (; dptr < limit; dptr++) {
		if (*dptr == ' ')
			continue;
		if (*dptr != '\r' && *dptr != '\n')
			break;
		dptr = sip_follow_continuation(dptr, limit);
		if (dptr == NULL)
			return NULL;
	}
	return dptr;
}

/* Search within a SIP header value, dealing with continuation lines */
static const char *ct_sip_header_search(const char *dptr, const char *limit,
					const char *needle, unsigned int len)
{
	for (limit -= len; dptr < limit; dptr++) {
		if (*dptr == '\r' || *dptr == '\n') {
			dptr = sip_follow_continuation(dptr, limit);
			if (dptr == NULL)
				break;
			continue;
		}

		if (strnicmp(dptr, needle, len) == 0)
			return dptr;
	}
	return NULL;
}

int ct_sip_get_header(const struct nf_conn *ct, const char *dptr,
		      unsigned int dataoff, unsigned int datalen,
		      enum sip_header_types type,
		      unsigned int *matchoff, unsigned int *matchlen)
{
	const struct sip_header *hdr = &ct_sip_hdrs[type];
	const char *start = dptr, *limit = dptr + datalen;
	int shift = 0;

	for (dptr += dataoff; dptr < limit; dptr++) {
		/* Find beginning of line */
		if (*dptr != '\r' && *dptr != '\n')
			continue;
		if (++dptr >= limit)
			break;
		if (*(dptr - 1) == '\r' && *dptr == '\n') {
			if (++dptr >= limit)
				break;
		}

		/* Skip continuation lines */
		if (*dptr == ' ' || *dptr == '\t')
			continue;

		/* Find header. Compact headers must be followed by a
		 * non-alphabetic character to avoid mismatches. */
		if (limit - dptr >= hdr->len &&
		    strnicmp(dptr, hdr->name, hdr->len) == 0)
			dptr += hdr->len;
		else if (hdr->cname && limit - dptr >= hdr->clen + 1 &&
			 strnicmp(dptr, hdr->cname, hdr->clen) == 0 &&
			 !isalpha(*(dptr + hdr->clen)))
			dptr += hdr->clen;
		else
			continue;

		/* Find and skip colon */
		dptr = sip_skip_whitespace(dptr, limit);
		if (dptr == NULL)
			break;
		if (*dptr != ':' || ++dptr >= limit)
			break;

		/* Skip whitespace after colon */
		dptr = sip_skip_whitespace(dptr, limit);
		if (dptr == NULL)
			break;

		*matchoff = dptr - start;
		if (hdr->search) {
			dptr = ct_sip_header_search(dptr, limit, hdr->search,
						    hdr->slen);
			if (!dptr)
				return -1;
			dptr += hdr->slen;
		}

		*matchlen = hdr->match_len(ct, dptr, limit, &shift);
		if (!*matchlen)
			return -1;
		*matchoff = dptr - start + shift;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ct_sip_get_header);

/* Get next header field in a list of comma separated values */
static int ct_sip_next_header(const struct nf_conn *ct, const char *dptr,
			      unsigned int dataoff, unsigned int datalen,
			      enum sip_header_types type,
			      unsigned int *matchoff, unsigned int *matchlen)
{
	const struct sip_header *hdr = &ct_sip_hdrs[type];
	const char *start = dptr, *limit = dptr + datalen;
	int shift = 0;

	dptr += dataoff;

	dptr = ct_sip_header_search(dptr, limit, ",", strlen(","));
	if (!dptr)
		return 0;

	dptr = ct_sip_header_search(dptr, limit, hdr->search, hdr->slen);
	if (!dptr)
		return 0;
	dptr += hdr->slen;

	*matchoff = dptr - start;
	*matchlen = hdr->match_len(ct, dptr, limit, &shift);
	if (!*matchlen)
		return -1;
	*matchoff += shift;
	return 1;
}

/* Walk through headers until a parsable one is found or no header of the
 * given type is left. */
static int ct_sip_walk_headers(const struct nf_conn *ct, const char *dptr,
			       unsigned int dataoff, unsigned int datalen,
			       enum sip_header_types type, int *in_header,
			       unsigned int *matchoff, unsigned int *matchlen)
{
	int ret;

	if (in_header && *in_header) {
		while (1) {
			ret = ct_sip_next_header(ct, dptr, dataoff, datalen,
						 type, matchoff, matchlen);
			if (ret > 0)
				return ret;
			if (ret == 0)
				break;
			dataoff += *matchoff;
		}
		*in_header = 0;
	}

	while (1) {
		ret = ct_sip_get_header(ct, dptr, dataoff, datalen,
					type, matchoff, matchlen);
		if (ret > 0)
			break;
		if (ret == 0)
			return ret;
		dataoff += *matchoff;
	}

	if (in_header)
		*in_header = 1;
	return 1;
}

/* Locate a SIP header, parse the URI and return the offset and length of
 * the address as well as the address and port themselves. A stream of
 * headers can be parsed by handing in a non-NULL datalen and in_header
 * pointer.
 */
int ct_sip_parse_header_uri(const struct nf_conn *ct, const char *dptr,
			    unsigned int *dataoff, unsigned int datalen,
			    enum sip_header_types type, int *in_header,
			    unsigned int *matchoff, unsigned int *matchlen,
			    union nf_inet_addr *addr, __be16 *port)
{
	const char *c, *limit = dptr + datalen;
	unsigned int p;
	int ret;

	ret = ct_sip_walk_headers(ct, dptr, dataoff ? *dataoff : 0, datalen,
				  type, in_header, matchoff, matchlen);
	WARN_ON(ret < 0);
	if (ret == 0)
		return ret;

	if (!parse_addr(ct, dptr + *matchoff, &c, addr, limit))
		return -1;
	if (*c == ':') {
		c++;
		p = simple_strtoul(c, (char **)&c, 10);
		if (p < 1024 || p > 65535)
			return -1;
		*port = htons(p);
	} else
		*port = htons(SIP_PORT);

	if (dataoff)
		*dataoff = c - dptr;
	return 1;
}
EXPORT_SYMBOL_GPL(ct_sip_parse_header_uri);

static int ct_sip_parse_param(const struct nf_conn *ct, const char *dptr,
			      unsigned int dataoff, unsigned int datalen,
			      const char *name,
			      unsigned int *matchoff, unsigned int *matchlen)
{
	const char *limit = dptr + datalen;
	const char *start;
	const char *end;

	limit = ct_sip_header_search(dptr + dataoff, limit, ",", strlen(","));
	if (!limit)
		limit = dptr + datalen;

	start = ct_sip_header_search(dptr + dataoff, limit, name, strlen(name));
	if (!start)
		return 0;
	start += strlen(name);

	end = ct_sip_header_search(start, limit, ";", strlen(";"));
	if (!end)
		end = limit;

	*matchoff = start - dptr;
	*matchlen = end - start;
	return 1;
}

/* Parse address from header parameter and return address, offset and length */
int ct_sip_parse_address_param(const struct nf_conn *ct, const char *dptr,
			       unsigned int dataoff, unsigned int datalen,
			       const char *name,
			       unsigned int *matchoff, unsigned int *matchlen,
			       union nf_inet_addr *addr)
{
	const char *limit = dptr + datalen;
	const char *start, *end;

	limit = ct_sip_header_search(dptr + dataoff, limit, ",", strlen(","));
	if (!limit)
		limit = dptr + datalen;

	start = ct_sip_header_search(dptr + dataoff, limit, name, strlen(name));
	if (!start)
		return 0;

	start += strlen(name);
	if (!parse_addr(ct, start, &end, addr, limit))
		return 0;
	*matchoff = start - dptr;
	*matchlen = end - start;
	return 1;
}
EXPORT_SYMBOL_GPL(ct_sip_parse_address_param);

/* Parse numerical header parameter and return value, offset and length */
int ct_sip_parse_numerical_param(const struct nf_conn *ct, const char *dptr,
				 unsigned int dataoff, unsigned int datalen,
				 const char *name,
				 unsigned int *matchoff, unsigned int *matchlen,
				 unsigned int *val)
{
	const char *limit = dptr + datalen;
	const char *start;
	char *end;

	limit = ct_sip_header_search(dptr + dataoff, limit, ",", strlen(","));
	if (!limit)
		limit = dptr + datalen;

	start = ct_sip_header_search(dptr + dataoff, limit, name, strlen(name));
	if (!start)
		return 0;

	start += strlen(name);
	*val = simple_strtoul(start, &end, 0);
	if (start == end)
		return 0;
	if (matchoff && matchlen) {
		*matchoff = start - dptr;
		*matchlen = end - start;
	}
	return 1;
}
EXPORT_SYMBOL_GPL(ct_sip_parse_numerical_param);

static int ct_sip_parse_transport(struct nf_conn *ct, const char *dptr,
				  unsigned int dataoff, unsigned int datalen,
				  u8 *proto)
{
	unsigned int matchoff, matchlen;

	if (ct_sip_parse_param(ct, dptr, dataoff, datalen, "transport=",
			       &matchoff, &matchlen)) {
		if (!strnicmp(dptr + matchoff, "TCP", strlen("TCP")))
			*proto = IPPROTO_TCP;
		else if (!strnicmp(dptr + matchoff, "UDP", strlen("UDP")))
			*proto = IPPROTO_UDP;
		else
			return 0;

		if (*proto != nf_ct_protonum(ct))
			return 0;
	} else
		*proto = nf_ct_protonum(ct);

	return 1;
}

/* SDP header parsing: a SDP session description contains an ordered set of
 * headers, starting with a section containing general session parameters,
 * optionally followed by multiple media descriptions.
 *
 * SDP headers always start at the beginning of a line. According to RFC 2327:
 * "The sequence CRLF (0x0d0a) is used to end a record, although parsers should
 * be tolerant and also accept records terminated with a single newline
 * character". We handle both cases.
 */
static const struct sip_header ct_sdp_hdrs[] = {
	[SDP_HDR_VERSION]		= SDP_HDR("v=", NULL, digits_len),
	[SDP_HDR_OWNER_IP4]		= SDP_HDR("o=", "IN IP4 ", epaddr_len),
	[SDP_HDR_CONNECTION_IP4]	= SDP_HDR("c=", "IN IP4 ", epaddr_len),
	[SDP_HDR_OWNER_IP6]		= SDP_HDR("o=", "IN IP6 ", epaddr_len),
	[SDP_HDR_CONNECTION_IP6]	= SDP_HDR("c=", "IN IP6 ", epaddr_len),
	[SDP_HDR_MEDIA]			= SDP_HDR("m=", NULL, media_len),
};

/* Linear string search within SDP header values */
static const char *ct_sdp_header_search(const char *dptr, const char *limit,
					const char *needle, unsigned int len)
{
	for (limit -= len; dptr < limit; dptr++) {
		if (*dptr == '\r' || *dptr == '\n')
			break;
		if (strncmp(dptr, needle, len) == 0)
			return dptr;
	}
	return NULL;
}

/* Locate a SDP header (optionally a substring within the header value),
 * optionally stopping at the first occurrence of the term header, parse
 * it and return the offset and length of the data we're interested in.
 */
int ct_sip_get_sdp_header(const struct nf_conn *ct, const char *dptr,
			  unsigned int dataoff, unsigned int datalen,
			  enum sdp_header_types type,
			  enum sdp_header_types term,
			  unsigned int *matchoff, unsigned int *matchlen)
{
	const struct sip_header *hdr = &ct_sdp_hdrs[type];
	const struct sip_header *thdr = &ct_sdp_hdrs[term];
	const char *start = dptr, *limit = dptr + datalen;
	int shift = 0;

	for (dptr += dataoff; dptr < limit; dptr++) {
		/* Find beginning of line */
		if (*dptr != '\r' && *dptr != '\n')
			continue;
		if (++dptr >= limit)
			break;
		if (*(dptr - 1) == '\r' && *dptr == '\n') {
			if (++dptr >= limit)
				break;
		}

		if (term != SDP_HDR_UNSPEC &&
		    limit - dptr >= thdr->len &&
		    strnicmp(dptr, thdr->name, thdr->len) == 0)
			break;
		else if (limit - dptr >= hdr->len &&
			 strnicmp(dptr, hdr->name, hdr->len) == 0)
			dptr += hdr->len;
		else
			continue;

		*matchoff = dptr - start;
		if (hdr->search) {
			dptr = ct_sdp_header_search(dptr, limit, hdr->search,
						    hdr->slen);
			if (!dptr)
				return -1;
			dptr += hdr->slen;
		}

		*matchlen = hdr->match_len(ct, dptr, limit, &shift);
		if (!*matchlen)
			return -1;
		*matchoff = dptr - start + shift;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ct_sip_get_sdp_header);

static int ct_sip_parse_sdp_addr(const struct nf_conn *ct, const char *dptr,
				 unsigned int dataoff, unsigned int datalen,
				 enum sdp_header_types type,
				 enum sdp_header_types term,
				 unsigned int *matchoff, unsigned int *matchlen,
				 union nf_inet_addr *addr)
{
	int ret;

	ret = ct_sip_get_sdp_header(ct, dptr, dataoff, datalen, type, term,
				    matchoff, matchlen);
	if (ret <= 0)
		return ret;

	if (!parse_addr(ct, dptr + *matchoff, NULL, addr,
			dptr + *matchoff + *matchlen))
		return -1;
	return 1;
}

static int refresh_signalling_expectation(struct nf_conn *ct,
					  union nf_inet_addr *addr,
					  u8 proto, __be16 port,
					  unsigned int expires)
{
	struct nf_conn_help *help = nfct_help(ct);
	struct nf_conntrack_expect *exp;
	struct hlist_node *n, *next;
	int found = 0;

	spin_lock_bh(&nf_conntrack_lock);
	hlist_for_each_entry_safe(exp, n, next, &help->expectations, lnode) {
		if (exp->class != SIP_EXPECT_SIGNALLING ||
		    !nf_inet_addr_cmp(&exp->tuple.dst.u3, addr) ||
		    exp->tuple.dst.protonum != proto ||
		    exp->tuple.dst.u.udp.port != port)
			continue;
		if (!del_timer(&exp->timeout))
			continue;
		exp->flags &= ~NF_CT_EXPECT_INACTIVE;
		exp->timeout.expires = jiffies + expires * HZ;
		add_timer(&exp->timeout);
		found = 1;
		break;
	}
	spin_unlock_bh(&nf_conntrack_lock);
	return found;
}

static void flush_expectations(struct nf_conn *ct, bool media)
{
	struct nf_conn_help *help = nfct_help(ct);
	struct nf_conntrack_expect *exp;
	struct hlist_node *n, *next;

	spin_lock_bh(&nf_conntrack_lock);
	hlist_for_each_entry_safe(exp, n, next, &help->expectations, lnode) {
		if ((exp->class != SIP_EXPECT_SIGNALLING) ^ media)
			continue;
		if (!del_timer(&exp->timeout))
			continue;
		nf_ct_unlink_expect(exp);
		nf_ct_expect_put(exp);
		if (!media)
			break;
	}
	spin_unlock_bh(&nf_conntrack_lock);
}

static int set_expected_rtp_rtcp(struct sk_buff *skb, unsigned int dataoff,
				 const char **dptr, unsigned int *datalen,
				 union nf_inet_addr *daddr, __be16 port,
				 enum sip_expectation_classes class,
				 unsigned int mediaoff, unsigned int medialen)
{
	struct nf_conntrack_expect *exp, *rtp_exp, *rtcp_exp;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct net *net = nf_ct_net(ct);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	union nf_inet_addr *saddr;
	struct nf_conntrack_tuple tuple;
	int direct_rtp = 0, skip_expect = 0, ret = NF_DROP;
	u_int16_t base_port;
	__be16 rtp_port, rtcp_port;
	typeof(nf_nat_sdp_port_hook) nf_nat_sdp_port;
	typeof(nf_nat_sdp_media_hook) nf_nat_sdp_media;

	saddr = NULL;
	if (sip_direct_media) {
		if (!nf_inet_addr_cmp(daddr, &ct->tuplehash[dir].tuple.src.u3))
			return NF_ACCEPT;
		saddr = &ct->tuplehash[!dir].tuple.src.u3;
	}

	/* We need to check whether the registration exists before attempting
	 * to register it since we can see the same media description multiple
	 * times on different connections in case multiple endpoints receive
	 * the same call.
	 *
	 * RTP optimization: if we find a matching media channel expectation
	 * and both the expectation and this connection are SNATed, we assume
	 * both sides can reach each other directly and use the final
	 * destination address from the expectation. We still need to keep
	 * the NATed expectations for media that might arrive from the
	 * outside, and additionally need to expect the direct RTP stream
	 * in case it passes through us even without NAT.
	 */
	memset(&tuple, 0, sizeof(tuple));
	if (saddr)
		tuple.src.u3 = *saddr;
	tuple.src.l3num		= nf_ct_l3num(ct);
	tuple.dst.protonum	= IPPROTO_UDP;
	tuple.dst.u3		= *daddr;
	tuple.dst.u.udp.port	= port;

	rcu_read_lock();
	do {
		exp = __nf_ct_expect_find(net, nf_ct_zone(ct), &tuple);

		if (!exp || exp->master == ct ||
		    nfct_help(exp->master)->helper != nfct_help(ct)->helper ||
		    exp->class != class)
			break;
#ifdef CONFIG_NF_NAT_NEEDED
		if (exp->tuple.src.l3num == AF_INET && !direct_rtp &&
		    (exp->saved_ip != exp->tuple.dst.u3.ip ||
		     exp->saved_proto.udp.port != exp->tuple.dst.u.udp.port) &&
		    ct->status & IPS_NAT_MASK) {
			daddr->ip		= exp->saved_ip;
			tuple.dst.u3.ip		= exp->saved_ip;
			tuple.dst.u.udp.port	= exp->saved_proto.udp.port;
			direct_rtp = 1;
		} else
#endif
			skip_expect = 1;
	} while (!skip_expect);
	rcu_read_unlock();

	base_port = ntohs(tuple.dst.u.udp.port) & ~1;
	rtp_port = htons(base_port);
	rtcp_port = htons(base_port + 1);

	if (direct_rtp) {
		nf_nat_sdp_port = rcu_dereference(nf_nat_sdp_port_hook);
		if (nf_nat_sdp_port &&
		    !nf_nat_sdp_port(skb, dataoff, dptr, datalen,
				     mediaoff, medialen, ntohs(rtp_port)))
			goto err1;
	}

	if (skip_expect)
		return NF_ACCEPT;

	rtp_exp = nf_ct_expect_alloc(ct);
	if (rtp_exp == NULL)
		goto err1;
	nf_ct_expect_init(rtp_exp, class, nf_ct_l3num(ct), saddr, daddr,
			  IPPROTO_UDP, NULL, &rtp_port);

	rtcp_exp = nf_ct_expect_alloc(ct);
	if (rtcp_exp == NULL)
		goto err2;
	nf_ct_expect_init(rtcp_exp, class, nf_ct_l3num(ct), saddr, daddr,
			  IPPROTO_UDP, NULL, &rtcp_port);

	nf_nat_sdp_media = rcu_dereference(nf_nat_sdp_media_hook);
	if (nf_nat_sdp_media && ct->status & IPS_NAT_MASK && !direct_rtp)
		ret = nf_nat_sdp_media(skb, dataoff, dptr, datalen,
				       rtp_exp, rtcp_exp,
				       mediaoff, medialen, daddr);
	else {
		if (nf_ct_expect_related(rtp_exp) == 0) {
			if (nf_ct_expect_related(rtcp_exp) != 0)
				nf_ct_unexpect_related(rtp_exp);
			else
				ret = NF_ACCEPT;
		}
	}
	nf_ct_expect_put(rtcp_exp);
err2:
	nf_ct_expect_put(rtp_exp);
err1:
	return ret;
}

static const struct sdp_media_type sdp_media_types[] = {
	SDP_MEDIA_TYPE("audio ", SIP_EXPECT_AUDIO),
	SDP_MEDIA_TYPE("video ", SIP_EXPECT_VIDEO),
	SDP_MEDIA_TYPE("image ", SIP_EXPECT_IMAGE),
};

static const struct sdp_media_type *sdp_media_type(const char *dptr,
						   unsigned int matchoff,
						   unsigned int matchlen)
{
	const struct sdp_media_type *t;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sdp_media_types); i++) {
		t = &sdp_media_types[i];
		if (matchlen < t->len ||
		    strncmp(dptr + matchoff, t->name, t->len))
			continue;
		return t;
	}
	return NULL;
}

static int process_sdp(struct sk_buff *skb, unsigned int dataoff,
		       const char **dptr, unsigned int *datalen,
		       unsigned int cseq)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	unsigned int matchoff, matchlen;
	unsigned int mediaoff, medialen;
	unsigned int sdpoff;
	unsigned int caddr_len, maddr_len;
	unsigned int i;
	union nf_inet_addr caddr, maddr, rtp_addr;
	unsigned int port;
	enum sdp_header_types c_hdr;
	const struct sdp_media_type *t;
	int ret = NF_ACCEPT;
	typeof(nf_nat_sdp_addr_hook) nf_nat_sdp_addr;
	typeof(nf_nat_sdp_session_hook) nf_nat_sdp_session;

	nf_nat_sdp_addr = rcu_dereference(nf_nat_sdp_addr_hook);
	c_hdr = nf_ct_l3num(ct) == AF_INET ? SDP_HDR_CONNECTION_IP4 :
					     SDP_HDR_CONNECTION_IP6;

	/* Find beginning of session description */
	if (ct_sip_get_sdp_header(ct, *dptr, 0, *datalen,
				  SDP_HDR_VERSION, SDP_HDR_UNSPEC,
				  &matchoff, &matchlen) <= 0)
		return NF_ACCEPT;
	sdpoff = matchoff;

	/* The connection information is contained in the session description
	 * and/or once per media description. The first media description marks
	 * the end of the session description. */
	caddr_len = 0;
	if (ct_sip_parse_sdp_addr(ct, *dptr, sdpoff, *datalen,
				  c_hdr, SDP_HDR_MEDIA,
				  &matchoff, &matchlen, &caddr) > 0)
		caddr_len = matchlen;

	mediaoff = sdpoff;
	for (i = 0; i < ARRAY_SIZE(sdp_media_types); ) {
		if (ct_sip_get_sdp_header(ct, *dptr, mediaoff, *datalen,
					  SDP_HDR_MEDIA, SDP_HDR_UNSPEC,
					  &mediaoff, &medialen) <= 0)
			break;

		/* Get media type and port number. A media port value of zero
		 * indicates an inactive stream. */
		t = sdp_media_type(*dptr, mediaoff, medialen);
		if (!t) {
			mediaoff += medialen;
			continue;
		}
		mediaoff += t->len;
		medialen -= t->len;

		port = simple_strtoul(*dptr + mediaoff, NULL, 10);
		if (port == 0)
			continue;
		if (port < 1024 || port > 65535)
			return NF_DROP;

		/* The media description overrides the session description. */
		maddr_len = 0;
		if (ct_sip_parse_sdp_addr(ct, *dptr, mediaoff, *datalen,
					  c_hdr, SDP_HDR_MEDIA,
					  &matchoff, &matchlen, &maddr) > 0) {
			maddr_len = matchlen;
			memcpy(&rtp_addr, &maddr, sizeof(rtp_addr));
		} else if (caddr_len)
			memcpy(&rtp_addr, &caddr, sizeof(rtp_addr));
		else
			return NF_DROP;

		ret = set_expected_rtp_rtcp(skb, dataoff, dptr, datalen,
					    &rtp_addr, htons(port), t->class,
					    mediaoff, medialen);
		if (ret != NF_ACCEPT)
			return ret;

		/* Update media connection address if present */
		if (maddr_len && nf_nat_sdp_addr && ct->status & IPS_NAT_MASK) {
			ret = nf_nat_sdp_addr(skb, dataoff, dptr, datalen,
					      mediaoff, c_hdr, SDP_HDR_MEDIA,
					      &rtp_addr);
			if (ret != NF_ACCEPT)
				return ret;
		}
		i++;
	}

	/* Update session connection and owner addresses */
	nf_nat_sdp_session = rcu_dereference(nf_nat_sdp_session_hook);
	if (nf_nat_sdp_session && ct->status & IPS_NAT_MASK)
		ret = nf_nat_sdp_session(skb, dataoff, dptr, datalen, sdpoff,
					 &rtp_addr);

	return ret;
}
static int process_invite_response(struct sk_buff *skb, unsigned int dataoff,
				   const char **dptr, unsigned int *datalen,
				   unsigned int cseq, unsigned int code)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conn_help *help = nfct_help(ct);

	if ((code >= 100 && code <= 199) ||
	    (code >= 200 && code <= 299))
		return process_sdp(skb, dataoff, dptr, datalen, cseq);
	else if (help->help.ct_sip_info.invite_cseq == cseq)
		flush_expectations(ct, true);
	return NF_ACCEPT;
}

static int process_update_response(struct sk_buff *skb, unsigned int dataoff,
				   const char **dptr, unsigned int *datalen,
				   unsigned int cseq, unsigned int code)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conn_help *help = nfct_help(ct);

	if ((code >= 100 && code <= 199) ||
	    (code >= 200 && code <= 299))
		return process_sdp(skb, dataoff, dptr, datalen, cseq);
	else if (help->help.ct_sip_info.invite_cseq == cseq)
		flush_expectations(ct, true);
	return NF_ACCEPT;
}

static int process_prack_response(struct sk_buff *skb, unsigned int dataoff,
				  const char **dptr, unsigned int *datalen,
				  unsigned int cseq, unsigned int code)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conn_help *help = nfct_help(ct);

	if ((code >= 100 && code <= 199) ||
	    (code >= 200 && code <= 299))
		return process_sdp(skb, dataoff, dptr, datalen, cseq);
	else if (help->help.ct_sip_info.invite_cseq == cseq)
		flush_expectations(ct, true);
	return NF_ACCEPT;
}

static int process_invite_request(struct sk_buff *skb, unsigned int dataoff,
				  const char **dptr, unsigned int *datalen,
				  unsigned int cseq)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conn_help *help = nfct_help(ct);
	unsigned int ret;

	flush_expectations(ct, true);
	ret = process_sdp(skb, dataoff, dptr, datalen, cseq);
	if (ret == NF_ACCEPT)
		help->help.ct_sip_info.invite_cseq = cseq;
	return ret;
}

static int process_bye_request(struct sk_buff *skb, unsigned int dataoff,
			       const char **dptr, unsigned int *datalen,
			       unsigned int cseq)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);

	flush_expectations(ct, true);
	return NF_ACCEPT;
}

/* Parse a REGISTER request and create a permanent expectation for incoming
 * signalling connections. The expectation is marked inactive and is activated
 * when receiving a response indicating success from the registrar.
 */
static int process_register_request(struct sk_buff *skb, unsigned int dataoff,
				    const char **dptr, unsigned int *datalen,
				    unsigned int cseq)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conn_help *help = nfct_help(ct);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned int matchoff, matchlen;
	struct nf_conntrack_expect *exp;
	union nf_inet_addr *saddr, daddr;
	__be16 port;
	u8 proto;
	unsigned int expires = 0;
	int ret;
	typeof(nf_nat_sip_expect_hook) nf_nat_sip_expect;

	/* Expected connections can not register again. */
	if (ct->status & IPS_EXPECTED)
		return NF_ACCEPT;

	/* We must check the expiration time: a value of zero signals the
	 * registrar to release the binding. We'll remove our expectation
	 * when receiving the new bindings in the response, but we don't
	 * want to create new ones.
	 *
	 * The expiration time may be contained in Expires: header, the
	 * Contact: header parameters or the URI parameters.
	 */
	if (ct_sip_get_header(ct, *dptr, 0, *datalen, SIP_HDR_EXPIRES,
			      &matchoff, &matchlen) > 0)
		expires = simple_strtoul(*dptr + matchoff, NULL, 10);

	ret = ct_sip_parse_header_uri(ct, *dptr, NULL, *datalen,
				      SIP_HDR_CONTACT, NULL,
				      &matchoff, &matchlen, &daddr, &port);
	if (ret < 0)
		return NF_DROP;
	else if (ret == 0)
		return NF_ACCEPT;

	/* We don't support third-party registrations */
	if (!nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.src.u3, &daddr))
		return NF_ACCEPT;

	if (ct_sip_parse_transport(ct, *dptr, matchoff + matchlen, *datalen,
				   &proto) == 0)
		return NF_ACCEPT;

	if (ct_sip_parse_numerical_param(ct, *dptr,
					 matchoff + matchlen, *datalen,
					 "expires=", NULL, NULL, &expires) < 0)
		return NF_DROP;

	if (expires == 0) {
		ret = NF_ACCEPT;
		goto store_cseq;
	}

	exp = nf_ct_expect_alloc(ct);
	if (!exp)
		return NF_DROP;

	saddr = NULL;
	if (sip_direct_signalling)
		saddr = &ct->tuplehash[!dir].tuple.src.u3;

	nf_ct_expect_init(exp, SIP_EXPECT_SIGNALLING, nf_ct_l3num(ct),
			  saddr, &daddr, proto, NULL, &port);
	exp->timeout.expires = sip_timeout * HZ;
	exp->helper = nfct_help(ct)->helper;
	exp->flags = NF_CT_EXPECT_PERMANENT | NF_CT_EXPECT_INACTIVE;

	nf_nat_sip_expect = rcu_dereference(nf_nat_sip_expect_hook);
	if (nf_nat_sip_expect && ct->status & IPS_NAT_MASK)
		ret = nf_nat_sip_expect(skb, dataoff, dptr, datalen, exp,
					matchoff, matchlen);
	else {
		if (nf_ct_expect_related(exp) != 0)
			ret = NF_DROP;
		else
			ret = NF_ACCEPT;
	}
	nf_ct_expect_put(exp);

store_cseq:
	if (ret == NF_ACCEPT)
		help->help.ct_sip_info.register_cseq = cseq;
	return ret;
}

static int process_register_response(struct sk_buff *skb, unsigned int dataoff,
				     const char **dptr, unsigned int *datalen,
				     unsigned int cseq, unsigned int code)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conn_help *help = nfct_help(ct);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	union nf_inet_addr addr;
	__be16 port;
	u8 proto;
	unsigned int matchoff, matchlen, coff = 0;
	unsigned int expires = 0;
	int in_contact = 0, ret;

	/* According to RFC 3261, "UAs MUST NOT send a new registration until
	 * they have received a final response from the registrar for the
	 * previous one or the previous REGISTER request has timed out".
	 *
	 * However, some servers fail to detect retransmissions and send late
	 * responses, so we store the sequence number of the last valid
	 * request and compare it here.
	 */
	if (help->help.ct_sip_info.register_cseq != cseq)
		return NF_ACCEPT;

	if (code >= 100 && code <= 199)
		return NF_ACCEPT;
	if (code < 200 || code > 299)
		goto flush;

	if (ct_sip_get_header(ct, *dptr, 0, *datalen, SIP_HDR_EXPIRES,
			      &matchoff, &matchlen) > 0)
		expires = simple_strtoul(*dptr + matchoff, NULL, 10);

	while (1) {
		unsigned int c_expires = expires;

		ret = ct_sip_parse_header_uri(ct, *dptr, &coff, *datalen,
					      SIP_HDR_CONTACT, &in_contact,
					      &matchoff, &matchlen,
					      &addr, &port);
		if (ret < 0)
			return NF_DROP;
		else if (ret == 0)
			break;

		/* We don't support third-party registrations */
		if (!nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.dst.u3, &addr))
			continue;

		if (ct_sip_parse_transport(ct, *dptr, matchoff + matchlen,
					   *datalen, &proto) == 0)
			continue;

		ret = ct_sip_parse_numerical_param(ct, *dptr,
						   matchoff + matchlen,
						   *datalen, "expires=",
						   NULL, NULL, &c_expires);
		if (ret < 0)
			return NF_DROP;
		if (c_expires == 0)
			break;
		if (refresh_signalling_expectation(ct, &addr, proto, port,
						   c_expires))
			return NF_ACCEPT;
	}

flush:
	flush_expectations(ct, false);
	return NF_ACCEPT;
}

static const struct sip_handler sip_handlers[] = {
	SIP_HANDLER("INVITE", process_invite_request, process_invite_response),
	SIP_HANDLER("UPDATE", process_sdp, process_update_response),
	SIP_HANDLER("ACK", process_sdp, NULL),
	SIP_HANDLER("PRACK", process_sdp, process_prack_response),
	SIP_HANDLER("BYE", process_bye_request, NULL),
	SIP_HANDLER("REGISTER", process_register_request, process_register_response),
};

static int process_sip_response(struct sk_buff *skb, unsigned int dataoff,
				const char **dptr, unsigned int *datalen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	unsigned int matchoff, matchlen, matchend;
	unsigned int code, cseq, i;

	if (*datalen < strlen("SIP/2.0 200"))
		return NF_ACCEPT;
	code = simple_strtoul(*dptr + strlen("SIP/2.0 "), NULL, 10);
	if (!code)
		return NF_DROP;

	if (ct_sip_get_header(ct, *dptr, 0, *datalen, SIP_HDR_CSEQ,
			      &matchoff, &matchlen) <= 0)
		return NF_DROP;
	cseq = simple_strtoul(*dptr + matchoff, NULL, 10);
	if (!cseq)
		return NF_DROP;
	matchend = matchoff + matchlen + 1;

	for (i = 0; i < ARRAY_SIZE(sip_handlers); i++) {
		const struct sip_handler *handler;

		handler = &sip_handlers[i];
		if (handler->response == NULL)
			continue;
		if (*datalen < matchend + handler->len ||
		    strnicmp(*dptr + matchend, handler->method, handler->len))
			continue;
		return handler->response(skb, dataoff, dptr, datalen,
					 cseq, code);
	}
	return NF_ACCEPT;
}

static int process_sip_request(struct sk_buff *skb, unsigned int dataoff,
			       const char **dptr, unsigned int *datalen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conn_help *help = nfct_help(ct);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned int matchoff, matchlen;
	unsigned int cseq, i;
	union nf_inet_addr addr;
	__be16 port;

	/* Many Cisco IP phones use a high source port for SIP requests, but
	 * listen for the response on port 5060.  If we are the local
	 * router for one of these phones, save the port number from the
	 * Via: header so that nf_nat_sip can redirect the responses to
	 * the correct port.
	 */
	if (ct_sip_parse_header_uri(ct, *dptr, NULL, *datalen,
				    SIP_HDR_VIA_UDP, NULL, &matchoff,
				    &matchlen, &addr, &port) > 0 &&
	    port != ct->tuplehash[dir].tuple.src.u.udp.port &&
	    nf_inet_addr_cmp(&addr, &ct->tuplehash[dir].tuple.src.u3))
		help->help.ct_sip_info.forced_dport = port;

	for (i = 0; i < ARRAY_SIZE(sip_handlers); i++) {
		const struct sip_handler *handler;

		handler = &sip_handlers[i];
		if (handler->request == NULL)
			continue;
		if (*datalen < handler->len ||
		    strnicmp(*dptr, handler->method, handler->len))
			continue;

		if (ct_sip_get_header(ct, *dptr, 0, *datalen, SIP_HDR_CSEQ,
				      &matchoff, &matchlen) <= 0)
			return NF_DROP;
		cseq = simple_strtoul(*dptr + matchoff, NULL, 10);
		if (!cseq)
			return NF_DROP;

		return handler->request(skb, dataoff, dptr, datalen, cseq);
	}
	return NF_ACCEPT;
}

static int process_sip_msg(struct sk_buff *skb, struct nf_conn *ct,
			   unsigned int dataoff, const char **dptr,
			   unsigned int *datalen)
{
	typeof(nf_nat_sip_hook) nf_nat_sip;
	int ret;

	if (strnicmp(*dptr, "SIP/2.0 ", strlen("SIP/2.0 ")) != 0)
		ret = process_sip_request(skb, dataoff, dptr, datalen);
	else
		ret = process_sip_response(skb, dataoff, dptr, datalen);

	if (ret == NF_ACCEPT && ct->status & IPS_NAT_MASK) {
		nf_nat_sip = rcu_dereference(nf_nat_sip_hook);
		if (nf_nat_sip && !nf_nat_sip(skb, dataoff, dptr, datalen))
			ret = NF_DROP;
	}

	return ret;
}

static int sip_help_tcp(struct sk_buff *skb, unsigned int protoff,
			struct nf_conn *ct, enum ip_conntrack_info ctinfo)
{
	struct tcphdr *th, _tcph;
	unsigned int dataoff, datalen;
	unsigned int matchoff, matchlen, clen;
	unsigned int msglen, origlen;
	const char *dptr, *end;
	s16 diff, tdiff = 0;
	int ret = NF_ACCEPT;
	bool term;
	typeof(nf_nat_sip_seq_adjust_hook) nf_nat_sip_seq_adjust;

	if (ctinfo != IP_CT_ESTABLISHED &&
	    ctinfo != IP_CT_ESTABLISHED_REPLY)
		return NF_ACCEPT;

	/* No Data ? */
	th = skb_header_pointer(skb, protoff, sizeof(_tcph), &_tcph);
	if (th == NULL)
		return NF_ACCEPT;
	dataoff = protoff + th->doff * 4;
	if (dataoff >= skb->len)
		return NF_ACCEPT;

	nf_ct_refresh(ct, skb, sip_timeout * HZ);

	if (unlikely(skb_linearize(skb)))
		return NF_DROP;

	dptr = skb->data + dataoff;
	datalen = skb->len - dataoff;
	if (datalen < strlen("SIP/2.0 200"))
		return NF_ACCEPT;

	while (1) {
		if (ct_sip_get_header(ct, dptr, 0, datalen,
				      SIP_HDR_CONTENT_LENGTH,
				      &matchoff, &matchlen) <= 0)
			break;

		clen = simple_strtoul(dptr + matchoff, (char **)&end, 10);
		if (dptr + matchoff == end)
			break;

		term = false;
		for (; end + strlen("\r\n\r\n") <= dptr + datalen; end++) {
			if (end[0] == '\r' && end[1] == '\n' &&
			    end[2] == '\r' && end[3] == '\n') {
				term = true;
				break;
			}
		}
		if (!term)
			break;
		end += strlen("\r\n\r\n") + clen;

		msglen = origlen = end - dptr;
		if (msglen > datalen)
			return NF_DROP;

		ret = process_sip_msg(skb, ct, dataoff, &dptr, &msglen);
		if (ret != NF_ACCEPT)
			break;
		diff     = msglen - origlen;
		tdiff   += diff;

		dataoff += msglen;
		dptr    += msglen;
		datalen  = datalen + diff - msglen;
	}

	if (ret == NF_ACCEPT && ct->status & IPS_NAT_MASK) {
		nf_nat_sip_seq_adjust = rcu_dereference(nf_nat_sip_seq_adjust_hook);
		if (nf_nat_sip_seq_adjust)
			nf_nat_sip_seq_adjust(skb, tdiff);
	}

	return ret;
}

static int sip_help_udp(struct sk_buff *skb, unsigned int protoff,
			struct nf_conn *ct, enum ip_conntrack_info ctinfo)
{
	unsigned int dataoff, datalen;
	const char *dptr;

	/* No Data ? */
	dataoff = protoff + sizeof(struct udphdr);
	if (dataoff >= skb->len)
		return NF_ACCEPT;

	nf_ct_refresh(ct, skb, sip_timeout * HZ);

	if (unlikely(skb_linearize(skb)))
		return NF_DROP;

	dptr = skb->data + dataoff;
	datalen = skb->len - dataoff;
	if (datalen < strlen("SIP/2.0 200"))
		return NF_ACCEPT;

	return process_sip_msg(skb, ct, dataoff, &dptr, &datalen);
}

static struct nf_conntrack_helper sip[MAX_PORTS][4] __read_mostly;
static char sip_names[MAX_PORTS][4][sizeof("sip-65535")] __read_mostly;

static const struct nf_conntrack_expect_policy sip_exp_policy[SIP_EXPECT_MAX + 1] = {
	[SIP_EXPECT_SIGNALLING] = {
		.name		= "signalling",
		.max_expected	= 1,
		.timeout	= 3 * 60,
	},
	[SIP_EXPECT_AUDIO] = {
		.name		= "audio",
		.max_expected	= 2 * IP_CT_DIR_MAX,
		.timeout	= 3 * 60,
	},
	[SIP_EXPECT_VIDEO] = {
		.name		= "video",
		.max_expected	= 2 * IP_CT_DIR_MAX,
		.timeout	= 3 * 60,
	},
	[SIP_EXPECT_IMAGE] = {
		.name		= "image",
		.max_expected	= IP_CT_DIR_MAX,
		.timeout	= 3 * 60,
	},
};

static void nf_conntrack_sip_fini(void)
{
	int i, j;

	for (i = 0; i < ports_c; i++) {
		for (j = 0; j < ARRAY_SIZE(sip[i]); j++) {
			if (sip[i][j].me == NULL)
				continue;
			nf_conntrack_helper_unregister(&sip[i][j]);
		}
	}
}

static int __init nf_conntrack_sip_init(void)
{
	int i, j, ret;
	char *tmpname;

	printk("Linux conntrack_sip init\n");

	if (ports_c == 0)
		ports[ports_c++] = SIP_PORT;

	for (i = 0; i < ports_c; i++) {
		memset(&sip[i], 0, sizeof(sip[i]));

		sip[i][0].tuple.src.l3num = AF_INET;
		sip[i][0].tuple.dst.protonum = IPPROTO_UDP;
		sip[i][0].help = sip_help_udp;
		sip[i][1].tuple.src.l3num = AF_INET;
		sip[i][1].tuple.dst.protonum = IPPROTO_TCP;
		sip[i][1].help = sip_help_tcp;

		sip[i][2].tuple.src.l3num = AF_INET6;
		sip[i][2].tuple.dst.protonum = IPPROTO_UDP;
		sip[i][2].help = sip_help_udp;
		sip[i][3].tuple.src.l3num = AF_INET6;
		sip[i][3].tuple.dst.protonum = IPPROTO_TCP;
		sip[i][3].help = sip_help_tcp;

		for (j = 0; j < ARRAY_SIZE(sip[i]); j++) {
			sip[i][j].tuple.src.u.udp.port = htons(ports[i]);
			sip[i][j].expect_policy = sip_exp_policy;
			sip[i][j].expect_class_max = SIP_EXPECT_MAX;
			sip[i][j].me = THIS_MODULE;

			tmpname = &sip_names[i][j][0];
			if (ports[i] == SIP_PORT)
				sprintf(tmpname, "sip");
			else
				sprintf(tmpname, "sip-%u", i);
			sip[i][j].name = tmpname;

			pr_debug("port #%u: %u\n", i, ports[i]);

			ret = nf_conntrack_helper_register(&sip[i][j]);
			if (ret) {
				printk(KERN_ERR "nf_ct_sip: failed to register"
				       " helper for pf: %u port: %u\n",
				       sip[i][j].tuple.src.l3num, ports[i]);
				nf_conntrack_sip_fini();
				return ret;
			}
		}
	}
	return 0;
}

module_init(nf_conntrack_sip_init);
module_exit(nf_conntrack_sip_fini);

#endif
