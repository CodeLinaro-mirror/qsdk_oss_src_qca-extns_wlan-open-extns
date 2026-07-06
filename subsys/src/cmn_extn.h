/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef CMN_EXTN_H
#define CMN_EXTN_H

#include <net/mac80211.h>
#include "../../../include/net/cfg80211.h"
#include "dfs_extn.h"

struct ieee802_11_elems;
enum ieee80211_conn_mode;
struct ieee80211_conn_settings;
struct sk_buff;
struct ieee80211_sub_if_data;
struct ieee80211_vif;
struct ieee80211_local;
struct link_sta_info;
struct ieee80211_supported_band;
struct ieee80211_link_sta;
struct ieee80211_rx_data;
struct ieee80211_rx_status;
struct wiphy;
typedef unsigned int __bitwise ieee80211_rx_result;
struct wireless_dev;
struct net_device;
struct sta_info;
struct ieee80211_bss_conf;
struct ieee80211_hw;
struct ieee80211_sta;
extern unsigned int mmeshsim;
struct notifier_block;

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
#define MESH_CAPS_VER1 0x8000
#define MESH_CAPS_BW_OFFSET 0
#define MESH_CAPS_NSS_OFFSET 4
#define MESH_CAPS_MODE_OFFSET 8
#define MESH_CAPS_NIBBLE_MASK 0xF
#define MESH_CAPS_MAX_NSS 8
#define MESH_CAPS_SHORT_SLOT 0x1000
#define MESH_CAPS_SHORT_PREAMBLE 0x2000
/* bit 14: when set, driver skips auto key-install — consumer manages key installation */
#define MESH_CAPS_SKIP_KEY_INSTALL 0x4000

/* Key indices for managed mesh.  PTK (pairwise) always at index 0 per
 * 802.11 standard.  GTK (group/broadcast) at index 1. */
#define MANAGED_MESH_PTK_KEY_IDX	0
#define MANAGED_MESH_GTK_KEY_IDX	1

/* mac80211 set_key accepts link_id = -1 only for pairwise keys (PTK is per-STA). */
#define MANAGED_MESH_SET_KEY_DEFAULT_LINK_ID	(-1)

#define MESH_PEER_MAX_LINKS 3

#define MESH_PEER_TIMEOUT_CNT 3			/* Peer inactivity timeout threshold */
#define MMESH_TX_META_HDR 16
#define MESH_PEER_INACTIVITY_PERIOD 20000	/* Peer inactivity timer in milliseconds */

#define MESH_AID_WORDS ((2008 + 31) / 32)  /* 63 words for 2008 AIDs */
#define MESH_RSN_IE_MAX_LEN	257		/* tag(1) + length(1) + body(up to 255) */

/* RSN Capabilities field bits (802.11-2020 Table 9-151, little-endian 16-bit) */
#define RSN_CAP_MFP_CAPABLE	BIT(7)
#define RSN_CAP_MFP_REQUIRED	BIT(6)

/* RSN IE field sizes (802.11-2020 Table 9-149) */
#define RSN_VERSION_LEN		2  /* RSN Version field */
#define RSN_SUITE_LEN		4  /* cipher/AKM suite: 3-byte OUI + 1-byte type */
#define RSN_SUITE_COUNT_LEN	2  /* pairwise/AKM suite count field */
#define RSN_CAP_LEN		2  /* RSN Capabilities field */

/**
 * enum ath12k_extn_vap_submode: VAP submode types for QCA WLAN vendor
 *
 * @IEEE80211_EXTN_VAP_SUBMODE_NONE: Default mode, no special submode applied.
 * @IEEE80211_EXTN_VAP_SUBMODE_MESH: VAP operates in mesh mode for mesh networking.
 * @IEEE80211_EXTN_VAP_SUBMODE_SCAN: VAP operates in scan mode, typically for offchannel scan.
 */
enum ieee80211_extn_vap_submode {
	IEEE80211_EXTN_VAP_SUBMODE_NONE = 0,
	IEEE80211_EXTN_VAP_SUBMODE_MESH = 1,
	IEEE80211_EXTN_VAP_SUBMODE_SCAN = 2,
};

