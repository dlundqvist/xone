// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xbox wireless dongle transport on top of a mac80211 monitor interface.
 *
 * The kernel's mt76x2u driver runs the radio on Mediatek's firmware from
 * linux-firmware, so transmit power, calibration and regulatory limits stay
 * with that driver. This module connects controllers to the GIP bus by sending
 * and receiving frames through that driver's monitor interface, so the dongle
 * needs no firmware from Microsoft.
 *
 * Frame formats follow transport/mt76.c and transport/dongle.c. CCMP is done
 * here in software, because the hardware keys aren't reachable through a
 * monitor interface.
 *
 * Userspace puts the interface in monitor mode, with the dongle's own MAC
 * address, and sets the channel given by the channel parameter. This module
 * attaches when that interface comes up. See install/mtnet/.
 *
 * This module polls the dongle's button and drives its LED, through vendor USB
 * requests alongside mt76x2u. Pressing the button starts pairing, as does writing
 * 1 to the pairing parameter. The LED blinks while pairing, stays on while a
 * client is connected, and is off otherwise.
 *
 * Not handled: moving between channels, wake on wireless, and more than one dongle.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/usb.h>
#include <linux/unaligned.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/math64.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/rtnetlink.h>
#include <crypto/aead.h>
#include <net/ieee80211_radiotap.h>

#include "../bus/bus.h"

#define MTNET_MAX_CLIENTS 8
/* Beacons go out every 100 TU (102.4 ms), on the slot their timestamp names */
#define MTNET_BEACON_TU 100
#define MTNET_BEACON_NS (MTNET_BEACON_TU * 1024 * NSEC_PER_USEC)
#define MTNET_KEEPALIVE_MS 10
#define MTNET_CLIENT_TIMEOUT_MS 10000
#define MTNET_PAIRING_TIMEOUT_MS 60000
#define MTNET_BUTTON_MS 50

/* Vendor USB requests, as used by mt76 */
#define MTNET_VEND_MULTI_WRITE 0x06
#define MTNET_VEND_MULTI_READ 0x07
#define MTNET_VEND_TIMEOUT_MS 300

/* Bit is clear while the button is held. Found by observing the register. */
#define MTNET_REG_BUTTON 0x0064
#define MTNET_BUTTON_RELEASED BIT(30)

/*
 * The dongle transmits and receives on one chain, and mt76x2u sets this chip up
 * for two. These are the steps of the kernel's mt76x2_phy_set_antenna() for chain
 * 0, which exists only for the PCI version of the chip.
 */
#define MTNET_REG_TX_PIN_CFG 0x1328
#define MTNET_TX_PIN_TXANT GENMASK(3, 0)
#define MTNET_REG_BBP_CORE32 (0x2000 + (32 << 2))
#define MTNET_REG_BBP_CORE33 (0x2000 + (33 << 2))
#define MTNET_REG_BBP_IBI9 (0x2100 + (9 << 2))
#define MTNET_REG_BBP_AGC0 0x2300
#define MTNET_REG_BBP_TXBE5 (0x2700 + (5 << 2))

/* The LED is active low on pin 2 of the MT_LED registers */
#define MTNET_LED_PIN 2
#define MTNET_REG_LED_CTRL 0x0770
#define MTNET_REG_LED_S0 (0x077c + 8 * MTNET_LED_PIN)
#define MTNET_REG_LED_S1 (0x0780 + 8 * MTNET_LED_PIN)
#define MTNET_LED_CTRL_REPLAY BIT(8 * MTNET_LED_PIN)
#define MTNET_LED_CTRL_KICK BIT(7 + 8 * MTNET_LED_PIN)
#define MTNET_LED_STATUS(on, off) (0xff << 8 | (on) << 16 | (off) << 24)
#define MTNET_BUFFER_LEN 2048
#define MTNET_RESERVED_STYPE 0x70
/* Internal event: the client's session no longer matches ours */
#define MTNET_STALE_SESSION 0xffff
#define MTNET_REASON_NOT_ASSOCIATED 7
#define MTNET_MAX_PLAIN_FRAMES 3
#define MTNET_SETUP_TIMEOUT_MS 8000
#define MTNET_PAIR_REQ 0x01
#define MTNET_PAIR_RESP 0x02
#define MTNET_ENABLE_ENCRYPTION 0x10
#define MTNET_QOS_HDR_LEN 26
#define MTNET_CCMP_HDR_LEN 8
#define MTNET_CCMP_MIC_LEN 8
#define MTNET_KEY_LEN 16

static int channel = 153;
module_param(channel, int, 0444);
MODULE_PARM_DESC(channel, "Channel userspace must set on the interface, announced in the beacon");

static bool pairing;
module_param(pairing, bool, 0644);
MODULE_PARM_DESC(pairing, "Announce pairing mode in the beacon, for up to 60 seconds");

/* Version 0, 9 bytes long, with the rate field: 6 Mb/s in 500 kb/s units */
static const u8 mtnet_radiotap[] = { 0, 0, 9, 0, 0x04, 0, 0, 0, 12 };

struct mtnet_client {
	u8 address[ETH_ALEN];
	bool encryption_enabled;
	u64 tx_pn;
	u64 rx_pn;
	unsigned long last_rx;
	u64 last_reply_ns;
	unsigned long last_stale;
	unsigned long added;
	u16 last_seq;
	unsigned int plain_frames;
	struct crypto_aead *aead;
	struct gip_adapter *adapter;
};

