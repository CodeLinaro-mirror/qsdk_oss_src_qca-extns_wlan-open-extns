ccflags-y := -Wall -Werror
ccflags-y += -I$(obj)/include -I$(obj)/.. -I$(obj)/ecm_plugins/  -I$(obj)/ppe_plugins/ -I$(obj)/ppeds_plugins/

ccflags-y += -DNSS_CLIENT_BUILD_ID="$(BUILD_ID)"
ccflags-y += -DQCA_WIFI_NSS_PLUGINS_DEBUG_LEVEL=1

qca-wifi-nss-plugins-objs := qca_wifi_nss_plugins_init.o

qca-wifi-nss-plugins-objs += ecm_plugins/qca_wifi_nss_plugins_emesh.o

qca-wifi-nss-plugins-objs += ecm_plugins/qca_wifi_nss_plugins_fse.o
qca-wifi-nss-plugins-objs += ecm_plugins/qca_wifi_nss_plugins_wifi_classifier.o
qca-wifi-nss-plugins-objs += ecm_plugins/qca_wifi_nss_plugins_mscs.o
qca-wifi-nss-plugins-objs += ecm_plugins/qca_wifi_nss_plugins_nl.o

qca-wifi-nss-plugins-objs += ppe_plugins/qca_wifi_nss_plugins_ppe_fse.o
qca-wifi-nss-plugins-objs += ppeds_plugins/qca_wifi_nss_plugins_ppeds.o

obj-m += qca-wifi-nss-plugins.o