/**
 * enum mesh_peer_preamble_type: Preamble type for mesh peer config
 *
 * @MESH_PREAMBLE_OFDM: OFDM preamble (802.11a)
 * @MESH_PREAMBLE_CCK: CCK preamble (802.11b)
 * @MESH_PREAMBLE_HT: HT preamble (802.11n)
 * @MESH_PREAMBLE_VHT: VHT preamble (802.11ac)
 * @MESH_PREAMBLE_HE: HE preaamble (802.11ax)
 * @MESH_PREAMBLE_EHT: EHT preamble (802.11be)
 */
enum mesh_peer_preamble_type {
	MESH_PREAMBLE_OFDM,
	MESH_PREAMBLE_CCK,
	MESH_PREAMBLE_HT,
	MESH_PREAMBLE_VHT,
	MESH_PREAMBLE_HE,
	MESH_PREAMBLE_EHT,
	MESH_PREAMBLE_MAX,
};

/**
 * enum mesh_peer_bw: Operating BW for mesh peer
 *
 * @MESH_BW_20: 20MHz operating BW
 * @MESH_BW_40: 40MHz operating BW
 * @MESH_BW_80: 80MHz operating BW
 * @MESH_BW_80_80: 80+80MHz operating BW
 * @MESH_BW_160: 160MHz operating BW
 * @MESH_BW_320: 320MHz operating BW
 */
enum mesh_peer_bw {
	MESH_BW_20,
	MESH_BW_40,
	MESH_BW_80,
	MESH_BW_80_80,
	MESH_BW_160,
	MESH_BW_320,
	MESH_BW_MAX,
};

/**
 * struct mesh_rsn_conf - RSN/WPA security policy for the managed mesh BSS.
 * Saved during ieee80211_mesh_ap_config_save() and used at beacon intersection
 * to gate peers based on their RSN IE. Fields are mode-agnostic (HT/VHT/HE/EHT).
 */
struct mesh_rsn_conf {
	u32 wpa_versions;  /* NL80211_WPA_VERSION_* bitmask; 0 = open */
	u8 *rsn_ie;        /* copy of local AP's RSN IE (tag+len+body); NULL if open */
	u16 rsn_ie_len;    /* total IE length: tag(1) + len(1) + body(up to 255) */
	u16 rsn_caps;      /* RSN Capabilities field, pre-parsed at AP setup time */
};

struct mesh_bss_conf {
	struct ieee80211_ht_cap *ht_cap;
	struct ieee80211_vht_cap *vht_cap;
	struct ieee80211_he_cap_elem *he_cap;
	struct ieee80211_he_6ghz_capa *he_6ghz_capa;
	struct ieee80211_eht_cap_elem *eht_cap;
	u8 eht_capa_len;
	u16 eml_cap;
	struct mesh_rsn_conf *rsn_conf;
};

/**
 * struct mesh_peer_info - Mesh peer beacon intersection state
 * @is_mesh_peer: True if this station is a configured mesh peer
 * @mesh_peer_phymode: Peer phymode
 * @mesh_peer_nss: Peer nss
 * @beacon_ie_checksum: CRC32 checksum of critical IEs from last beacon
 * @beacon_miss_count: Number of consecutive missed beacons
 * @last_beacon_time: Timestamp of last received beacon (jiffies)
 * @intersection_done: True if initial intersection completed successfully
 * @intersect_attempts: Total number of intersection attempts
 * @intersect_successes: Number of successful intersections
 * @intersect_failures: Number of failed intersections
 * @parse_beacon_install_key: Runtime copy of the flag from ieee80211_mesh_config_params_extn;
 *   when set, beacon intersection installs the VAP key on this peer
 * @key_installed: True after per-peer key has been successfully installed
 * @peer_wpa_versions: NL80211_WPA_VERSION_* bitmask derived from peer RSN IE
 * @peer_ucast_cipher: WLAN_CIPHER_SUITE_* selected pairwise cipher from peer RSN IE
 * @peer_mcast_cipher: WLAN_CIPHER_SUITE_* group cipher from peer RSN IE
 * @peer_akm_suite: WLAN_AKM_SUITE_* from peer RSN IE
 * @peer_mfp_capable: Peer RSN caps MFP enabled bit
 * @peer_mfp_required: Peer RSN caps MFP required bit
 * @peer_rsn_ie: Raw copy of peer RSN IE bytes
 * @peer_rsn_ie_len: Length of peer_rsn_ie (0 if open)
 * @peer_ht_restricted: TKIP pairwise cipher detected with HT-capable peer;
 *   HT caps are suppressed at beacon intersection per 802.11-2020 §10.3.3
 *   to fall back to legacy rates; the peer is not rejected.
 * @peer_rejected: Set on first RSN mismatch to suppress repeated deauth frames
 *   and timeout_wk re-queues on subsequent beacons from the same peer.
 */