struct mtnet_event {
	struct work_struct work;
	u16 stype;
	u8 address[ETH_ALEN];
	u8 cmd;
};

enum mtnet_led {
	MTNET_LED_UNSET,
	MTNET_LED_OFF,
	MTNET_LED_ON,
	MTNET_LED_BLINK,
};

static struct {
	struct net_device *ndev;
	struct usb_device *udev;
	__le32 *regbuf;
	struct delayed_work button_work;
	enum mtnet_led led;
	bool button_down;
	struct packet_type ptype;
	struct hrtimer beacon_timer;
	struct delayed_work timeout_work;
	struct delayed_work keepalive_work;
	struct workqueue_struct *wq;
	/* Beacons and keepalives can't wait for USB requests or client teardown */
	struct workqueue_struct *txwq;
	spinlock_t lock;
	struct mtnet_client *clients[MTNET_MAX_CLIENTS];
	atomic_t seq;
	bool running;
	unsigned long pairing_since;
	unsigned long last_unknown;
} mtnet;

static void mtnet_xmit(struct sk_buff *skb)
{
	memcpy(skb_push(skb, sizeof(mtnet_radiotap)), mtnet_radiotap, sizeof(mtnet_radiotap));
	skb->dev = mtnet.ndev;
	skb->protocol = htons(ETH_P_802_2);
	skb->pkt_type = PACKET_OUTGOING;
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	dev_queue_xmit(skb);
}

static __le16 mtnet_next_seq(void)
{
	return cpu_to_le16((atomic_inc_return(&mtnet.seq) & 0xfff) << 4);
}

static struct sk_buff *mtnet_alloc(unsigned int len)
{
	struct sk_buff *skb = alloc_skb(LL_MAX_HEADER + 64 + len + MTNET_CCMP_MIC_LEN, GFP_ATOMIC);

	if (skb)
		skb_reserve(skb, LL_MAX_HEADER + 64);
	return skb;
}

/* Management frame: 24-byte header, then the body */
static int mtnet_send_mgmt(u16 fc, const u8 *addr, const void *body, unsigned int len)
{
	struct ieee80211_hdr_3addr *hdr;
	struct sk_buff *skb = mtnet_alloc(sizeof(*hdr) + len);

	if (!skb)
		return -ENOMEM;
	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(fc);
	ether_addr_copy(hdr->addr1, addr);
	ether_addr_copy(hdr->addr2, mtnet.ndev->dev_addr);
	ether_addr_copy(hdr->addr3, mtnet.ndev->dev_addr);
	hdr->seq_ctrl = mtnet_next_seq();
	if (len)
		skb_put_data(skb, body, len);
	mtnet_xmit(skb);
	return 0;
}

/*
 * Controllers expect a beacon every 100 TU, on the slot the timestamp
 * names, as from a hardware beacon timer. A workqueue drifts by a few
 * milliseconds a beacon, so beacons come from an absolute hrtimer instead,
 * each stamped with the time of its slot.
 */
static enum hrtimer_restart mtnet_beacon(struct hrtimer *timer)
{
	static const u8 prefix[] = { 0xdd, 0x10, 0x00, 0x50, 0xf2, 0x11, 0x01, 0x10 };
	static const u8 broadcast[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	ktime_t slot = hrtimer_get_expires(timer);
	u8 body[12 + 2 + 18] = {};
	u8 *ie = body + 14;
	u64 late;

	late = hrtimer_forward_now(timer, ns_to_ktime(MTNET_BEACON_NS));
	if (late > 1) {
		netdev_dbg(mtnet.ndev, "beacon late: %llu slots missed\n", late - 1);
		slot = ktime_sub_ns(hrtimer_get_expires(timer), MTNET_BEACON_NS);
	}

	*(__le64 *)body = cpu_to_le64(ktime_to_us(slot));
	*(__le16 *)(body + 8) = cpu_to_le16(MTNET_BEACON_TU);
	*(__le16 *)(body + 10) = cpu_to_le16(0xc631);
	/* An empty SSID element sits at body[12..13], left zero */
	memcpy(ie, prefix, sizeof(prefix));
	ie[8] = READ_ONCE(pairing) ? 1 : 0;
	ie[9] = 0xa5;
	ie[10] = 0x30;
	ie[11] = 0x99;
	ie[12] = channel;

	mtnet_send_mgmt(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON, broadcast, body, sizeof(body));
	return HRTIMER_RESTART;
}

/*
 * Controllers are in power save: they stay awake briefly after each frame
 * they send, and Microsoft's firmware answers each one with a QoS Null
 * within 0.3 ms. A frame sent on our own schedule can land while the
 * controller sleeps, and long runs of those make it give up on the link.
 */
static void mtnet_send_null(const u8 *addr)
{
	struct ieee80211_qos_hdr *hdr;
	struct sk_buff *skb = mtnet_alloc(MTNET_QOS_HDR_LEN);

	if (!skb)
		return;
	hdr = skb_put_zero(skb, MTNET_QOS_HDR_LEN);
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC |
					 IEEE80211_FCTL_FROMDS);
	ether_addr_copy(hdr->addr1, addr);
	ether_addr_copy(hdr->addr2, mtnet.ndev->dev_addr);
	ether_addr_copy(hdr->addr3, mtnet.ndev->dev_addr);
	hdr->seq_ctrl = mtnet_next_seq();
	mtnet_xmit(skb);
}

