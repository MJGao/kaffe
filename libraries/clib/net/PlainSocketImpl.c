/*
 * java.net.PlainSocketImpl.c
 *
 * Copyright (c) 1996, 1997
 *	Transvirtual Technologies, Inc.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution 
 * of this file. 
 */

#include "config.h"
#include "config-std.h"
#include "config-io.h"
#include "config-mem.h"
#include "config-net.h"
#include <native.h>
#include "java_io_FileDescriptor.h"
#include "java_lang_Integer.h"
#include "java_net_SocketImpl.h"
#include "java_net_InetAddress.h"
#include "java_net_PlainSocketImpl.h"
#include "java_net_SocketOptions.h"
#include "java_io_InterruptedIOException.h"
#include "nets.h"
#include <jsyscall.h>
#include <jthread.h>
#include "../../../kaffe/kaffevm/debug.h"
#include "../../../kaffe/kaffevm/object.h"
#include "../../../kaffe/kaffevm/itypes.h"
#include "../../../kaffe/kaffevm/exception.h"

#include "dummyin6.h"

#if !defined(HAVE_GETADDRINFO) || !defined(HAVE_GETNAMEINFO)
#include "getaddrinfo.h"
#endif

/*
 * Supported socket options
 */
  static const struct {
	  int jopt;
	  int level;
	  int copt;
  } socketOptions[] = {
#ifdef SO_SNDBUF
    { java_net_SocketOptions_SO_SNDBUF,		SOL_SOCKET,	SO_SNDBUF },
#endif
#ifdef SO_RCVBUF
    { java_net_SocketOptions_SO_RCVBUF,		SOL_SOCKET,	SO_RCVBUF },
#endif
#ifdef SO_LINGER
    { java_net_SocketOptions_SO_LINGER,		SOL_SOCKET,	SO_LINGER },
#endif
#ifdef SO_REUSEADDR
    { java_net_SocketOptions_SO_REUSEADDR,	SOL_SOCKET,	SO_REUSEADDR },
#endif
#ifdef TCP_NODELAY
    { java_net_SocketOptions_TCP_NODELAY,	IPPROTO_TCP,	TCP_NODELAY },
#endif
  };

#ifdef KAFFE_VMDEBUG
/*
 * Option names (for debugging only)
 */
  static const struct {
	  int opt;
	  char *name;
  } optionNames[] = {
#ifdef SO_SNDBUF
    { java_net_SocketOptions_SO_SNDBUF, "SO_SNDBUF" },
#endif
#ifdef SO_RCVBUF
    { java_net_SocketOptions_SO_RCVBUF, "SO_RCVBUF" },
#endif
#ifdef SO_LINGER
    { java_net_SocketOptions_SO_LINGER, "SO_LINGER" },
#endif
#ifdef SO_REUSEADDR
    { java_net_SocketOptions_SO_REUSEADDR, "SO_REUSEADDR" },
#endif
#ifdef TCP_NODELAY
    { java_net_SocketOptions_TCP_NODELAY, "TCP_NODELAY" },
#endif
    { java_net_SocketOptions_SO_BINDADDR, "SO_BINDADDR" },
    { java_net_SocketOptions_SO_TIMEOUT, "SO_TIMEOUT" },
    { java_net_SocketOptions_IP_MULTICAST_IF, "IP_MULTICAST_IF" }
  };
#endif /* KAFFE_VMDEBUG */

#ifdef KAFFE_VMDEBUG
static char *
ip2str(jint addr) 
{
	static char addrbuf[16];

	sprintf(addrbuf, "%u.%u.%u.%u",
	  (addr >> 24) & 0xff,
	  (addr >> 16) & 0xff,
	  (addr >>  8) & 0xff,
	  (addr      ) & 0xff);
	return addrbuf;
}
#endif /* KAFFE_VMDEBUG */

/*
 * Create a stream or datagram socket.
 */
void
java_net_PlainSocketImpl_socketCreate(struct Hjava_net_PlainSocketImpl* this, jbool stream)
{
	int fd;
	int type;
	int rc;

	if (stream == 0) {
		type = SOCK_DGRAM;
	}
	else {
		type = SOCK_STREAM;
	}

	DBG(NATIVENET,
	    dprintf("socketCreate(%p, %s)\n", this, stream ? "stream" : "datagram");
	    )

	rc = KSOCKET(AF_INET, type, 0, &fd);
	if (rc) {
		unhand(unhand(this)->fd)->nativeFd = -1;
		SignalError("java.io.IOException", SYS_ERROR(rc));
	}

	DBG(NATIVENET,
	    dprintf("socketCreate(%p, %s) -> fd=%d\n", 
		    this, stream ? "stream" : "datagram", fd);
	    )

	unhand(unhand(this)->fd)->nativeFd = fd;
}

