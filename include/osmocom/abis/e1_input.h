#ifndef _E1_INPUT_H
#define _E1_INPUT_H

#include <stdlib.h>
#include <netinet/in.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/select.h>
#include <osmocom/abis/subchan_demux.h>

#define NUM_E1_TS   32

enum e1inp_sign_type {
	E1INP_SIGN_NONE,
	E1INP_SIGN_OML,
	E1INP_SIGN_RSL,
};
const char *e1inp_signtype_name(enum e1inp_sign_type tp);

struct e1inp_ts;

struct e1inp_sign_link {
	/* list of signalling links */
	struct llist_head list;

	/* to which timeslot do we belong? */
	struct e1inp_ts *ts;

	enum e1inp_sign_type type;

	/* trx for msg->trx of received msgs */	
	struct gsm_bts_trx *trx;

	/* msgb queue of to-be-transmitted msgs */
	struct llist_head tx_list;

	/* SAPI and TEI on the E1 TS */
	uint8_t sapi;
	uint8_t tei;

	union {
		struct {
			uint8_t channel;
		} misdn;
	} driver;
};

enum e1inp_ts_type {
	E1INP_TS_TYPE_NONE,
	E1INP_TS_TYPE_SIGN,
	E1INP_TS_TYPE_TRAU,
};
const char *e1inp_tstype_name(enum e1inp_ts_type tp);

/* A timeslot in the E1 interface */
struct e1inp_ts {
	enum e1inp_ts_type type;
	int num;

	/* to which line do we belong ? */
	struct e1inp_line *line;

	union {
		struct {
			/* list of all signalling links on this TS */
			struct llist_head sign_links;
			/* delay for the queue */
			int delay;
			/* timer when to dequeue next frame */
			struct osmo_timer_list tx_timer;
		} sign;
		struct {
			/* subchannel demuxer for frames from E1 */
			struct subch_demux demux;
			/* subchannel muxer for frames to E1 */
			struct subch_mux mux;
		} trau;
	};
	union {
		struct {
			/* mISDN driver has one fd for each ts */
			struct osmo_fd fd;
		} misdn;
		struct {
			/* ip.access driver has one fd for each ts */
			struct osmo_fd fd;
		} ipaccess;
		struct {
			/* DAHDI driver has one fd for each ts */
			struct osmo_fd fd;
			struct lapd_instance *lapd;
		} dahdi;
	} driver;
};

struct gsm_e1_subslot {
	/* Number of E1 link */
	uint8_t e1_nr;
	/* Number of E1 TS inside E1 link */
	uint8_t e1_ts;
	/* Sub-slot within the E1 TS, 0xff if full TS */
	uint8_t e1_ts_ss;
};

enum e1inp_line_role {
	E1INP_LINE_R_NONE,
	E1INP_LINE_R_BSC,
	E1INP_LINE_R_BTS,
	E1INP_LINE_R_MAX
};

struct e1inp_driver {
	struct llist_head list;
	const char *name;
	int (*want_write)(struct e1inp_ts *ts);
	int (*line_update)(struct e1inp_line *line, enum e1inp_line_role role, const char *addr);
	int default_delay;
};

struct e1inp_line_ops {
	int	(*sign_link_up)(struct msgb *msg, struct e1inp_line *line);
	int	(*sign_link)(struct msgb *msg, struct e1inp_sign_link *link);
	int	(*error)(struct msgb *msg, int error);
};

struct e1inp_line {
	struct llist_head list;
	int refcnt;

	unsigned int num;
	const char *name;

	/* array of timestlots */
	struct e1inp_ts ts[NUM_E1_TS];

	struct e1inp_line_ops ops;

	struct e1inp_driver *driver;
	void *driver_data;
};

/* SS_INPUT signals */
enum signal_input {
	S_INP_NONE,
	S_INP_TEI_UP,
	S_INP_TEI_DN,
	S_INP_LINE_INIT,
	S_INP_LINE_ALARM,
	S_INP_LINE_NOALARM,
};

/* register a driver with the E1 core */
int e1inp_driver_register(struct e1inp_driver *drv);

/* fine a previously registered driver */
struct e1inp_driver *e1inp_driver_find(const char *name);

/* register a line with the E1 core */
int e1inp_line_register(struct e1inp_line *line);

/* get a line by its ID */
struct e1inp_line *e1inp_line_get(uint8_t e1_nr);

/* create a line in the E1 input core */
struct e1inp_line *e1inp_line_create(uint8_t e1_nr, const char *driver_name, const struct e1inp_line_ops *ops);

/* find a sign_link for given TEI and SAPI in a TS */
struct e1inp_sign_link *
e1inp_lookup_sign_link(struct e1inp_ts *ts, uint8_t tei,
			uint8_t sapi);

/* create a new signalling link in a E1 timeslot */
struct e1inp_sign_link *
e1inp_sign_link_create(struct e1inp_ts *ts, enum e1inp_sign_type type,
			struct gsm_bts_trx *trx, uint8_t tei,
			uint8_t sapi);

/* configure and initialize one signalling e1inp_ts */
int e1inp_ts_config_sign(struct e1inp_ts *ts, struct e1inp_line *line);

/* configure and initialize one timeslot dedicated to TRAU frames. */
int e1inp_ts_config_trau(struct e1inp_ts *ts, struct e1inp_line *line,
                         int (*trau_rcv_cb)(struct subch_demux *dmx, int ch,
					uint8_t *data, int len, void *_priv));

/* Call from the Stack: configuration of this TS has changed */
int e1inp_update_ts(struct e1inp_ts *ts);

/* Receive a packet from the E1 driver */
int e1inp_rx_ts(struct e1inp_ts *ts, struct msgb *msg,
		uint8_t tei, uint8_t sapi);

/* called by driver if it wants to transmit on a given TS */
struct msgb *e1inp_tx_ts(struct e1inp_ts *e1i_ts,
			 struct e1inp_sign_link **sign_link);

/* called by driver in case some kind of link state event */
int e1inp_event(struct e1inp_ts *ts, int evt, uint8_t tei, uint8_t sapi);

/* Write LAPD frames to the fd. */
void e1_set_pcap_fd(int fd);

/* called by TRAU muxer to obtain the destination mux entity */
struct subch_mux *e1inp_get_mux(uint8_t e1_nr, uint8_t ts_nr);

void e1inp_sign_link_destroy(struct e1inp_sign_link *link);
int e1inp_line_update(struct e1inp_line *line, enum e1inp_line_role role, const char *addr);

struct gsm_network;
int ipaccess_setup(struct gsm_network *gsmnet);
int hsl_setup(struct gsm_network *gsmnet);

extern struct llist_head e1inp_driver_list;
extern struct llist_head e1inp_line_list;

int _abis_nm_sendmsg(struct msgb *msg, int to_trx_oml);

/* XXX */
struct input_signal_data {
	int link_type;
	uint8_t tei;
	uint8_t sapi;
	struct gsm_bts_trx *trx;
	struct e1inp_line *line;
};

#endif /* _E1_INPUT_H */