struct mesh_peer_info {
	bool is_mesh_peer;
	u32 mesh_peer_phymode;
	u32 mesh_peer_nss;
	u32 beacon_ie_checksum;
	u8 beacon_miss_count;
	unsigned long last_beacon_time;
	bool intersection_done;
	/* Statistics */
	u32 intersect_attempts;
	u32 intersect_successes;
	u32 intersect_failures;
	/* Security fields — parse_beacon_install_key is copied from
	 * ieee80211_mesh_config_params_extn at add_local_peer time.
	 * Default: driver auto-installs the VAP key and authorizes
	 * the peer after successful beacon intersection.
	 * When true (MESH_CAPS_SKIP_KEY_INSTALL set): consumer manages key installation
	 * and peer authorization; driver skips both. */
	bool parse_beacon_install_key;
	bool key_installed;
	/* Crypto params extracted from peer RSN IE at beacon intersection */
	u32 peer_wpa_versions;
	u32 peer_ucast_cipher;
	u32 peer_mcast_cipher;
	u32 peer_akm_suite;
	bool peer_mfp_capable;
	bool peer_mfp_required;
	u8 peer_rsn_ie[MESH_RSN_IE_MAX_LEN]; /* raw RSN IE: tag(1) + len(1) + body(up to 255) */
	u16 peer_rsn_ie_len;  /* total stored length (0 if open) */
	/* PHY mode cipher restrictions */
	bool peer_ht_restricted;  /* TKIP+HT: HT caps suppressed, legacy rates only */
	bool peer_rejected;       /* RSN mismatch: suppress repeated deauth on subsequent beacons */
};

struct ieee80211_mesh_peer_info {
	u8 vap_mac[ETH_ALEN];
	u8 peer_link_mac[ETH_ALEN];
	u8 link_id;
	u64 caps;
	u16 puncture_bitmaps;
} __packed;

struct ieee80211_mesh_config_params_extn {
	u8 cmd_type;
	u16 num_links;
	u8 peer_mld_mac[ETH_ALEN];
	struct ieee80211_mesh_peer_info peers[MESH_PEER_MAX_LINKS];
	u16 mesh_peer_timeout_cnt; /* Configurable peer inactivity timeout threshold */
	bool peer_cleanup_timer_enable;
} __packed;
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */

struct bw240MHz_parameters_extn {
	struct ieee80211_240mhz_vendor_oper_extn *eht_240mhz_cap;
	u8 eht_240mhz_len;
};

struct link_station_parameters_extn {
	struct bw240MHz_parameters_extn params_240MHz;
};

struct ieee802_11_elems_extn {
	struct bw240MHz_parameters_extn params_240MHz;
};

struct ieee80211_bss_extn {
	struct bw240MHz_parameters_extn params_240MHz;
};

struct ieee80211_bss_conf_extn {
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	struct mesh_bss_conf mesh_bss_caps;
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */
};

struct ieee80211_if_ap_extn {
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	struct timer_list mesh_peer_cleanup_timer;
	u16 num_mesh_peers;
	u32 mesh_peer_aid[MESH_AID_WORDS];  /* Bitmap for AID allocation */
	u16 mesh_peer_timeout_cnt; /* Configurable peer inactivity timeout threshold */
	bool mesh_peer_cleanup_timer_enabled; /* Runtime enable/disable of cleanup timer */
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */
};

