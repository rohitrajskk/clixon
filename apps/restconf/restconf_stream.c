/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2019 Olof Hagsand
  Copyright (C) 2020 Olof Hagsand and Rubicon Communications, LLC(Netgate)

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****
  
  Restconf event stream implementation. 
  See RFC 8040  RESTCONF Protocol
  Sections 3.8, 6, 9.3

  RFC8040:
   A RESTCONF server MAY send the "retry" field, and if it does, RESTCONF
   clients SHOULD use it.  A RESTCONF server SHOULD NOT send the "event" 
   or "id" fields, as there are no meaningful values. RESTCONF
   servers that do not send the "id" field also do not need to support
   the HTTP header field "Last-Event-ID"

   The RESTCONF client can then use this URL value to start monitoring
   the event stream:

      GET /streams/NETCONF HTTP/1.1
      Host: example.com
      Accept: text/event-stream
      Cache-Control: no-cache
      Connection: keep-alive

   The server MAY support the "start-time", "stop-time", and "filter"
   query parameters, defined in Section 4.8.  Refer to Appendix B.3.6
   for filter parameter examples.

 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>

#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <libgen.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clixon/clixon.h>

#include <fcgiapp.h> /* Need to be after clixon_xml.h due to attribute format */

#include "restconf_lib.h"
#include "restconf_handle.h"
#include "restconf_api.h"
#include "restconf_err.h"
#include "restconf_stream.h"

/*
 * Constants
 */
/* Enable for forking stream subscription loop. 
 * Disable to get single threading but blocking on streams
 */
#define STREAM_FORK 1

/* Keep track of children - whjen they exit - their FCGX handle needs to be 
 * freed with  FCGX_Free(&rbk, 0);
 */
struct stream_child{
    qelem_t                     sc_q;   /* queue header */
    int                         sc_pid; /* Child process id */
    FCGX_Request                sc_r;   /* FCGI stream data */
};
/* Linked list of children
 * @note could hang STREAM_CHILD list on clicon handle instead.
 */
static struct stream_child *STREAM_CHILD = NULL; 

/*! Find restconf child using PID and cleanup FCGI Request data
 * @param[in]  h   Clicon handle
 * @param[in]  pid Process id of child
 * @note could hang STREAM_CHILD list on clicon handle instead.
 */
int
stream_child_free(clicon_handle h,
		  int           pid)
{
    struct stream_child *sc;
    
    if ((sc = STREAM_CHILD) != NULL){
	do {
	    if (pid == sc->sc_pid){
		DELQ(sc, STREAM_CHILD, struct stream_child *);
		FCGX_Free(&sc->sc_r, 0);
		free(sc);
		goto done;
	    }
	    sc = NEXTQ(struct stream_child *, sc);
	} while (sc && sc !=  STREAM_CHILD);
    }
 done:
    return 0;
}

int
stream_child_freeall(clicon_handle h)
{
    struct stream_child *sc;

    while ((sc = STREAM_CHILD) != NULL){
	DELQ(sc, STREAM_CHILD, struct stream_child *);
	FCGX_Free(&sc->sc_r, 1);
	free(sc);
    }
    return 0;
}

/*! Callback when stream notifications arrive from backend
 */