/*
 * Connect the socket to someone.
 */
void
java_net_PlainSocketImpl_socketConnect(struct Hjava_net_PlainSocketImpl* this,
				       struct Hjava_net_InetAddress* daddr, 
				       jint dport, jint timeout)
{
	int fd;
	int r;
	struct sockaddr_in addr;
	int alen;

	DBG(NATIVENET,
	    dprintf("socketConnect(%p, %s, %d, %d)\n", 
		    this, ip2str(unhand(daddr)->address), dport, timeout);
	    )

	memset(&addr, 0, sizeof(addr));
#if defined(BSD44)
	addr.sin_len = sizeof(addr);
#endif
	addr.sin_family = AF_INET;
	addr.sin_port = htons(dport);
	addr.sin_addr.s_addr = htonl(unhand(daddr)->address);

	fd = (int)unhand(unhand(this)->fd)->nativeFd;
	r = KCONNECT(fd, (struct sockaddr*)&addr, sizeof(addr), timeout);
	if (r == EINTR) {
		SignalError("java.io.InterruptedIOException", 
			    "Connect was interrupted");
	}
	if (r == ETIMEDOUT) {
	        SignalError("java.net.SocketTimeoutException",
			    "Connect timed out");
	}
	if (r == EWOULDBLOCK && unhand(this)->blocking) {
		unhand(this)->connecting = true;
		return;
	}
	if (r) {
		SignalError("java.io.IOException", SYS_ERROR(r));
	}

	/* Enter information into socket object */
	alen = sizeof(addr);
	r = KGETSOCKNAME(fd, (struct sockaddr*)&addr, &alen);
	if (r) {
		SignalError("java.io.IOException", SYS_ERROR(r));
	}

	DBG(NATIVENET,
	    dprintf("socketConnect(%p, %s, %d) -> (lport: %d)\n",
		    this, ip2str(unhand(daddr)->address), dport,
		    ntohs(addr.sin_port)
		    );
	    )

	unhand(this)->address = daddr;
	unhand(this)->port = dport;
	unhand(this)->localport = ntohs(addr.sin_port);
}

/*
 * Bind this socket to an address.
 */