struct ieee80211_sta_extn {
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	struct mesh_peer_info peer_info;
	struct wiphy_work update_cap_wk; /* capability intersection + key installation */
	struct wiphy_work timeout_wk;
	struct wiphy_work deauth_rx_wk;  /* deferred cleanup on RX deauth/disassoc */
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */
};

struct ieee80211_240mhz_vendor_oper_extn {
	u8 ccfs1;
	u8 ccfs0;
	u16 punctured;
	u16 is5ghz240mhz          :1,
	    bfmess320mhz          :3,
	    numsound320mhz        :3,
	    nonofdmaulmumimo320mhz:1,
	    mubfmr320mhz          :1;
	u8 mcs_map_320mhz[3];
}__packed;

#ifndef CPTCFG_QCN_EXTN

static inline u8 *
ieee80211_add_qcn_vendor_ie_extn(struct sk_buff *skb,
				 struct ieee80211_bss_extn *bss_extn,
				 int  mode)
{
	return NULL;
}

static inline void
ieee80211_240mhz_cap_to_eht_cap_extn(struct ieee80211_sub_if_data *sdata,
				     struct link_sta_info *link_sta,
				     struct ieee80211_supported_band *sband,
				     const struct
				     ieee80211_240mhz_vendor_oper_extn
				     *eht_240mhz_cap)
{
	return;
}

static inline void
ieee80211_inform_bss_extn(struct cfg80211_bss *cbss,
			  struct ieee802_11_elems *elems)
{
	return;
}

static inline int
ieee802_11_parse_elems_vendor_qcn_extn(const u8 *pos, u8 elen,
				       struct ieee802_11_elems *elems)
{
	return -1;
}

static inline int
ieee802_11_determine_ap_chan_extn(const struct ieee802_11_elems *elems,
				  struct cfg80211_chan_def *chandef,
				  struct ieee80211_sub_if_data *sdata)
{
	/* If this function returns -1, then mode is forced to be
	 * IEEE80211_CONN_MODE_HE. Hence, return 0 to not alter the
	 * AP mode.
	 */
	return 0;
}

static inline size_t
ieee802_11_qcn_ie_len_extn(struct cfg80211_bss *cbss,
			   struct ieee80211_local *local)
{
	return 0;
}

static inline struct ieee80211_240mhz_vendor_oper_extn*
drv_get_240mhz_cap_extn(struct ieee80211_local *local,
			struct ieee80211_vif *vif,
			struct ieee80211_link_sta *link_sta)
{
	return NULL;
}

static inline int
ieee80211_validate_6ghz_chandef_extn(struct ieee80211_local *local,
				     struct ieee80211_vif *vif,
				     unsigned int link_id,
				     const struct cfg80211_chan_def *chandef,
				     enum ieee80211_ap_reg_power power_type)
{
	return 0;
}

static inline void
ieee80211_bss_240mhz_to_sta_eht_cap_extn(struct ieee80211_sub_if_data *sdata,
					 struct link_sta_info *link_sta,
					 struct ieee80211_supported_band *sband,
					 struct cfg80211_bss *cbss)
{
	return;
}

static inline void
ieee80211_update_eht_cap_from_240mhz_nl(struct ieee80211_sub_if_data *sdata,
					struct link_sta_info *link_sta,
					struct ieee80211_supported_band *sband)
{
	return;
}

static inline void
ieee80211_modify_bw_limit_for_240mhz(bool is_5ghz,
				     struct ieee80211_conn_settings *conn)
{
	return;
}

static inline void
ieee80211_scan_radio_do_open_extn(struct ieee80211_sub_if_data *sdata,
				  struct wireless_dev *wdev,
				  struct net_device *dev,
				  u32 *hw_reconf_flags)
{
	return;
}

static inline void
ieee80211_scan_radio_do_stop_extn(struct ieee80211_sub_if_data *sdata,
				  u32 *hw_reconf_flags)
{
	return;
}

static inline void
ieee80211_set_allow_scan_on_dfs_chan_extn(struct ieee80211_local *local, bool enable)
{
	return;
}

#else