static void mtnet_keepalive(struct work_struct *work)
{
	u8 addrs[MTNET_MAX_CLIENTS][ETH_ALEN];
	unsigned long flags;
	u64 now = ktime_get_ns();
	int i, count = 0;

	/* Only for clients that went quiet: the others get a reply to each frame */
	spin_lock_irqsave(&mtnet.lock, flags);
	for (i = 0; i < MTNET_MAX_CLIENTS; i++)
		if (mtnet.clients[i] &&
		    now - mtnet.clients[i]->last_reply_ns > 2 * MTNET_KEEPALIVE_MS * NSEC_PER_MSEC)
			ether_addr_copy(addrs[count++], mtnet.clients[i]->address);
	spin_unlock_irqrestore(&mtnet.lock, flags);

	for (i = 0; i < count; i++)
		mtnet_send_null(addrs[i]);
	queue_delayed_work(mtnet.txwq, &mtnet.keepalive_work, msecs_to_jiffies(MTNET_KEEPALIVE_MS));
}

/* AAD and nonce as in IEEE 802.11 CCMP, for a QoS data frame with three addresses */
static int mtnet_ccmp(struct crypto_aead *aead, const u8 *hdr, u64 pn, u8 *data, unsigned int len,
		      bool encrypt)
{
	struct aead_request *req;
	struct scatterlist sg[2];
	u16 fc = get_unaligned_le16(hdr);
	unsigned int crypt_len = encrypt ? len + MTNET_CCMP_MIC_LEN : len;
	u8 *mem, *aad, *iv;
	int err, i;

	mem = kzalloc(sizeof(*req) + crypto_aead_reqsize(aead) + 24 + 16, GFP_ATOMIC);
	if (!mem)
		return -ENOMEM;
	req = (struct aead_request *)mem;
	aad = mem + sizeof(*req) + crypto_aead_reqsize(aead);
	iv = aad + 24;

	/* Clear the QoS subtype bits, retry, power management and more data. Set protected */
	fc &= ~(0x0070 | IEEE80211_FCTL_RETRY | IEEE80211_FCTL_PM | IEEE80211_FCTL_MOREDATA);
	fc |= IEEE80211_FCTL_PROTECTED;
	put_unaligned_le16(fc, aad);
	memcpy(aad + 2, hdr + 4, 18);
	aad[20] = hdr[22] & 0x0f;           /* sequence control: fragment number only */
	aad[21] = 0;
	aad[22] = hdr[24] & 0x0f;           /* QoS control: TID only */
	aad[23] = 0;

	/* CCM IV: length field size minus one, then the 13-byte nonce */
	iv[0] = 1;
	iv[1] = hdr[24] & 0x0f;
	memcpy(iv + 2, hdr + 10, ETH_ALEN);
	for (i = 0; i < 6; i++)
		iv[8 + i] = pn >> (8 * (5 - i));

	sg_init_table(sg, 2);
	sg_set_buf(&sg[0], aad, 24);
	sg_set_buf(&sg[1], data, encrypt ? crypt_len : len);

	aead_request_set_tfm(req, aead);
	aead_request_set_callback(req, 0, NULL, NULL);
	aead_request_set_ad(req, 24);
	aead_request_set_crypt(req, sg, sg, encrypt ? len : len, iv);
	err = encrypt ? crypto_aead_encrypt(req) : crypto_aead_decrypt(req);
	kfree_sensitive(mem);
	return err;
}

static struct mtnet_client *mtnet_find(const u8 *addr)
{
	int i;

	for (i = 0; i < MTNET_MAX_CLIENTS; i++)
		if (mtnet.clients[i] && ether_addr_equal(mtnet.clients[i]->address, addr))
			return mtnet.clients[i];
	return NULL;
}

static int mtnet_get_buffer(struct gip_adapter *adap, struct gip_adapter_buffer *buf)
{
	struct sk_buff *skb = mtnet_alloc(MTNET_BUFFER_LEN);

	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, MTNET_QOS_HDR_LEN + MTNET_CCMP_HDR_LEN);
	buf->context = skb;
	buf->data = skb->data;
	buf->length = skb_tailroom(skb) - MTNET_CCMP_MIC_LEN;
	return 0;
}

static struct ieee80211_qos_hdr *mtnet_push_qos_hdr(struct sk_buff *skb,
						    struct mtnet_client *client, u16 fc)
{
	struct ieee80211_qos_hdr *hdr = skb_push(skb, MTNET_QOS_HDR_LEN);

	memset(hdr, 0, MTNET_QOS_HDR_LEN);
	hdr->frame_control = cpu_to_le16(fc);
	ether_addr_copy(hdr->addr1, client->address);
	ether_addr_copy(hdr->addr2, mtnet.ndev->dev_addr);
	ether_addr_copy(hdr->addr3, mtnet.ndev->dev_addr);
	hdr->seq_ctrl = mtnet_next_seq();
	return hdr;
}

