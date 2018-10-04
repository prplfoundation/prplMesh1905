/*
 *  prplMesh Wi-Fi Multi-AP
 *
 *  Copyright (c) 2018, prpl Foundation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
/*
 *  Code based on the 'iw' project from http://git.sipsolutions.net/iw.git/
 *
 *  Copyright (c) 2007, 2008        Johannes Berg
 *  Copyright (c) 2007              Andy Lutomirski
 *  Copyright (c) 2007              Mike Kershaw
 *  Copyright (c) 2008-2009         Luis R. Rodriguez
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/errno.h>
#include <netlink/utils.h>
#include <netlink/msg.h>
#include <netlink/attr.h>

#include "nl80211.h"
#include "ieee80211.h"
#include "platform.h"
#include "netlink_funcs.h"

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg)
{
    PLATFORM_PRINTF_DEBUG_INFO("** error_handler() called **");

    *(int *)arg = err->error;

#ifdef NLM_F_ACK_TLVS
    {
        struct nlmsghdr *nlh = (struct nlmsghdr *)err - 1;
        unsigned         len = nlh->nlmsg_len;
        struct nlattr   *attrs;
        struct nlattr   *tb[NLMSGERR_ATTR_MAX + 1];
        unsigned         ack_len = sizeof(*nlh) + sizeof(int) + sizeof(*nlh);

        /* netlink: extended ACK reporting
         * Only since kernel 4.11.0-rc5
         */
        if ( ! (nlh->nlmsg_flags & NLM_F_ACK_TLVS) )
            return NL_STOP;

        if ( ! (nlh->nlmsg_flags & NLM_F_CAPPED) )
            ack_len += err->msg.nlmsg_len - sizeof(*nlh);

        if ( len <= ack_len )
            return NL_STOP;

        attrs = (void *)((unsigned char *)nlh + ack_len);
        len  -= ack_len;

        nla_parse(tb, NLMSGERR_ATTR_MAX, attrs, len, NULL);

        if ( tb[NLMSGERR_ATTR_MSG] ) {
            len = strnlen((char *)nla_data(tb[NLMSGERR_ATTR_MSG]), nla_len(tb[NLMSGERR_ATTR_MSG]));
            PLATFORM_PRINTF_DEBUG_ERROR("kernel reports: %*s\n", len, (char *)nla_data(tb[NLMSGERR_ATTR_MSG]));
        }
    }
#endif
    return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg)
{
	PLATFORM_PRINTF_DEBUG_INFO("** finish_handler() called **");
	*(int *)arg = 0;
	return NL_SKIP;
}

static int ack_handler(struct nl_msg *msg, void *arg)
{
	PLATFORM_PRINTF_DEBUG_INFO("== ack_handler() called **");
	*(int *)arg = 0;
	return NL_STOP;
}

static int nl80211_init(struct nl80211_state *state)
{
    int err;

    state->nl_sock = nl_socket_alloc();
    if ( ! state->nl_sock ) {
        PLATFORM_PRINTF("ERROR! Failed to allocate netlink socket !");
        return -ENOMEM;
    }

    if ( genl_connect(state->nl_sock) ) {
        PLATFORM_PRINTF("ERROR! Failed to connect to generic netlink !");
        err = -ENOLINK;
        goto out_handle_destroy;
    }
    nl_socket_set_buffer_size(state->nl_sock, 8192, 8192);

    /* try to set NETLINK_EXT_ACK to 1, ignoring errors */
    err = 1;
    setsockopt(nl_socket_get_fd(state->nl_sock), SOL_NETLINK, NETLINK_EXT_ACK, &err, sizeof(err));

    state->nl80211_id = genl_ctrl_resolve(state->nl_sock, "nl80211");
    if ( state->nl80211_id < 0 ) {
        PLATFORM_PRINTF("ERROR! nl80211 not found !");
        err = -ENOENT;
        goto out_handle_destroy;
    }
    return 0;

 out_handle_destroy:
    nl_socket_free(state->nl_sock);
    return err;
}

static void nl80211_cleanup(struct nl80211_state *s)
{
    nl_socket_free(s->nl_sock);
}

//#define CB_DEFAULT      NL_CB_DEFAULT
#define CB_DEFAULT      NL_CB_DEBUG

int netlink_process(enum nl80211_commands cmd, int devidx,
        int (*process)(struct nl_msg *, void *), void *process_datas)
{
    struct nl80211_state     nlstate;
    struct nl_msg           *msg;
    struct nl_cb            *cb,
                            *s_cb;
    int                      err;

    if ( nl80211_init(&nlstate) )
        return -1;

    if ( ! (msg = nlmsg_alloc()) ) {
        PLATFORM_PRINTF("ERROR ! Failed to allocate netlink message\n");
        return -1;
    }

    cb   = nl_cb_alloc(CB_DEFAULT);
    s_cb = nl_cb_alloc(CB_DEFAULT);

    if ( ! (cb && s_cb) ) {
        PLATFORM_PRINTF("ERROR ! Failed to allocate netlink callbacks\n");
        return -1;
    }
    genlmsg_put(msg, 0, 0, nlstate.nl80211_id, 0, 0, cmd, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, devidx);

    nl_socket_set_cb(nlstate.nl_sock, s_cb);

    if ( (err = nl_send_auto_complete(nlstate.nl_sock, msg)) < 0 )
        goto out;

    err = 1;

    nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);

    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler,  &err);
    nl_cb_set(cb, NL_CB_ACK,    NL_CB_CUSTOM, ack_handler,     &err);
    nl_cb_set(cb, NL_CB_VALID,  NL_CB_CUSTOM, (void *)process, process_datas);

    while ( err > 0 )
        nl_recvmsgs(nlstate.nl_sock, cb);

 nla_put_failure:
 out:
    nl_cb_put(cb);
    nl_cb_put(s_cb);
    nlmsg_free(msg);
    nl80211_cleanup(&nlstate);

    return err;
}