u8 *
ieee80211_add_qcn_vendor_ie_extn(struct sk_buff *skb,
				 struct ieee80211_bss_extn *bss_extn,
				 enum ieee80211_conn_mode mode);

void
ieee80211_240mhz_cap_to_eht_cap_extn(struct ieee80211_sub_if_data *sdata,
				     struct link_sta_info *link_sta,
				     struct ieee80211_supported_band *sband,
				     const struct
				     ieee80211_240mhz_vendor_oper_extn
				     *eht_240mhz_cap);

void
ieee80211_inform_bss_extn(struct cfg80211_bss *cbss,
			  struct ieee802_11_elems *elems);

int ieee802_11_parse_elems_vendor_qcn_extn(const u8 *pos, u8 elen,
					   struct ieee802_11_elems *elems);

int
ieee802_11_determine_ap_chan_extn(const struct ieee802_11_elems *elems,
				  struct cfg80211_chan_def *chandef,
				  struct ieee80211_sub_if_data *sdata);

size_t
ieee802_11_qcn_ie_len_extn(struct cfg80211_bss *cbss,
			   struct ieee80211_local *local);

struct ieee80211_240mhz_vendor_oper_extn*
drv_get_240mhz_cap_extn(struct ieee80211_local *local,
			struct ieee80211_vif *vif,
			struct ieee80211_link_sta *link_sta);

int ieee80211_validate_6ghz_chandef_extn(struct ieee80211_local *local,
					 struct ieee80211_vif *vif,
					 unsigned int link_id,
					 const struct cfg80211_chan_def *chandef,
					 enum ieee80211_ap_reg_power power_type);

void
ieee80211_bss_240mhz_to_sta_eht_cap_extn(struct ieee80211_sub_if_data *sdata,
					 struct link_sta_info *link_sta,
					 struct ieee80211_supported_band *sband,
					 struct cfg80211_bss *cbss);

void
ieee80211_update_eht_cap_from_240mhz_nl(struct ieee80211_sub_if_data *sdata,
					struct link_sta_info *link_sta,
					struct ieee80211_supported_band *sband);

void ieee80211_modify_bw_limit_for_240mhz(bool is_5ghz,
					  struct ieee80211_conn_settings *conn);

void ieee80211_scan_radio_do_open_extn(struct ieee80211_sub_if_data *sdata,
				       struct wireless_dev *wdev,
				       struct net_device *dev,
				       u32 *hw_reconf_flags);

void ieee80211_scan_radio_do_stop_extn(struct ieee80211_sub_if_data *sdata,
				       u32 *hw_reconf_flags);

void ieee80211_set_allow_scan_on_dfs_chan_extn(struct ieee80211_local *local, bool enable);

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
int  ieee80211_is_mesh_peer(struct ieee80211_sta *sta);
void ieee80211_mark_mmesh_frame(struct ieee80211_rx_status *status);
int
ieee80211_mesh_ap_config_save(struct cfg80211_ap_settings *params,
			      struct ieee80211_bss_conf *link_conf);

int
ieee80211_vendor_mesh_add_local_peer(struct wiphy *wiphy,
				     struct ieee80211_vif *vif,
				     struct ieee80211_mesh_config_params_extn *mesh_params);

int
ieee80211_vendor_mesh_authorize_peer(struct wiphy *wiphy,
				     struct ieee80211_vif *vif,
				     const u8 *peer_addr);

int
ieee80211_vendor_mesh_delete_local_peer(struct wiphy *wiphy,
					struct ieee80211_vif *vif,
					const u8 *peer_addr);

int
ieee80211_vendor_mesh_set_peer_timeout_cnt(struct wiphy *wiphy,
					   struct ieee80211_vif *vif,
					   u16 timeout_cnt);

void
ieee80211_free_mesh_vap_caps(struct ieee80211_bss_conf *bss_conf);

void
ieee80211_mesh_ap_setup(struct wireless_dev *wdev,
			struct ieee80211_sub_if_data *sdata,
			struct cfg80211_ap_settings *params,
			struct ieee80211_bss_conf *link_conf);