static int
restconf_stream_cb(int   s, 
		   void *arg)
{
    int                retval = -1;
    FCGX_Request      *r = (FCGX_Request *)arg;
    int                eof;
    struct clicon_msg *reply = NULL;
    cxobj             *xtop = NULL; /* top xml */
    cxobj             *xn;        /* notification xml */
    cbuf              *cb = NULL;
    int                pretty = 0; /* XXX should be via arg */
    int                ret;
    
    clicon_debug(1, "%s", __FUNCTION__);
    /* get msg (this is the reason this function is called) */
    if (clicon_msg_rcv(s, &reply, &eof) < 0){
	clicon_debug(1, "%s msg_rcv error", __FUNCTION__);
	goto done;
    }
    clicon_debug(1, "%s msg: %s", __FUNCTION__, reply?reply->op_body:"null");
    /* handle close from remote end: this will exit the client */
    if (eof){
	clicon_debug(1, "%s eof", __FUNCTION__);
	clicon_err(OE_PROTO, ESHUTDOWN, "Socket unexpected close");
	errno = ESHUTDOWN;
	FCGX_FPrintF(r->out, "SHUTDOWN\r\n");
	FCGX_FPrintF(r->out, "\r\n");
	FCGX_FFlush(r->out);
	clicon_exit_set(); 
	goto done;
    }
    if ((ret = clicon_msg_decode(reply, NULL, NULL, &xtop, NULL)) < 0)  /* XXX pass yang_spec */
	goto done;
    if (ret == 0){
	clicon_err(OE_XML, EFAULT, "Invalid notification");
	goto done;
    }
    /* create event */
    if ((cb = cbuf_new()) == NULL){
	clicon_err(OE_PLUGIN, errno, "cbuf_new");
	goto done;
    }
    if ((xn = xpath_first(xtop, NULL, "notification")) == NULL)
	goto ok;
#ifdef notused
    xt = xpath_first(xn, NULL, "eventTime");
    if ((xe = xpath_first(xn, NULL, "event")) == NULL) /* event can depend on yang? */
	goto ok;

    if (xt)
	FCGX_FPrintF(r->out, "M#id: %s\r\n", xml_body(xt));
    else{ /* XXX */
	gettimeofday(&tv, NULL);
	FCGX_FPrintF(r->out, "M#id: %02d:0\r\n", tv.tv_sec);
    }
#endif
    if (clicon_xml2cbuf(cb, xn, 0, pretty, -1) < 0)
	goto done;
    FCGX_FPrintF(r->out, "data: %s\r\n", cbuf_get(cb));
    FCGX_FPrintF(r->out, "\r\n");
    FCGX_FFlush(r->out);
 ok:
    retval = 0;
 done:
    clicon_debug(1, "%s retval: %d", __FUNCTION__, retval);
    if (xtop != NULL)
	xml_free(xtop);
    if (reply)
	free(reply);
    if (cb)
	cbuf_free(cb);
    return retval;
}

/*! Send subsctription to backend
 * @param[in] h    Clicon handle
 * @param[in] r    Fastcgi request handle
 * @param[in] name Stream name
 * @param[out] sp  Socket -1 if not set
 */
static int
restconf_stream(clicon_handle h,
		FCGX_Request *r,
		char         *name,
		cvec         *qvec, 
		int           pretty,
		restconf_media media_out,
		int          *sp)
{
    int     retval = -1;
    cxobj  *xret = NULL;
    cxobj  *xe;
    cbuf   *cb = NULL;
    int     s; /* socket */
    int     i;
    cg_var *cv;
    char   *vname;

    clicon_debug(1, "%s", __FUNCTION__);
    *sp = -1;
    if ((cb = cbuf_new()) == NULL){
	clicon_err(OE_XML, errno, "cbuf_new");
	goto done;
    }
    cprintf(cb, "<rpc><create-subscription xmlns=\"%s\"><stream>%s</stream>",
	    EVENT_RFC5277_NAMESPACE, name);
    /* Print all fields */
    for (i=0; i<cvec_len(qvec); i++){
        cv = cvec_i(qvec, i);
	vname = cv_name_get(cv);
	if (strcmp(vname, "start-time") == 0){
	    cprintf(cb, "<startTime>");
	    cv2cbuf(cv, cb);
	    cprintf(cb, "</startTime>");
	}
	else if (strcmp(vname, "stop-time") == 0){
	    cprintf(cb, "<stopTime>");
	    cv2cbuf(cv, cb);
	    cprintf(cb, "</stopTime>");
	}
    }
    cprintf(cb, "</create-subscription></rpc>]]>]]>");
    if (clicon_rpc_netconf(h, cbuf_get(cb), &xret, &s) < 0)
	goto done;
    if ((xe = xpath_first(xret, NULL, "rpc-reply/rpc-error")) != NULL){
	if (api_return_err(h, r, xe, pretty, media_out, 0) < 0)
	    goto done;
	goto ok;
    }
    /* Setting up stream */
    FCGX_SetExitStatus(201, r->out); /* Created */
    FCGX_FPrintF(r->out, "Status: 201 Created\r\n");
    FCGX_FPrintF(r->out, "Content-Type: text/event-stream\r\n");
    FCGX_FPrintF(r->out, "Cache-Control: no-cache\r\n");
    FCGX_FPrintF(r->out, "Connection: keep-alive\r\n");
    FCGX_FPrintF(r->out, "X-Accel-Buffering: no\r\n");
    FCGX_FPrintF(r->out, "\r\n");
    FCGX_FFlush(r->out);
    *sp = s;
 ok:
    retval = 0;
 done:
    clicon_debug(1, "%s retval: %d", __FUNCTION__, retval);
    if (xret)
	xml_free(xret);
    if (cb)
	cbuf_free(cb);
    return retval;
}