static int mtnet_submit_buffer(struct gip_adapter *adap, struct gip_adapter_buffer *buf)
{
	struct mtnet_client *client = dev_get_drvdata(&adap->dev);
	struct sk_buff *skb = buf->context;
	unsigned int len = buf->length;
	struct ieee80211_qos_hdr *hdr;
	u16 fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA | IEEE80211_FCTL_FROMDS;

	skb_put(skb, len);

	if (client->encryption_enabled) {
		u8 *ccmp = skb_push(skb, MTNET_CCMP_HDR_LEN);

		u64 pn = ++client->tx_pn;
		int err;

		ccmp[0] = pn;
		ccmp[1] = pn >> 8;
		ccmp[2] = 0;
		ccmp[3] = 0x20;     /* extended IV, key 0 */
		ccmp[4] = pn >> 16;
		ccmp[5] = pn >> 24;
		ccmp[6] = pn >> 32;
		ccmp[7] = pn >> 40;
		fc |= IEEE80211_FCTL_PROTECTED;

		hdr = mtnet_push_qos_hdr(skb, client, fc);

		err = mtnet_ccmp(client->aead, (u8 *)hdr, pn,
				 (u8 *)hdr + MTNET_QOS_HDR_LEN + MTNET_CCMP_HDR_LEN, len, true);
		if (err) {
			kfree_skb(skb);
			return err;
		}
		skb_put(skb, MTNET_CCMP_MIC_LEN);
	} else {
		hdr = mtnet_push_qos_hdr(skb, client, fc);
	}

	mtnet_xmit(skb);
	return 0;
}