void
ieee80211_mesh_ap_cleanup(struct wireless_dev *wdev,
			  struct ieee80211_sub_if_data *sdata,
			  struct ieee80211_bss_conf *link_conf);

int
drv_mesh_peer_update_caps_extn(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       struct sta_info *sta,
			       struct link_sta_info *link_sta);

bool ieee80211_skip_pn_check(struct ieee80211_rx_data *rx);
/* Mesh beacon intersection functions */
ieee80211_rx_result
ieee80211_process_mesh_peer_beacon(struct ieee80211_rx_data *rx);

ieee80211_rx_result
ieee80211_process_mesh_peer_deauth_disassoc(struct ieee80211_rx_data *rx);

void
ieee80211_mesh_init_peer_work(struct ieee80211_sub_if_data *sdata,
			      struct sta_info *sta_info);

void
ieee80211_mesh_peer_cleanup(struct wiphy *wiphy,
			    struct ieee80211_sub_if_data *sdata,
			    struct sta_info *sta_info);

void
ieee80211_mesh_peer_timeout_check(struct timer_list *t);

int
ieee80211_vendor_mesh_peer_timeout_en(struct wiphy *wiphy, struct ieee80211_vif *vif,
				      bool enable);

void
ieee80211_update_mesh_peer_caps_work(struct wiphy *wiphy, struct wiphy_work *work);

void
ieee80211_timeout_mesh_peer_work(struct wiphy *wiphy, struct wiphy_work *work);

void
ieee80211_mesh_peer_key_install_work(struct wiphy *wiphy, struct wiphy_work *work);

int ieee80211_extn_get_key_material(struct ieee80211_vif *vif,
				    int link_id,
				    const u8 *peer_mac,
				    bool pairwise,
				    u8 key_idx,
				    u8 *out_key, u8 out_key_max,
				    u8 *out_key_len);

/**
 * struct ieee80211_vendor_mesh_ops - vendor mesh operation function pointers
 *
 * This structure contains function pointers for vendor mesh operations
 * that can be used by external modules to manage mesh peers.
 *
 * @add_local_peer: Add a local mesh peer with specified configuration
 * @authorize_peer: Authorize a mesh peer for data exchange
 * @delete_local_peer: Delete a local mesh peer
 */
struct ieee80211_vendor_mesh_ops {
       int (*add_local_peer)(struct wiphy *wiphy,
                             struct ieee80211_vif *vif,
                             struct ieee80211_mesh_config_params_extn *mesh_params);
       int (*authorize_peer)(struct wiphy *wiphy,
                             struct ieee80211_vif *vif,
                             const u8 *peer_addr);
       int (*delete_local_peer)(struct wiphy *wiphy,
                                struct ieee80211_vif *vif,
                                const u8 *peer_addr);
};

/**
 * ieee80211_get_vendor_mesh_ops - Get vendor mesh operations structure
 *
 * Returns a pointer to the ieee80211_vendor_mesh_ops structure containing
 * function pointers for mesh peer management operations.
 *
 * Return: Pointer to ieee80211_vendor_mesh_ops structure
 */
const struct ieee80211_vendor_mesh_ops *ieee80211_get_vendor_mesh_ops(void);
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */

#endif /* CPTCFG_QCN_EXTN */

/**
 * struct ieee80211_ops_extn - extension callbacks from mac80211 to the driver
 *
 * This structure contains various callbacks that the driver may
 * handle or, in some cases, must handle, for example to configure
 * the hardware to a new channel or to transmit a frame.
 *
 * @get_240mhz_cap_extn: Get the 240MHz Capability information of the STA,
 * 	from ath12K ahvif structure to be used by the mac80211 component during
 * 	the association of STA at the AP.
 * 	This 240MHz capability information is obtained from Vendor NL Event from
 * 	hostapd during association of the STA. Use this information to update
 * 	the EHT phy capability info of the STA in the mac80211.
 * @validate_6ghz_chandef_extn: Validate 6 GHz chandef/power-mode
 *	combination against extension-specific policy (for example,
 *	HW blocklist constraints).
 * @mesh_peer_update_caps_extn: When receiving beacons from the mesh peer, parse the
 * 	relevant elements and update the mesh peer capabilities. Send the peer assoc
 * 	WMI to update the existing peer capabilities to FW.
 */