/* restconf */
#include "restconf_lib.h"
#include "restconf_stream.h"

static int
stream_checkuplink(int   s, 
		   void *arg)
{
    FCGX_Request *r = (FCGX_Request *)arg;
    clicon_debug(1, "%s", __FUNCTION__);
    if (FCGX_GetError(r->out) != 0){ /* break loop */
	clicon_debug(1, "%s FCGX_GetError upstream", __FUNCTION__);
	clicon_exit_set();
    }
    return 0;
}

int
stream_timeout(int   s,
	       void *arg)
{
    struct timeval t;
    struct timeval t1;
    FCGX_Request *r = (FCGX_Request *)arg;
    
    clicon_debug(1, "%s", __FUNCTION__);
    if (FCGX_GetError(r->out) != 0){ /* break loop */
	clicon_debug(1, "%s FCGX_GetError upstream", __FUNCTION__);
	clicon_exit_set();
    }
    else{
	gettimeofday(&t, NULL);
	t1.tv_sec = 1; t1.tv_usec = 0;
	timeradd(&t, &t1, &t);
	clixon_event_reg_timeout(t, stream_timeout, arg, "Stream timeout");
    }
    return 0;
} 

/*! Process a FastCGI request
 * @param[in]  r        Fastcgi request handle
 */