static int mtnet_set_encryption_key(struct gip_adapter *adap, u8 *key, int len)
{
	struct mtnet_client *client = dev_get_drvdata(&adap->dev);
	struct crypto_aead *aead, *old;
	unsigned long flags;
	int err;

	if (len != MTNET_KEY_LEN)
		return -EINVAL;
	aead = crypto_alloc_aead("ccm(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(aead))
		return PTR_ERR(aead);
	err = crypto_aead_setkey(aead, key, len);
	if (!err)
		err = crypto_aead_setauthsize(aead, MTNET_CCMP_MIC_LEN);
	if (err) {
		crypto_free_aead(aead);
		return err;
	}
	spin_lock_irqsave(&mtnet.lock, flags);
	old = client->aead;
	client->aead = aead;
	/* Starts where the hardware key setup starts its IV: packet number 1 */
	client->tx_pn = 1;
	client->rx_pn = 0;
	spin_unlock_irqrestore(&mtnet.lock, flags);
	if (old)
		crypto_free_aead(old);
	netdev_dbg(mtnet.ndev, "encryption key set for %pM\n", client->address);
	return 0;
}

static struct gip_adapter_ops mtnet_adapter_ops = {
	.get_buffer = mtnet_get_buffer,
	.submit_buffer = mtnet_submit_buffer,
	.set_encryption_key = mtnet_set_encryption_key,
};

static void mtnet_remove_client(int index)
{
	struct mtnet_client *client;
	unsigned long flags;

	spin_lock_irqsave(&mtnet.lock, flags);
	client = mtnet.clients[index];
	mtnet.clients[index] = NULL;
	spin_unlock_irqrestore(&mtnet.lock, flags);
	if (!client)
		return;
	netdev_dbg(mtnet.ndev, "client %pM removed\n", client->address);
	gip_destroy_adapter(client->adapter);
	if (client->aead)
		crypto_free_aead(client->aead);
	kfree_sensitive(client);
}

/* Tells a client to start over, so that both sides drop their session state */
static void mtnet_send_disassoc(const u8 *addr)
{
	__le16 reason = cpu_to_le16(MTNET_REASON_NOT_ASSOCIATED);

	mtnet_send_mgmt(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DISASSOC, addr, &reason,
			sizeof(reason));
}

static void mtnet_remove_address(const u8 *addr)
{
	unsigned long flags;
	int i;

	for (i = 0; i < MTNET_MAX_CLIENTS; i++) {
		bool match;

		spin_lock_irqsave(&mtnet.lock, flags);
		match = mtnet.clients[i] && ether_addr_equal(mtnet.clients[i]->address, addr);
		spin_unlock_irqrestore(&mtnet.lock, flags);
		if (match)
			mtnet_remove_client(i);
	}
}

static void mtnet_associate(const u8 *addr, u16 resp_stype)
{
	/* Capability 0, xone's status code and association ID, then 8 zero bytes */
	u8 body[14] = { 0x00, 0x00, 0x10, 0x01, 0x00, 0x0f };
	struct mtnet_client *client;
	unsigned long flags;
	int i, slot = -1;
	bool stale;

	/* A repeat request means the last answer was missed, so always answer */
	mtnet_send_mgmt(IEEE80211_FTYPE_MGMT | resp_stype, addr, body, sizeof(body));

	spin_lock_irqsave(&mtnet.lock, flags);
	client = mtnet_find(addr);
	if (client)
		client->last_rx = jiffies;
	stale = client && client->aead;
	spin_unlock_irqrestore(&mtnet.lock, flags);
	/* An association after key exchange means the client restarted its session */
	if (stale) {
		netdev_dbg(mtnet.ndev, "%pM restarted, resetting its session\n", addr);
		mtnet_remove_address(addr);
	} else if (client) {
		return;
	}

	for (i = 0; i < MTNET_MAX_CLIENTS && slot < 0; i++)
		if (!mtnet.clients[i])
			slot = i;
	if (slot < 0)
		return;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return;
	ether_addr_copy(client->address, addr);
	client->last_rx = jiffies;
	client->added = jiffies;
	client->adapter = gip_create_adapter(&mtnet.ndev->dev, &mtnet_adapter_ops, 1);
	if (IS_ERR(client->adapter)) {
		netdev_err(mtnet.ndev, "could not create adapter: %ld\n", PTR_ERR(client->adapter));
		kfree(client);
		return;
	}
	dev_set_drvdata(&client->adapter->dev, client);

	spin_lock_irqsave(&mtnet.lock, flags);
	mtnet.clients[slot] = client;
	spin_unlock_irqrestore(&mtnet.lock, flags);
	netdev_dbg(mtnet.ndev, "client %pM added\n", addr);
}

static void mtnet_handle_event(struct work_struct *work)
{
	struct mtnet_event *evt = container_of(work, struct mtnet_event, work);
	static const u8 pair_data[] = { MTNET_RESERVED_STYPE, MTNET_PAIR_RESP, 0x00, 0x45, 0x55,
					0x01, 0x0f, 0x8f, 0xff, 0x87, 0x1f };
	static const u8 enc_data[] = { MTNET_RESERVED_STYPE, MTNET_ENABLE_ENCRYPTION, 0x00, 0x00 };
	struct mtnet_client *client;
	unsigned long flags;

	switch (evt->stype) {
	case IEEE80211_STYPE_DISASSOC:
		netdev_dbg(mtnet.ndev, "disconnect from %pM\n", evt->address);
		mtnet_remove_address(evt->address);
		break;
	case MTNET_STALE_SESSION:
		netdev_dbg(mtnet.ndev, "%pM has a different session, asking it to reconnect\n",
			   evt->address);
		mtnet_remove_address(evt->address);
		mtnet_send_disassoc(evt->address);
		break;
	case IEEE80211_STYPE_ASSOC_REQ:
		mtnet_associate(evt->address, IEEE80211_STYPE_ASSOC_RESP);
		break;
	case IEEE80211_STYPE_REASSOC_REQ:
		mtnet_associate(evt->address, IEEE80211_STYPE_REASSOC_RESP);
		break;
	case MTNET_RESERVED_STYPE:
		if (evt->cmd == MTNET_PAIR_REQ) {
			netdev_dbg(mtnet.ndev, "pair request from %pM\n", evt->address);
			mtnet_send_mgmt(IEEE80211_FTYPE_MGMT | MTNET_RESERVED_STYPE, evt->address,
					pair_data, sizeof(pair_data));
			WRITE_ONCE(pairing, false);
		} else if (evt->cmd == MTNET_ENABLE_ENCRYPTION) {
			netdev_dbg(mtnet.ndev, "encryption request from %pM\n", evt->address);
			mtnet_send_mgmt(IEEE80211_FTYPE_MGMT | MTNET_RESERVED_STYPE, evt->address,
					enc_data, sizeof(enc_data));
			spin_lock_irqsave(&mtnet.lock, flags);
			client = mtnet_find(evt->address);
			if (client && client->aead)
				client->encryption_enabled = true;
			spin_unlock_irqrestore(&mtnet.lock, flags);
		} else {
			netdev_dbg(mtnet.ndev, "unhandled client command %#x from %pM\n",
				    evt->cmd, evt->address);
		}
		break;
	}
	kfree(evt);
}

static void mtnet_queue_event(u16 stype, const u8 *addr, u8 cmd)
{
	struct mtnet_event *evt = kzalloc(sizeof(*evt), GFP_ATOMIC);

	if (!evt)
		return;
	evt->stype = stype;
	evt->cmd = cmd;
	ether_addr_copy(evt->address, addr);
	INIT_WORK(&evt->work, mtnet_handle_event);
	queue_work(mtnet.wq, &evt->work);
}

static void mtnet_stale_session(struct mtnet_client *client)
{
	if (client->last_stale && time_before(jiffies, client->last_stale + HZ))
		return;
	client->last_stale = jiffies;
	mtnet_queue_event(MTNET_STALE_SESSION, client->address, 0);
}

static void mtnet_rx_data(struct sk_buff *skb, u8 *f, unsigned int len)
{
	u16 fc = get_unaligned_le16(f);
	const u8 *addr2 = f + 10;
	struct mtnet_client *client;
	unsigned long flags;
	u8 *payload = f + MTNET_QOS_HDR_LEN;
	unsigned int plen;
	bool reply = false;
	u16 seq;

	if (len < MTNET_QOS_HDR_LEN)
		return;
	plen = len - MTNET_QOS_HDR_LEN;

	spin_lock_irqsave(&mtnet.lock, flags);
	client = mtnet_find(addr2);
	if (!client) {
		/* Encrypted data from a client we don't know: it thinks it's still connected */
		if ((fc & IEEE80211_FCTL_PROTECTED) &&
		    time_after(jiffies, mtnet.last_unknown + HZ)) {
			mtnet.last_unknown = jiffies;
			mtnet_queue_event(MTNET_STALE_SESSION, addr2, 0);
		}
		goto out;
	}
	client->last_rx = jiffies;
	/* Answer while the controller is still awake */
	client->last_reply_ns = ktime_get_ns();
	reply = true;

	/* QoS Null frames only keep the link alive */
	if ((fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_QOS_DATA)
		goto out;

	/* Monitor interfaces don't filter retransmissions, and GIP can't handle repeats */
	seq = get_unaligned_le16(f + 22);
	if ((fc & IEEE80211_FCTL_RETRY) && seq == client->last_seq)
		goto out;
	client->last_seq = seq;

	if (fc & IEEE80211_FCTL_PROTECTED) {
		u64 pn;

		if (!client->aead) {
			mtnet_stale_session(client);
			goto out;
		}
		client->plain_frames = 0;
		if (plen < MTNET_CCMP_HDR_LEN + MTNET_CCMP_MIC_LEN)
			goto out;
		pn = (u64)payload[0] | (u64)payload[1] << 8 | (u64)payload[4] << 16 |
		     (u64)payload[5] << 24 | (u64)payload[6] << 32 | (u64)payload[7] << 40;
		/* Also drops retransmissions of a frame already received */
		if (pn <= client->rx_pn)
			goto out;
		payload += MTNET_CCMP_HDR_LEN;
		plen -= MTNET_CCMP_HDR_LEN;
		if (mtnet_ccmp(client->aead, f, pn, payload, plen, false))
			goto out;
		client->rx_pn = pn;
		plen -= MTNET_CCMP_MIC_LEN;
	} else if (client->encryption_enabled && ++client->plain_frames >= MTNET_MAX_PLAIN_FRAMES) {
		/* It dropped its key. A few can still be in flight as encryption starts. */
		mtnet_stale_session(client);
		goto out;
	}
	gip_process_buffer(client->adapter, payload, plen);
out:
	spin_unlock_irqrestore(&mtnet.lock, flags);
	if (reply)
		mtnet_send_null(addr2);
}

static int mtnet_rx(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt,
		    struct net_device *orig)
{
	unsigned int rt_len, len;
	u8 *f;
	u16 fc;

	if (!READ_ONCE(mtnet.running))
		goto drop;
	skb = skb_unshare(skb, GFP_ATOMIC);
	if (!skb)
		return NET_RX_DROP;
	/* Monitor frames arrive with the radiotap header at the MAC header */
	skb_push(skb, skb->data - skb_mac_header(skb));
	if (skb_linearize(skb) || skb->len < sizeof(struct ieee80211_radiotap_header))
		goto drop;
	rt_len = get_unaligned_le16(skb->data + 2);
	if (skb->len < rt_len + 24)
		goto drop;
	f = skb->data + rt_len;
	len = skb->len - rt_len;
	/* Frames with a bad checksum are also delivered to monitors, but flagged */
	if (skb->data[4] & BIT(IEEE80211_RADIOTAP_FLAGS)) {
		int off = 8;
		u32 present = get_unaligned_le32(skb->data + 4);

		while (present & BIT(IEEE80211_RADIOTAP_EXT)) {
			present = get_unaligned_le32(skb->data + off);
			off += 4;
		}
		if (skb->data[4] & BIT(IEEE80211_RADIOTAP_TSFT))
			off = ALIGN(off, 8) + 8;
		if (off < rt_len && (skb->data[off] & IEEE80211_RADIOTAP_F_BADFCS))
			goto drop;
		if (off < rt_len && (skb->data[off] & IEEE80211_RADIOTAP_F_FCS)) {
			if (len < 28)
				goto drop;
			len -= 4;
		}
	}
	if (!ether_addr_equal(f + 4, dev->dev_addr) || ether_addr_equal(f + 10, dev->dev_addr))
		goto drop;

	fc = get_unaligned_le16(f);
	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
		mtnet_rx_data(skb, f, len);
		break;
	case IEEE80211_FTYPE_MGMT:
		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_ASSOC_REQ:
		case IEEE80211_STYPE_REASSOC_REQ:
		case IEEE80211_STYPE_DISASSOC:
			if (is_valid_ether_addr(f + 10))
				mtnet_queue_event(fc & IEEE80211_FCTL_STYPE, f + 10, 0);
			break;
		case MTNET_RESERVED_STYPE:
			if (len >= 26 && f[24] == MTNET_RESERVED_STYPE)
				mtnet_queue_event(MTNET_RESERVED_STYPE, f + 10, f[25]);
			break;
		}
		break;
	}
drop:
	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

static int mtnet_reg_read(u32 addr, u32 *val)
{
	int ret = usb_control_msg(mtnet.udev, usb_rcvctrlpipe(mtnet.udev, 0), MTNET_VEND_MULTI_READ,
				  USB_DIR_IN | USB_TYPE_VENDOR, addr >> 16, addr & 0xffff,
				  mtnet.regbuf, sizeof(*mtnet.regbuf), MTNET_VEND_TIMEOUT_MS);

	if (ret != sizeof(*mtnet.regbuf))
		return ret < 0 ? ret : -EIO;
	*val = le32_to_cpu(*mtnet.regbuf);
	return 0;
}

static int mtnet_reg_write(u32 addr, u32 val)
{
	int ret;

	*mtnet.regbuf = cpu_to_le32(val);
	ret = usb_control_msg(mtnet.udev, usb_sndctrlpipe(mtnet.udev, 0), MTNET_VEND_MULTI_WRITE,
			      USB_DIR_OUT | USB_TYPE_VENDOR, addr >> 16, addr & 0xffff,
			      mtnet.regbuf, sizeof(*mtnet.regbuf), MTNET_VEND_TIMEOUT_MS);
	if (ret != sizeof(*mtnet.regbuf))
		return ret < 0 ? ret : -EIO;
	return 0;
}

static int mtnet_reg_rmw(u32 addr, u32 mask, u32 val)
{
	u32 old;
	int err = mtnet_reg_read(addr, &old);

	if (err)
		return err;
	return mtnet_reg_write(addr, (old & ~mask) | val);
}

static void mtnet_set_single_chain(void)
{
	u32 val;

	/* mt76x2u can restore these when it changes channel, so this runs repeatedly */
	if (mtnet_reg_read(MTNET_REG_TX_PIN_CFG, &val) ||
	    (val & MTNET_TX_PIN_TXANT) == 0x3)
		return;

	netdev_dbg(mtnet.ndev, "selecting one radio chain\n");
	if (mtnet_reg_rmw(MTNET_REG_BBP_IBI9, BIT(11), 0) ||
	    mtnet_reg_rmw(MTNET_REG_BBP_TXBE5, GENMASK(1, 0), 0) ||
	    mtnet_reg_rmw(MTNET_REG_BBP_CORE32, GENMASK(21, 20), 2 << 20) ||
	    mtnet_reg_rmw(MTNET_REG_BBP_CORE33, GENMASK(12, 9), 4 << 9) ||
	    mtnet_reg_rmw(MTNET_REG_BBP_AGC0, BIT(4) | BIT(3) | BIT(1) | BIT(0), 0))
		return;
	mtnet_reg_rmw(MTNET_REG_TX_PIN_CFG, MTNET_TX_PIN_TXANT, 0x3);
}

static void mtnet_set_led(enum mtnet_led led)
{
	u32 status;

	if (led == mtnet.led)
		return;
	switch (led) {
	case MTNET_LED_ON:
		status = MTNET_LED_STATUS(0x00, 0xff);
		break;
	case MTNET_LED_BLINK:
		status = MTNET_LED_STATUS(0x10, 0x10);
		break;
	default:
		status = MTNET_LED_STATUS(0xff, 0x00);
		break;
	}
	if (mtnet_reg_write(MTNET_REG_LED_S0, status) ||
	    mtnet_reg_write(MTNET_REG_LED_S1, status) ||
	    mtnet_reg_write(MTNET_REG_LED_CTRL, MTNET_LED_CTRL_REPLAY | MTNET_LED_CTRL_KICK))
		return;
	mtnet.led = led;
}

/* Polled, because this firmware sends no event for the button */
static void mtnet_button(struct work_struct *work)
{
	enum mtnet_led led = MTNET_LED_OFF;
	unsigned long flags;
	u32 val;
	int i;

	if (!mtnet_reg_read(MTNET_REG_BUTTON, &val)) {
		bool down = !(val & MTNET_BUTTON_RELEASED);

		if (down && !mtnet.button_down) {
			netdev_dbg(mtnet.ndev, "button pressed\n");
			mtnet.pairing_since = 0;
			WRITE_ONCE(pairing, true);
		}
		mtnet.button_down = down;
	}

	if (READ_ONCE(pairing)) {
		led = MTNET_LED_BLINK;
	} else {
		spin_lock_irqsave(&mtnet.lock, flags);
		for (i = 0; i < MTNET_MAX_CLIENTS; i++)
			if (mtnet.clients[i])
				led = MTNET_LED_ON;
		spin_unlock_irqrestore(&mtnet.lock, flags);
	}
	mtnet_set_led(led);
	mtnet_set_single_chain();

	queue_delayed_work(mtnet.wq, &mtnet.button_work, msecs_to_jiffies(MTNET_BUTTON_MS));
}

/* Replaces the lost-client event Microsoft's firmware sends */
static void mtnet_timeout(struct work_struct *work)
{
	unsigned long flags;
	int i;

	/* Nothing else ends pairing mode if no controller asks to pair */
	if (!READ_ONCE(pairing))
		mtnet.pairing_since = 0;
	else if (!mtnet.pairing_since)
		mtnet.pairing_since = jiffies;
	else if (time_after(jiffies, mtnet.pairing_since +
			    msecs_to_jiffies(MTNET_PAIRING_TIMEOUT_MS)))
		WRITE_ONCE(pairing, false);

	for (i = 0; i < MTNET_MAX_CLIENTS; i++) {
		u8 addr[ETH_ALEN];
		bool expired, stalled = false;

		spin_lock_irqsave(&mtnet.lock, flags);
		expired = mtnet.clients[i] &&
			  time_after(jiffies, mtnet.clients[i]->last_rx +
					      msecs_to_jiffies(MTNET_CLIENT_TIMEOUT_MS));
		if (mtnet.clients[i] && !expired) {
			struct mtnet_client *c = mtnet.clients[i];
			struct gip_client *gc = c->adapter->clients[0];

			/* A lost setup packet is never resent, so start the client over */
			stalled = time_after(jiffies, c->added +
						      msecs_to_jiffies(MTNET_SETUP_TIMEOUT_MS)) &&
				  (!gc || !device_is_registered(&gc->dev));
			if (stalled)
				ether_addr_copy(addr, c->address);
		}
		spin_unlock_irqrestore(&mtnet.lock, flags);
		if (expired)
			mtnet_remove_client(i);
		if (stalled) {
			netdev_dbg(mtnet.ndev, "%pM stalled during setup, asking it to reconnect\n", addr);
			mtnet_remove_client(i);
			mtnet_send_disassoc(addr);
		}
	}
	queue_delayed_work(mtnet.wq, &mtnet.timeout_work, HZ);
}

static const struct usb_device_id mtnet_id_table[] = {
	{ USB_DEVICE(0x045e, 0x02e6) }, /* old dongle */
	{ USB_DEVICE(0x045e, 0x02fe) }, /* new dongle */
	{ },
};
/* Loads this module alongside mt76x2u. It doesn't bind to the device itself. */
MODULE_DEVICE_TABLE(usb, mtnet_id_table);

static bool mtnet_is_dongle_monitor(struct net_device *dev)
{
	struct device *parent = dev->dev.parent;

	if (dev->type != ARPHRD_IEEE80211_RADIOTAP || !parent || !parent->driver ||
	    strcmp(parent->driver->name, "mt76x2u"))
		return false;
	return usb_match_id(to_usb_interface(parent), mtnet_id_table);
}

static void mtnet_start(struct net_device *dev)
{
	if (READ_ONCE(mtnet.running))
		return;
	dev_hold(dev);
	mtnet.ndev = dev;
	mtnet.udev = usb_get_dev(interface_to_usbdev(to_usb_interface(dev->dev.parent)));
	mtnet.led = MTNET_LED_UNSET;
	mtnet.button_down = false;
	mtnet.ptype.dev = dev;
	WRITE_ONCE(mtnet.running, true);
	dev_add_pack(&mtnet.ptype);
	/* Start on a slot boundary, so slot times are whole multiples of 100 TU */
	hrtimer_start(&mtnet.beacon_timer,
		      ns_to_ktime((div64_u64(ktime_get_ns(), MTNET_BEACON_NS) + 1) * MTNET_BEACON_NS),
		      HRTIMER_MODE_ABS_SOFT);
	queue_delayed_work(mtnet.wq, &mtnet.timeout_work, HZ);
	queue_delayed_work(mtnet.txwq, &mtnet.keepalive_work, msecs_to_jiffies(MTNET_KEEPALIVE_MS));
	queue_delayed_work(mtnet.wq, &mtnet.button_work, 0);
	netdev_info(dev, "Xbox wireless transport on channel %d\n", channel);
}

static void mtnet_stop(void)
{
	int i;

	if (!READ_ONCE(mtnet.running))
		return;
	WRITE_ONCE(mtnet.running, false);
	dev_remove_pack(&mtnet.ptype);
	for (i = 0; i < MTNET_MAX_CLIENTS; i++)
		if (mtnet.clients[i])
			mtnet_send_disassoc(mtnet.clients[i]->address);
	hrtimer_cancel(&mtnet.beacon_timer);
	cancel_delayed_work_sync(&mtnet.timeout_work);
	cancel_delayed_work_sync(&mtnet.keepalive_work);
	cancel_delayed_work_sync(&mtnet.button_work);
	flush_workqueue(mtnet.wq);
	for (i = 0; i < MTNET_MAX_CLIENTS; i++)
		mtnet_remove_client(i);
	mtnet_set_led(MTNET_LED_OFF);
	usb_put_dev(mtnet.udev);
	mtnet.udev = NULL;
	dev_put(mtnet.ndev);
	mtnet.ndev = NULL;
}

static int mtnet_notifier(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (event == NETDEV_UP && mtnet_is_dongle_monitor(dev))
		mtnet_start(dev);
	else if (event == NETDEV_DOWN && dev == mtnet.ndev)
		mtnet_stop();
	return NOTIFY_DONE;
}

static struct notifier_block mtnet_nb = { .notifier_call = mtnet_notifier };

static int __init mtnet_init(void)
{
	int err;

	mtnet.regbuf = kmalloc(sizeof(*mtnet.regbuf), GFP_KERNEL);
	if (!mtnet.regbuf)
		return -ENOMEM;
	mtnet.wq = alloc_ordered_workqueue("xone_mtnet", 0);
	mtnet.txwq = alloc_ordered_workqueue("xone_mtnet_tx", WQ_HIGHPRI);
	if (!mtnet.wq || !mtnet.txwq) {
		if (mtnet.wq)
			destroy_workqueue(mtnet.wq);
		if (mtnet.txwq)
			destroy_workqueue(mtnet.txwq);
		kfree(mtnet.regbuf);
		return -ENOMEM;
	}
	spin_lock_init(&mtnet.lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
	hrtimer_init(&mtnet.beacon_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_SOFT);
	mtnet.beacon_timer.function = mtnet_beacon;
#else
	hrtimer_setup(&mtnet.beacon_timer, mtnet_beacon, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_SOFT);
#endif
	INIT_DELAYED_WORK(&mtnet.timeout_work, mtnet_timeout);
	INIT_DELAYED_WORK(&mtnet.keepalive_work, mtnet_keepalive);
	INIT_DELAYED_WORK(&mtnet.button_work, mtnet_button);
	mtnet.ptype.type = htons(ETH_P_ALL);
	mtnet.ptype.func = mtnet_rx;
	/* Also reports interfaces that are already up */
	err = register_netdevice_notifier(&mtnet_nb);
	if (err) {
		destroy_workqueue(mtnet.txwq);
		destroy_workqueue(mtnet.wq);
		kfree(mtnet.regbuf);
	}
	return err;
}

static void __exit mtnet_exit(void)
{
	/* Reports every interface as going down, which stops the transport */
	unregister_netdevice_notifier(&mtnet_nb);
	destroy_workqueue(mtnet.txwq);
	destroy_workqueue(mtnet.wq);
	kfree(mtnet.regbuf);
}

module_init(mtnet_init);
module_exit(mtnet_exit);

MODULE_DESCRIPTION("xone transport for the Xbox wireless dongle on mt76x2u");
MODULE_LICENSE("GPL");