struct ieee80211_ops_extn {
	struct ieee80211_240mhz_vendor_oper_extn*
	(*get_240mhz_cap_extn) (struct ieee80211_vif *vif,
				struct ieee80211_link_sta *link_sta);
	int (*validate_6ghz_chandef_extn)(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  unsigned int link_id,
					  const struct cfg80211_chan_def *chandef,
					  enum ieee80211_ap_reg_power power_type);
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	int (*mesh_peer_update_caps_extn)(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_sta *sta,
					  struct ieee80211_link_sta *link_sta);
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */
};

/**
 * enum ieee80211_ext_mon_event_type_extn: event type pre/post rtap
 *
 * @IEEE80211_EXT_MON_PRE_RTAP: event type pre rtap addition
 * @IEEE80211_EXT_MON_POST_RTAP: event type post rtap addition
 */
enum ieee80211_ext_mon_event_type_extn {
	IEEE80211_EXT_MON_PRE_RTAP,
	IEEE80211_EXT_MON_POST_RTAP,
};

/**
 * struct ieee80211_ext_mon_rx_event_extn - Extended monitor RX notifier event
 * @mpdu: The raw MPDU SKB as captured by the driver.
 *        No radiotap header is prepended by the driver. The rx_status
 *        is stored in the SKB's control buffer (cb) and can be accessed
 *        via IEEE80211_SKB_RXCB(mpdu). If the listener forwards the
 *        frame to mac80211 via ieee80211_rx_ni(), mac80211 reads the
 *        rx_status from the cb and prepends the radiotap header.
 *
 *        The driver frees this SKB after srcu_notifier_call_chain()
 *        returns. Listeners that need to retain the frame MUST call
 *        skb_clone() inside the callback and take ownership of the
 *        clone; they must NOT free or hold a reference to @mpdu itself.
 *
 * @hw: Pointer to the ieee80211_hw instance this frame arrived on.
 *      This pointer is valid ONLY for the duration of the notifier
 *      callback. Listeners MUST NOT store this pointer in any global
 *      or persistent context for use after the callback returns -- doing
 *      so results in undefined behaviour as the hardware may be torn
 *      down at any time.
 *
 *      If a listener needs to reference the hardware beyond the
 *      callback lifetime (e.g. from a deferred work item), it must
 *      extract hw->wiphy->perm_addr during the callback and use that
 *      MAC address to look up the corresponding wiphy/ieee80211_hw
 *      safely at the point of use.
 *
 * Passed to every registered callback on the ext_mon RX SRCU notifier
 * chain. The chain fires from rxmon workqueue (process) context;
 * callbacks may sleep.
 */
struct ieee80211_ext_mon_rx_event_extn {
	struct sk_buff *mpdu;
	struct ieee80211_hw *hw;
};

#ifndef CPTCFG_QCN_EXTN
static inline int
ieee80211_register_ext_mon_rx_notifier_extn(struct notifier_block *nb)
{
	return -1;
}

static inline int
ieee80211_unregister_ext_mon_rx_notifier_extn(struct notifier_block *nb)
{
	return -1;
}

static inline bool
ieee80211_ext_mon_rx_notifier_has_listeners_extn(void)
{
	return false;
}

static inline int
ieee80211_ext_mon_rx_notifier_call_extn(enum ieee80211_ext_mon_event_type_extn type,
					struct ieee80211_ext_mon_rx_event_extn *event)
{
	return -1;
}
#else
int ieee80211_register_ext_mon_rx_notifier_extn(struct notifier_block *nb);
int ieee80211_unregister_ext_mon_rx_notifier_extn(struct notifier_block *nb);
bool ieee80211_ext_mon_rx_notifier_has_listeners_extn(void);
int
ieee80211_ext_mon_rx_notifier_call_extn(enum ieee80211_ext_mon_event_type_extn type,
					struct ieee80211_ext_mon_rx_event_extn *event);
#endif /* CPTCFG_QCN_EXTN */

#endif /* CMN_EXTN_H */