void
java_net_PlainSocketImpl_socketBind(struct Hjava_net_PlainSocketImpl* this,
				    struct Hjava_net_InetAddress* laddr, 
				    jint lport)
{
	int r;
	struct sockaddr_in addr;
	int fd;
	int on = 1;
	int alen;

	DBG(NATIVENET,
	    dprintf("socketBind(%p, %s, %d)\n", 
		    this, ip2str(unhand(laddr)->address), lport);
	    )

	memset(&addr, 0, sizeof(addr));
#if defined(BSD44)
	addr.sin_len = sizeof(addr);
#endif
	addr.sin_family = AF_INET;
	addr.sin_port = htons(lport);
	addr.sin_addr.s_addr = htonl(unhand(laddr)->address);

	fd = (int)unhand(unhand(this)->fd)->nativeFd;

	/* Allow rebinding to socket - ignore errors */
	(void)KSETSOCKOPT(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
	r = KBIND(fd, (struct sockaddr*)&addr, sizeof(addr));
	switch( r )
	{
	case 0:
		break;
	case EADDRNOTAVAIL:
	case EADDRINUSE:
	case EACCES:
		SignalError("java.net.BindException", SYS_ERROR(r));
		break;
	default:
		SignalError("java.net.SocketException", SYS_ERROR(r));
		break;
	}

	/* Enter information into socket object */
	unhand(this)->address = laddr;
	if (lport == 0) {
		alen = sizeof(addr);
		r = KGETSOCKNAME(fd, (struct sockaddr*)&addr, &alen);
		if (r) {
			SignalError("java.io.IOException", SYS_ERROR(r));
		}
		lport = ntohs(addr.sin_port);
	}
	unhand(this)->localport = lport;

	DBG(NATIVENET,
	    dprintf("socketBind(%p, %s, -) -> (lport: %d)\n", this,
		    ip2str(unhand(laddr)->address), lport);
	    );
}

/*
 * Turn this socket into a listener.
 */
void
java_net_PlainSocketImpl_socketListen(struct Hjava_net_PlainSocketImpl* this, jint count)
{
	int r;

	DBG(NATIVENET,
	    dprintf("socketListen(%p, count=%d)\n", this, count);
	    )

	r = KLISTEN((int)unhand(unhand(this)->fd)->nativeFd, count);
	if (r) {
		SignalError("java.io.IOException", SYS_ERROR(r));
	}
}

/*
 * Accept a connection.
 */
void
java_net_PlainSocketImpl_socketAccept(struct Hjava_net_PlainSocketImpl* this, struct Hjava_net_SocketImpl* sock)
{
	int r;
	int rc;
	int alen;
	struct sockaddr_in addr;
	HArrayOfByte *remote_addr;
	
	remote_addr = NULL;
	memset(&addr, 0, sizeof(addr));
#if defined(BSD44)
	addr.sin_len = sizeof(addr);
#endif
	addr.sin_family = AF_INET;
	addr.sin_port = htons(unhand(sock)->localport);
	/* I guess the next line is too much as unhand(sock)->address is not supposed
	 * to be initilized.
	 * addr.sin_addr.s_addr = htonl(unhand(unhand(sock)->address)->address);
	 */
	addr.sin_addr.s_addr = INADDR_ANY;

	DBG(NATIVENET,
	    dprintf("socketAccept(%p, localport=%d, addr=%s)\n", 
		    this, ntohs(addr.sin_port), ip2str(ntohl(addr.sin_addr.s_addr)));
	    )

	alen = sizeof(addr);
	do {
	        rc = KACCEPT(unhand(unhand(this)->fd)->nativeFd,
			     (struct sockaddr*)&addr, &alen, unhand(this)->timeout, &r);
	} while (rc == EINTR);
	if (rc == ETIMEDOUT) {
	        SignalError("java.net.SocketTimeoutException",
			    "Accept timed out");
	}
	if (rc) {
		SignalError("java.io.IOException", SYS_ERROR(rc));
	}
	unhand(unhand(sock)->fd)->nativeFd = r;

	/* Enter information into socket object */
	alen = sizeof(addr);
	r = KGETPEERNAME(r, (struct sockaddr*)&addr, &alen);
	if (r) {
		SignalError("java.io.IOException", SYS_ERROR(r));
	}

	/* create a byte array containing the raw address of the connected socket */
	switch (addr.sin_family) {	
		case AF_INET:
			remote_addr = (HArrayOfByte *)newArray(TYPE_CLASS(TYPE_Byte),
							       sizeof(addr.sin_addr));
			memcpy(unhand_byte_array(remote_addr),
			       &addr.sin_addr,
			       sizeof(addr.sin_addr));
			break;

#if notnow && defined(AF_INET6)
		case AF_INET6:
			remote_addr = (HArrayOfByte *)newArray(TYPE_CLASS(TYPE_Byte),
							       sizeof(in6->sin6_addr));
			memcpy(unhand_byte_array(remote_addr),
			       &in6->sin6_addr,
			       sizeof(in6->sin6_addr));
			break;
#endif
		default:
			/* Ignore */
			break;
	}

	/* and use that byte array to create an appropriate Inet*Address instance */
	unhand(sock)->address = do_execute_java_class_method ("java.net.InetAddress",
							      0, 
							      "getByAddress",
							      "([B)Ljava/net/InetAddress;",
							      remote_addr).l;
	unhand(sock)->port = ntohs(addr.sin_port);

	DBG(NATIVENET,
	    dprintf("socketAccept(%p, localport=-, addr=-) -> (sock: %p; addr: %s; port:%d)\n", 
		    this, sock, ip2str(ntohl(addr.sin_addr.s_addr)), ntohs(addr.sin_port));
	    )
}

/*
 * Return how many bytes can be read without blocking.
 */
jint
java_net_PlainSocketImpl_socketAvailable(struct Hjava_net_PlainSocketImpl* this)
{
	int r;
	jint len;
	int fd;
#if (!(defined(HAVE_IOCTL) && defined(FIONREAD)) && !defined(__WIN32__))
	static struct timeval tm = { 0, 0 };
	fd_set rd;
#endif

	DBG(NATIVENET,
	    dprintf("socketAvailable(%p)\n", this);
	    )
	fd = (int)unhand(unhand(this)->fd)->nativeFd;

#if defined(HAVE_IOCTL) && defined(FIONREAD)
	/* XXX make part of system call interface to protect errno */
	r = ioctl(fd, FIONREAD, &len);
	if (r < 0) {
		SignalError("java.io.IOException", SYS_ERROR(errno));
	}
#else
#if defined(__WIN32__) /* Windows hack - XXX */
	len = 0;
#else
	/* This uses KSELECT() to work out if we can read - but what
	 * happens at the end of file?
	 */

	FD_ZERO(&rd);
	FD_SET(fd, &rd);
	KSELECT(fd+1, &rd, NULL, NULL, &tm, &r);	/* XXX ignore ret code*/
	if (r == 1) {
		len = 1;
	}
	else {
		len = 0;
	}
#endif	/* defined(__WIN32__) */
#endif  /* defined(HAVE_IOCTL) && defined(FIONREAD) */

	DBG(NATIVENET,
	    dprintf("socketAvailable(%p) -> %d\n", this, len);
	    )

	return (len);
}

/*
 * Close this socket.
 */
void
java_net_PlainSocketImpl_socketClose(struct Hjava_net_PlainSocketImpl* this)
{
	int r;

	DBG(NATIVENET,
	    dprintf("socketClose(%p)\n", this);
	    )

	if (unhand(unhand(this)->fd)->nativeFd != -1) {
		r = KSOCKCLOSE((int)unhand(unhand(this)->fd)->nativeFd);
		unhand(unhand(this)->fd)->nativeFd = -1;
		if (r) {
			SignalError("java.io.IOException", SYS_ERROR(r));
		}
	}
}

void
java_net_PlainSocketImpl_socketSetOption(struct Hjava_net_PlainSocketImpl* this,
					 jint opt, 
					 struct Hjava_lang_Object* arg)
{
	int k, r, v;

	DBG(NATIVENET,
	    char *optstr = "UNKNOWN";
	    for (k = 0; k < sizeof(optionNames) / sizeof(optionNames[0]); k++) 
		    if (optionNames[k].opt == opt)
			    optstr = optionNames[k].name;
	    dprintf("socketSetOption(%p, %s, arg=%p)\n", this, optstr, arg);
	    )

	/* Do easy cases */
	for (k = 0; k < sizeof(socketOptions) / sizeof(*socketOptions); k++) {
		if (opt == socketOptions[k].jopt) {
			struct linger ling;
			char *optdata;
			int optlen;
			
			v = unhand((struct Hjava_lang_Integer*)arg)->value;
			if( socketOptions[k].copt == SO_LINGER )
			{
				ling.l_onoff = v;
				ling.l_linger = v;
				optdata = (char *)&ling;
				optlen = sizeof(ling);
			}
			else
			{
				optdata = (char *)&v;
				optlen = sizeof(v);
			}
			r = KSETSOCKOPT((int)unhand(unhand(this)->fd)->nativeFd,
				socketOptions[k].level, socketOptions[k].copt,
				optdata, optlen);
			if (r) {
				SignalError("java.net.SocketException", SYS_ERROR(r));
			}
			return;
		}
	}

	/* Do harder cases */
	switch(opt) {
	case java_net_SocketOptions_SO_BINDADDR:
		SignalError("java.net.SocketException", "Read-only socket option");    
		break;
	case java_net_SocketOptions_SO_TIMEOUT: /* java takes care */
	case java_net_SocketOptions_IP_MULTICAST_IF:
	default:
		SignalError("java.net.SocketException", "Unimplemented socket option");    
		break;
	} 
}

jint
java_net_PlainSocketImpl_socketGetOption(struct Hjava_net_PlainSocketImpl* this, jint opt)
{
	struct sockaddr_in addr;
	int alen = sizeof(addr);
	int k, r, v;
	int vsize = sizeof(v);

	DBG(NATIVENET,
	    char *optstr = "UNKNOWN";
	    for (k = 0; k < sizeof(optionNames) / sizeof(optionNames[0]); k++) 
		    if (optionNames[k].opt == opt)
			    optstr = optionNames[k].name;
	    dprintf("socketGetOption(%p, %s)\n", this, optstr);
	    )

	/* Do easy cases */
	for (k = 0; k < sizeof(socketOptions) / sizeof(*socketOptions); k++) {
		if (opt == socketOptions[k].jopt) {
			r = KGETSOCKOPT((int)unhand(unhand(this)->fd)->nativeFd,
				socketOptions[k].level, socketOptions[k].copt,
				&v, &vsize);
			if (r) {
				SignalError("java.net.SocketException", SYS_ERROR(r));
			}
			DBG(NATIVENET,
			    dprintf("socketGetOption(%p, -) -> %d\n", this, v);
			    )
			return v;
		}
	}

	/* Do harder cases */
	switch(opt) {
	case java_net_SocketOptions_SO_BINDADDR:
		r = KGETSOCKNAME((int)unhand(unhand(this)->fd)->nativeFd,
			(struct sockaddr*)&addr, &alen);
		if (r) {
			SignalError("java.net.SocketException", SYS_ERROR(r));
		}
		r = htonl(addr.sin_addr.s_addr);
		break;
	case java_net_SocketOptions_IP_MULTICAST_IF:
	case java_net_SocketOptions_SO_TIMEOUT: /* java takes care */
	default:
		SignalError("java.net.SocketException", "Unimplemented socket option");    
	} 
	DBG(NATIVENET,
	    dprintf("socketGetOption(%p, -) -> %d\n", this, r);
	    )
	return (r);
}

jint
java_net_PlainSocketImpl_socketRead(struct Hjava_net_PlainSocketImpl* this, HArrayOfByte* buf, jint offset, jint len)
{
        ssize_t r;
	int rc;
	int fd;
	int total_read;

	DBG(NATIVENET,
	    dprintf("socket_read(%p, %p, %d, %d)\n", 
		    this, buf, offset, len);
	    )

	fd = (int)unhand(unhand(this)->fd)->nativeFd;
	if (fd < 0) {
		SignalError("java.io.IOException", "fd invalid"); 
	}

	total_read = 0;
	r = 0;
	do {
	         rc = KSOCKREAD(fd, &unhand_array(buf)->body[offset], len, unhand(this)->timeout, &r);

		 if (rc == ETIMEDOUT) {
		         struct Hjava_io_InterruptedIOException* except;

			 except = (struct Hjava_io_InterruptedIOException *)
			   execute_java_constructor(
						    "java.net.SocketTimeoutException", 0, 0,
						    "(Ljava/lang/String;)V",
						    checkPtr(stringC2Java("Read was interrupted")));
			 except->bytesTransferred = r;
	      
			 throwException((struct Hjava_lang_Throwable*)except);
		 } else if (rc != EINTR && rc != 0) {
		   SignalError("java.io.IOException", SYS_ERROR(rc));
		 } else if (rc == 0 && r == 0 && len > 0) {
		   return (-1);
		 }
		 offset += r;
		 len -= r;
		 total_read += r;
	} while (rc == EINTR);
	return (total_read);
}

void
java_net_PlainSocketImpl_socketWrite(struct Hjava_net_PlainSocketImpl* this, HArrayOfByte* buf, jint offset, jint len)
{
	int r;
	int fd;
	ssize_t nw;

	DBG(NATIVENET,
	    dprintf("socket_write(%p, %p, %d, %d)\n", 
		    this, buf, offset, len);
	    )

	fd = (int)unhand(unhand(this)->fd)->nativeFd;
	if (fd >= 0) {
		while (len > 0) {
			r = KSOCKWRITE(fd,
			    &unhand_array(buf)->body[offset], len, &nw);
			if (r) {
				SignalError("java.io.IOException", SYS_ERROR(r));
			}
			offset += nw;
			len -= nw;
		}
	} else {
		SignalError("java.io.IOException", "fd invalid"); 
	}
}

void
java_net_PlainSocketImpl_setBlocking(struct Hjava_net_PlainSocketImpl* this, jbool blocking)
{
	if (blocking == unhand(this)->blocking)
		return;
	
	unhand(this)->blocking = true;
	jthread_set_blocking((int)unhand(unhand(this)->fd)->nativeFd, blocking);
}

void
java_net_PlainSocketImpl_waitForConnection(struct Hjava_net_PlainSocketImpl* this)
{
	fd_set w;
	int fd = (int)unhand(unhand(this)->fd)->nativeFd;
	int o, r;
	struct timeval tv;
	struct timeval *ptv = NULL;
	
	if (!unhand(this)->blocking) {
		if (!unhand(this)->connecting)
			return;
		
		FD_ZERO(&w);
		FD_SET(fd, &w);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		ptv = &tv;
	}

	r = KSELECT(fd+1, NULL, &w, NULL, ptv, &o);
	if (r == EINTR) {
		SignalError("java.io.InterruptedIOException", SYS_ERROR(r));
	}
	if (r != 0) {
		SignalError("java.io.IOException", SYS_ERROR(r));
	}
	if (o != 0 && FD_ISSET(fd, &w))
		unhand(this)->connecting = false;
}