int
api_stream(clicon_handle h,
	   FCGX_Request *r,
	   char         *streampath,
	   int          *finish)
{
    int    retval = -1;
    char  *path;
    char  *query;
    char  *method;
    char **pvec = NULL;
    int    pn;
    cvec  *qvec = NULL;
    cvec  *dvec = NULL;
    cvec  *pcvec = NULL; /* for rest api */
    cbuf  *cb = NULL;
    char  *data;
    int    authenticated = 0;
    int    pretty;
    restconf_media media_out = YANG_DATA_XML; /* XXX default */
    cbuf  *cbret = NULL;
    cxobj *xret = NULL;
    cxobj *xerr;
    int    s=-1;
#ifdef STREAM_FORK
    int    pid;
    struct stream_child *sc;
#endif

    clicon_debug(1, "%s", __FUNCTION__);
    path = restconf_uripath(h);
    query = restconf_param_get(h, "QUERY_STRING");
    pretty = clicon_option_bool(h, "CLICON_RESTCONF_PRETTY");
    if ((pvec = clicon_strsep(path, "/", &pn)) == NULL)
	goto done;
    /* Sanity check of path. Should be /stream/<name> */
    if (pn != 3){
	restconf_notfound(h, r);
	goto ok;
    }
    if (strlen(pvec[0]) != 0){
	retval = restconf_notfound(h, r);
	goto done;
    }
    if (strcmp(pvec[1], streampath)){
	retval = restconf_notfound(h, r);
	goto done;
    }

    if ((method = pvec[2]) == NULL){
	retval = restconf_notfound(h, r);
	goto done;
    }
    clicon_debug(1, "%s: method=%s", __FUNCTION__, method);
    if (str2cvec(query, '&', '=', &qvec) < 0)
	goto done;
    if (str2cvec(path, '/', '=', &pcvec) < 0) /* rest url eg /album=ricky/foo */
	goto done;
    /* data */
    if ((cb = restconf_get_indata(r)) == NULL)
	goto done;
    data = cbuf_get(cb);
    clicon_debug(1, "%s DATA=%s", __FUNCTION__, data);
    if (str2cvec(data, '&', '=', &dvec) < 0)
	goto done;
    /* If present, check credentials. See "plugin_credentials" in plugin  
     * See RFC 8040 section 2.5
     */
    if ((authenticated = clixon_plugin_auth_all(h, r)) < 0)
	goto done;
    clicon_debug(1, "%s auth:%d %s", __FUNCTION__, authenticated, clicon_username_get(h));

    /* If set but no user, we set a dummy user */
    if (authenticated){
	if (clicon_username_get(h) == NULL)
	    clicon_username_set(h, "none");
    }
    else{
	if (netconf_access_denied_xml(&xret, "protocol", "The requested URL was unauthorized") < 0)
	    goto done;
	if ((xerr = xpath_first(xret, NULL, "//rpc-error")) != NULL){
	    if (api_return_err(h, r, xerr, pretty, media_out, 0) < 0)
		goto done;
	    goto ok;
	}
	goto ok;
    }
    clicon_debug(1, "%s auth2:%d %s", __FUNCTION__, authenticated, clicon_username_get(h));
    if (restconf_stream(h, r, method, qvec, pretty, media_out, &s) < 0)
	goto done;
    if (s != -1){
#ifdef STREAM_FORK
	if ((pid = fork()) == 0){ /* child */
	    if (pvec)
		free(pvec);
	    if (dvec)
		cvec_free(dvec);
	    if (qvec)
		cvec_free(qvec);
	    if (pcvec)
		cvec_free(pcvec);
	    if (cb)
		cbuf_free(cb);
	    if (cbret)
		cbuf_free(cbret);
	    if (xret)
		xml_free(xret);
#endif /* STREAM_FORK */
	    /* Listen to backend socket */
	    if (clixon_event_reg_fd(s, 
			     restconf_stream_cb, 
			     (void*)r,
			     "stream socket") < 0)
		goto done;
	    if (clixon_event_reg_fd(r->listen_sock,
			     stream_checkuplink, 
			     (void*)r,
			     "stream socket") < 0)
		goto done;
	    /* Poll upstream errors */
	    stream_timeout(0, (void*)r);
	    /* Start loop */
	    clixon_event_loop();
	    close(s);
	    clixon_event_unreg_fd(s, restconf_stream_cb);
	    clixon_event_unreg_fd(r->listen_sock, restconf_stream_cb);
	    clixon_event_unreg_timeout(stream_timeout, (void*)r);
	    clicon_exit_reset();
#ifdef STREAM_FORK
	    FCGX_Finish_r(r);
	    FCGX_Free(r, 0);	    
	    restconf_terminate(h);
	    exit(0);
	}
	/* parent */
	/* Create stream_child struct and store pid and FCGI data, when child
	 * killed, call FCGX_Free
	 */
	if ((sc = malloc(sizeof(struct stream_child))) == NULL){
	    clicon_err(OE_XML, errno, "malloc");
	    goto done;
	}
	memset(sc, 0, sizeof(struct stream_child));
	sc->sc_pid = pid;
	sc->sc_r = *r;
	ADDQ(sc, STREAM_CHILD);
	*finish = 0; /* If spawn child, we should not finish this stream */
#endif /* STREAM_FORK */
    }
 ok:
    retval = 0;
 done:
    clicon_debug(1, "%s retval:%d", __FUNCTION__, retval);
    if (pvec)
	free(pvec);
    if (dvec)
	cvec_free(dvec);
    if (qvec)
	cvec_free(qvec);
    if (pcvec)
	cvec_free(pcvec);
    if (cb)
	cbuf_free(cb);
    if (cbret)
	cbuf_free(cbret);
    if (xret)
	xml_free(xret);
    return retval;
}
